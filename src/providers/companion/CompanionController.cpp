// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/companion/CompanionController.hpp"

#include "Application.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/companion/CompanionSocket.hpp"
#include "providers/companion/GlobalBan.hpp"
#include "singletons/WindowManager.hpp"

#include <QDateTime>

namespace {

/// The signed-in Twitch account, or nothing.
///
/// Null-checks the account controller rather than assuming it: this controller
/// also exists in an application built without one, where reaching through it
/// is an access violation rather than a failed assertion once asserts are
/// compiled out.
chatterino::TwitchAccount *currentAccount()
{
    auto *accounts = chatterino::getApp()->getAccounts();
    if (accounts == nullptr)
    {
        return nullptr;
    }

    return accounts->twitch.getCurrent().get();
}

/// How long a question waits for company before it is asked.
constexpr int flushDelayMs = 400;

/// Batches allowed to be outstanding at once. One keeps the service's load
/// proportional to its own speed rather than to how busy the chat is.
constexpr int maxInFlight = 2;

/// How often to sweep typists whose entry nobody sent a stop for. Frequent
/// enough that a stale line does not linger noticeably, rare enough to be free.
constexpr int typistSweepMs = 2000;

}  // namespace

namespace chatterino {

CompanionController::CompanionController()
{
    this->typistExpiryTimer_.setInterval(typistSweepMs);
    QObject::connect(&this->typistExpiryTimer_, &QTimer::timeout, this, [this] {
        for (const auto &channelId :
             this->typists_.dropExpired(QDateTime::currentMSecsSinceEpoch()))
        {
            Q_EMIT this->typistsChanged(channelId);
        }

        if (this->typists_.isEmpty())
        {
            this->typistExpiryTimer_.stop();
        }
    });

    this->flushTimer_.setSingleShot(true);
    this->flushTimer_.setInterval(flushDelayMs);
    QObject::connect(&this->flushTimer_, &QTimer::timeout, this, [this] {
        this->flush();
    });
}

void CompanionController::followSetting(QStringSetting &setting)
{
    setting.connect(
        [this](const QString &url) {
            this->setBaseUrl(url);
        },
        this->signalHolder_);

    // Signing in changes what the service can answer, and the reason shown
    // while signed out should stop being shown the moment it stops applying.
    if (auto *accounts = getApp()->getAccounts(); accounts != nullptr)
    {
        this->signalHolder_.managedConnect(accounts->twitch.currentUserChanged,
                                           [this] {
                                               this->refreshAvailability();
                                           });
    }
}

CompanionController::~CompanionController() = default;

void CompanionController::setBaseUrl(const QString &baseUrl)
{
    auto hadAnswers = !this->registry_.isEmpty();

    this->api_.setBaseUrl(baseUrl);
    this->registry_.clear();
    this->presence_.clear();
    this->flushTimer_.stop();
    this->refreshAvailability();

    if (this->socket_ || this->api_.isConfigured())
    {
        this->socket()->setBaseUrl(baseUrl);
    }

    if (hadAnswers)
    {
        // Markers already drawn were answers from the previous service, so
        // they have to come off the screen even though nothing has replaced
        // them yet. Nothing was held on the way up from startup, though, and
        // relayouting every view then would be work for no change.
        CompanionController::relayout();
    }
}

bool CompanionController::isEnabled() const
{
    return this->api_.isConfigured();
}

CompanionSocket *CompanionController::socket()
{
    if (!this->socket_)
    {
        this->socket_ = std::make_unique<CompanionSocket>();

        QObject::connect(this->socket_.get(),
                         &CompanionSocket::typingChanged, this,
                         [this](const QString &channelId, const QString &login,
                                bool active) {
                             auto now =
                                 QDateTime::currentMSecsSinceEpoch();
                             if (this->typists_.set(channelId, login, active,
                                                    now))
                             {
                                 Q_EMIT this->typistsChanged(channelId);
                             }

                             if (!this->typists_.isEmpty())
                             {
                                 this->typistExpiryTimer_.start();
                             }
                         });

        QObject::connect(this->socket_.get(), &CompanionSocket::disconnected,
                         this, [this] {
                             // Nothing learned before the drop is still true,
                             // and leaving a line up would claim somebody is
                             // typing into a socket that is gone.
                             this->clearTypists();
                         });

        QObject::connect(this->socket_.get(), &CompanionSocket::connected, this,
                         [this] {
                             for (auto it = this->watchers_.constBegin();
                                  it != this->watchers_.constEnd(); ++it)
                             {
                                 this->socket_->join(it.key());
                             }
                         });
    }

    return this->socket_.get();
}

void CompanionController::clearTypists()
{
    QStringList had;
    for (auto it = this->watchers_.constBegin();
         it != this->watchers_.constEnd(); ++it)
    {
        if (!this->typists_.typists(it.key(),
                                    QDateTime::currentMSecsSinceEpoch())
                 .isEmpty())
        {
            had.append(it.key());
        }
    }

    this->typists_.clear();
    this->typingNotifiers_.clear();
    this->typistExpiryTimer_.stop();

    for (const auto &channelId : had)
    {
        Q_EMIT this->typistsChanged(channelId);
    }
}

void CompanionController::watchChannel(const QString &channelId)
{
    if (channelId.isEmpty())
    {
        return;
    }

    if (++this->watchers_[channelId] == 1 && this->isEnabled())
    {
        this->socket()->join(channelId);
    }
}

void CompanionController::unwatchChannel(const QString &channelId)
{
    auto watcher = this->watchers_.find(channelId);
    if (watcher == this->watchers_.end())
    {
        return;
    }

    if (--watcher.value() > 0)
    {
        return;
    }

    this->watchers_.erase(watcher);
    this->typists_.forget(channelId);
    this->typingNotifiers_.remove(channelId);

    if (this->socket_)
    {
        this->socket_->part(channelId);
    }
}

int CompanionController::watcherCount(const QString &channelId) const
{
    return this->watchers_.value(channelId, 0);
}

QString CompanionController::typistsText(const QString &channelId) const
{
    return TypingTracker::describe(this->typists_.typists(
        channelId, QDateTime::currentMSecsSinceEpoch()));
}

void CompanionController::reportInput(const QString &channelId, bool hasText)
{
    if (!this->isEnabled() || channelId.isEmpty())
    {
        return;
    }

    auto decision = this->typingNotifiers_[channelId].inputChanged(
        hasText, QDateTime::currentMSecsSinceEpoch());

    if (decision.has_value())
    {
        this->socket()->sendTyping(channelId, *decision);
    }
}

void CompanionController::reportMessageSent(const QString &channelId)
{
    if (!this->isEnabled() || channelId.isEmpty())
    {
        return;
    }

    auto decision = this->typingNotifiers_[channelId].messageSent(
        QDateTime::currentMSecsSinceEpoch());

    if (decision.has_value())
    {
        this->socket()->sendTyping(channelId, *decision);
    }
}

CompanionStatus CompanionController::status() const
{
    return this->health_.status();
}

void CompanionController::refreshAvailability()
{
    auto *account = currentAccount();
    auto signedIn = account != nullptr && !account->isAnon();

    if (this->health_.setAvailability(this->api_.isConfigured(), signedIn))
    {
        Q_EMIT this->statusChanged(this->health_.status());
    }
}

void CompanionController::noteResult(bool ok)
{
    auto changed = ok ? this->health_.recordSuccess()
                      : this->health_.recordFailure();

    if (changed)
    {
        Q_EMIT this->statusChanged(this->health_.status());
    }
}

GlobalBanRegistry &CompanionController::globalBans()
{
    return this->registry_;
}

const GlobalBanRegistry &CompanionController::globalBans() const
{
    return this->registry_;
}

void CompanionController::noteChatter(const QString &channelId,
                                      const QString &userId)
{
    if (!this->isEnabled())
    {
        return;
    }

    auto now = QDateTime::currentMSecsSinceEpoch();

    auto queued = this->registry_.note(channelId, userId);
    queued = this->presence_.note(userId, now) || queued;

    if (queued)
    {
        this->scheduleFlush();
    }
}

std::optional<PresenceState> CompanionController::presenceOf(
    const QString &userId) const
{
    return this->presence_.state(userId,
                                 QDateTime::currentMSecsSinceEpoch());
}

void CompanionController::scheduleFlush()
{
    if (!this->flushTimer_.isActive())
    {
        this->flushTimer_.start();
    }
}

void CompanionController::flush()
{
    if (!this->isEnabled())
    {
        return;
    }

    while (this->inFlight_ < maxInFlight)
    {
        auto batch = this->registry_.takeBatch();
        if (!batch)
        {
            return;
        }

        this->inFlight_++;

        auto channelId = batch->channelId;
        auto userIds = batch->userIds;

        this->api_.fetchMarkers(
            userIds, channelId,
            [this, channelId, userIds](std::optional<QHash<QString, int>>
                                           markers) {
                this->inFlight_--;
                this->noteResult(markers.has_value());

                if (!markers)
                {
                    this->registry_.failBatch(channelId, userIds);
                    return;
                }

                if (this->registry_.applyMarkers(channelId, userIds, *markers))
                {
                    CompanionController::relayout();
                }

                // A batch finishing frees a slot, and a busy chat will have
                // queued more while this one was out.
                if (this->registry_.pendingCount() > 0)
                {
                    this->scheduleFlush();
                }
            });
    }

    this->flushPresence();

    // Still queued but out of slots: come back when a slot frees up, which the
    // callback above arranges, or after another delay if none does.
    if (this->registry_.pendingCount() > 0)
    {
        this->scheduleFlush();
    }
}

void CompanionController::flushPresence()
{
    auto batch = this->presence_.takeBatch();
    if (batch.isEmpty())
    {
        return;
    }

    this->api_.fetchPresence(
        batch, [this, batch](std::optional<QHash<QString, PresenceState>>
                                 states) {
            this->noteResult(states.has_value());

            if (!states)
            {
                this->presence_.failBatch(batch);
                return;
            }

            if (this->presence_.applyStates(
                    batch, *states, QDateTime::currentMSecsSinceEpoch()))
            {
                CompanionController::relayout();
            }
        });
}

void CompanionController::fetchHistory(
    const QString &offenderId, const QString &channelId,
    const std::function<void(std::optional<GlobalBanSummary>)> &callback)
{
    this->api_.fetchHistory(
        offenderId, channelId,
        [this, callback](std::optional<GlobalBanSummary> summary) {
            this->noteResult(summary.has_value());
            callback(std::move(summary));
        });
}

void CompanionController::vouch(const QString &offenderId,
                                const QString &channelId,
                                const std::function<void(bool)> &callback)
{
    this->api_.vouch(offenderId, channelId, [this, offenderId, channelId,
                                             callback](bool ok) {
        if (ok)
        {
            // The count we hold is now wrong in a way only the service can
            // correct, so ask again at once rather than waiting for them to
            // speak.
            this->registry_.refresh(channelId, offenderId);
            this->flush();
        }

        callback(ok);
    });
}

void CompanionController::withdrawVouch(
    const QString &offenderId, const QString &channelId,
    const std::function<void(bool)> &callback)
{
    this->api_.withdrawVouch(offenderId, channelId, [this, offenderId,
                                                     channelId,
                                                     callback](bool ok) {
        if (ok)
        {
            this->registry_.refresh(channelId, offenderId);
            this->flush();
        }

        callback(ok);
    });
}

void CompanionController::relayout()
{
    auto *windows = getApp()->getWindows();
    if (windows != nullptr)
    {
        windows->layoutChannelViews();
    }
}

}  // namespace chatterino

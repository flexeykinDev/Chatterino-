// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/companion/CompanionController.hpp"

#include "Application.hpp"
#include "providers/companion/GlobalBan.hpp"
#include "singletons/WindowManager.hpp"

namespace {

/// How long a question waits for company before it is asked.
constexpr int flushDelayMs = 400;

/// Batches allowed to be outstanding at once. One keeps the service's load
/// proportional to its own speed rather than to how busy the chat is.
constexpr int maxInFlight = 2;

}  // namespace

namespace chatterino {

CompanionController::CompanionController()
{
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
}

void CompanionController::setBaseUrl(const QString &baseUrl)
{
    auto hadAnswers = !this->registry_.isEmpty();

    this->api_.setBaseUrl(baseUrl);
    this->registry_.clear();
    this->flushTimer_.stop();

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

    if (this->registry_.note(channelId, userId))
    {
        this->scheduleFlush();
    }
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

    // Still queued but out of slots: come back when a slot frees up, which the
    // callback above arranges, or after another delay if none does.
    if (this->registry_.pendingCount() > 0)
    {
        this->scheduleFlush();
    }
}

void CompanionController::fetchHistory(
    const QString &offenderId, const QString &channelId,
    const std::function<void(std::optional<GlobalBanSummary>)> &callback)
{
    this->api_.fetchHistory(offenderId, channelId, callback);
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

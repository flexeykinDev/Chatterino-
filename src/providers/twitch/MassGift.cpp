// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/twitch/MassGift.hpp"

#include "common/Literals.hpp"
#include "providers/twitch/TwitchCommon.hpp"
#include "util/IrcHelpers.hpp"

#include <QCoreApplication>
#include <QStringBuilder>
#include <QStringList>

#include <algorithm>
#include <utility>

namespace chatterino {

using namespace literals;

int massGiftTier(const QString &subPlan)
{
    // "1000", "2000", "3000". Anything else — "Prime", an empty tag, something
    // Twitch has not shipped yet — reads as tier 1 rather than as garbage.
    bool ok = false;
    int thousands = subPlan.toInt(&ok) / 1000;
    if (!ok || thousands < 1 || thousands > 3)
    {
        return 1;
    }

    return thousands;
}

QString massGiftIdOf(Communi::TagsRef tags)
{
    auto id = tags.getOrEmpty("msg-param-community-gift-id");
    if (!id.isEmpty())
    {
        return id;
    }

    // Twitch sent only `msg-param-origin-id` for years and still sends it
    // alongside the newer tag with the same value. Older messages, and
    // whatever the recent-messages service has cached, only have this one.
    return tags.getOrEmpty("msg-param-origin-id");
}

std::optional<MassGift> parseMassGiftAnnouncement(Communi::TagsRef tags)
{
    if (tags.getOrEmpty("msg-id") != QStringLiteral("submysterygift"))
    {
        return std::nullopt;
    }

    auto id = massGiftIdOf(tags);
    if (id.isEmpty())
    {
        // Without an id there is nothing to match the individual gifts
        // against, so folding them in would swallow unrelated ones.
        return std::nullopt;
    }

    MassGift gift;
    gift.id = id;
    gift.gifterLogin = tags.getOrEmpty("login");
    gift.gifterDisplayName = tags.getOrEmpty("display-name");
    if (gift.gifterDisplayName.isEmpty())
    {
        gift.gifterDisplayName = gift.gifterLogin;
    }
    gift.gifterUserId = tags.getOrEmpty("user-id");
    gift.gifterColor = tags.getOrEmpty("color");
    gift.anonymous = ANONYMOUS_GIFTER_ID == gift.gifterUserId;
    // `msg-param-mass-gift-count` is this gift's size. `msg-param-sender-count`
    // beside it is how many the gifter has ever given the channel, which is a
    // much larger number and not what is being announced.
    gift.promisedCount = tags.getOrEmpty("msg-param-mass-gift-count").toInt();
    gift.tier = massGiftTier(tags.getOrEmpty("msg-param-sub-plan"));
    gift.announcement = parseTagString(tags.getOrEmpty("system-msg"));
    gift.messageId = tags.getOrEmpty("id");

    return gift;
}

MassGiftRecipient parseMassGiftRecipient(Communi::TagsRef tags)
{
    MassGiftRecipient recipient;
    recipient.login = tags.getOrEmpty("msg-param-recipient-user-name");
    if (recipient.login.isEmpty())
    {
        recipient.login = tags.getOrEmpty("msg-param-recipient-name");
    }
    recipient.displayName =
        tags.getOrEmpty("msg-param-recipient-display-name");
    if (recipient.displayName.isEmpty())
    {
        recipient.displayName = recipient.login;
    }
    recipient.userId = tags.getOrEmpty("msg-param-recipient-id");

    return recipient;
}

MassGiftTracker::MassGiftTracker(Clock clock)
    : clock_(std::move(clock))
{
    if (!this->clock_)
    {
        this->clock_ = [] {
            return std::chrono::steady_clock::now();
        };
    }
}

MassGiftTracker::Open *MassGiftTracker::lookup(const QString &id)
{
    auto found = std::ranges::find_if(this->open_, [&id](const Open &open) {
        return open.gift.id == id;
    });

    return found == this->open_.end() ? nullptr : &*found;
}

const MassGiftTracker::Open *MassGiftTracker::lookup(const QString &id) const
{
    auto found = std::ranges::find_if(this->open_, [&id](const Open &open) {
        return open.gift.id == id;
    });

    return found == this->open_.end() ? nullptr : &*found;
}

void MassGiftTracker::prune()
{
    auto now = this->clock_();

    // Only the three mutating entry points prune, and each does it before
    // handing out a pointer, so nothing a caller is holding is erased under it.
    std::erase_if(this->open_, [now](const Open &open) {
        return now - open.lastSeen >= idleFor;
    });

    while (this->open_.size() > maxOpen)
    {
        this->open_.pop_front();
    }
}

MassGift *MassGiftTracker::announce(MassGift gift)
{
    this->prune();

    if (auto *existing = this->lookup(gift.id))
    {
        // Twitch has been known to repeat an announcement. Keeping the
        // recipients already collected means the summary does not lose them.
        auto recipients = std::move(existing->gift.recipients);
        existing->gift = std::move(gift);
        existing->gift.recipients = std::move(recipients);
        existing->lastSeen = this->clock_();
        return &existing->gift;
    }

    this->open_.push_back({
        .gift = std::move(gift),
        .summary = {},
        .lastSeen = this->clock_(),
    });

    // One past the cap at most, since exactly one was added.
    if (this->open_.size() > maxOpen)
    {
        this->open_.pop_front();
    }

    return &this->open_.back().gift;
}

MassGift *MassGiftTracker::absorb(const QString &id,
                                  const MassGiftRecipient &recipient)
{
    if (id.isEmpty())
    {
        return nullptr;
    }

    this->prune();

    auto *open = this->lookup(id);
    if (open == nullptr)
    {
        return nullptr;
    }

    open->gift.recipients.push_back(recipient);
    open->lastSeen = this->clock_();

    return &open->gift;
}

MassGift *MassGiftTracker::find(const QString &id)
{
    if (id.isEmpty())
    {
        return nullptr;
    }

    this->prune();

    auto *open = this->lookup(id);
    return open == nullptr ? nullptr : &open->gift;
}

MessagePtr MassGiftTracker::summaryOf(const QString &id) const
{
    const auto *open = this->lookup(id);
    return open == nullptr ? MessagePtr{} : open->summary;
}

void MassGiftTracker::setSummary(const QString &id, MessagePtr summary)
{
    if (auto *open = this->lookup(id))
    {
        open->summary = std::move(summary);
    }
}

std::size_t MassGiftTracker::openCount() const
{
    return this->open_.size();
}

MassGiftRecipientsView massGiftRecipientsView(const MassGift &gift)
{
    MassGiftRecipientsView view;
    auto total = gift.recipients.size();
    auto shown = std::min(total, MassGiftTracker::shownRecipients);

    view.shown.assign(gift.recipients.begin(),
                      gift.recipients.begin() +
                          static_cast<std::ptrdiff_t>(shown));
    view.more = static_cast<int>(total - shown);

    return view;
}

QString massGiftRecipientsTemplate()
{
    return QCoreApplication::translate("MassGift", "Gifted to %1");
}

QString massGiftMoreText(int more)
{
    if (more <= 0)
    {
        return {};
    }

    return QCoreApplication::translate("MassGift", "and %n more", "", more);
}

QString massGiftRecipientsList(const MassGift &gift)
{
    auto view = massGiftRecipientsView(gift);
    if (view.shown.empty())
    {
        return {};
    }

    QStringList names;
    names.reserve(static_cast<qsizetype>(view.shown.size()));
    for (const auto &recipient : view.shown)
    {
        names.append(recipient.displayName);
    }

    auto joined = names.join(QStringLiteral(", "));
    auto more = massGiftMoreText(view.more);
    if (more.isEmpty())
    {
        return joined;
    }

    return joined % u' ' % more;
}

QString massGiftRecipientsText(const MassGift &gift)
{
    auto list = massGiftRecipientsList(gift);
    if (list.isEmpty())
    {
        return {};
    }

    return massGiftRecipientsTemplate().arg(list);
}

}  // namespace chatterino

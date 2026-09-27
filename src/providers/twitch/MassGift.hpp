// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <IrcTagsRef>
#include <QDateTime>
#include <QString>

#include <chrono>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace chatterino {

struct Message;
using MessagePtr = std::shared_ptr<const Message>;

/// Somebody who received a sub out of a community gift.
struct MassGiftRecipient {
    QString login;
    QString displayName;
    QString userId;
};

/// One person gifting a pile of subs to a channel at once.
///
/// Twitch announces this as a single `submysterygift` and then sends a separate
/// `subgift` for every recipient. A hundred-sub gift is therefore a hundred and
/// one messages, which is the whole of chat for the next few screens. This
/// holds the announcement so the individual gifts can be folded into it.
struct MassGift {
    /// Twitch's `msg-param-community-gift-id`, which every gift in the pile
    /// carries. The announcement and its gifts are tied together by nothing
    /// else — not the gifter, who may be gifting twice at once.
    QString id;
    /// The announcement's own `id` tag. A rebuilt summary keeps it, so it stays
    /// the same message as far as search and replacement are concerned.
    QString messageId;
    /// When Twitch sent the announcement. Kept so a rebuild does not restamp
    /// the summary with the time the latest gift happened to arrive.
    QDateTime receivedAt;
    QString gifterLogin;
    QString gifterDisplayName;
    QString gifterUserId;
    /// The `color` tag as Twitch sent it, so the summary can be rebuilt later
    /// without keeping the original message around.
    QString gifterColor;
    /// Twitch attributes an anonymous gift to a real account with a reserved
    /// id, so the gifter's name is a real name that must not be shown.
    bool anonymous = false;
    /// How many subs the announcement promised. Known upfront, which is why the
    /// summary can be written before a single gift has arrived.
    int promisedCount = 0;
    /// 1, 2 or 3. Never Prime: Prime subs cannot be gifted.
    int tier = 1;
    /// Twitch's own `system-msg`, unescaped. Used rather than rephrased,
    /// because it already reads correctly for every shape of gift.
    QString announcement;
    std::vector<MassGiftRecipient> recipients;
};

/// The tier a `msg-param-sub-plan` names.
///
/// Twitch sends "1000", "2000", "3000" or "Prime". Taking the first character,
/// as the sub message path does, turns Prime into tier 'P'; this answers 1 for
/// anything it does not recognise instead.
int massGiftTier(const QString &subPlan);

/// Reads a `submysterygift` USERNOTICE's tags. Empty when the tags are not one,
/// or are missing the gift id that the individual gifts would be matched on.
std::optional<MassGift> parseMassGiftAnnouncement(Communi::TagsRef tags);

/// The community gift a `subgift` belongs to, if it says.
///
/// A single gift carries one of these too, so its presence does not mean a
/// gift is part of a pile — only a tracked announcement does.
QString massGiftIdOf(Communi::TagsRef tags);

/// Reads who a `subgift` went to.
MassGiftRecipient parseMassGiftRecipient(Communi::TagsRef tags);

/// Keeps the community gifts a channel has announced but not finished sending.
///
/// Deliberately not thread-safe: USERNOTICEs are handled on one thread, the
/// same one that already mutates a channel's queued redemptions.
class MassGiftTracker
{
public:
    using Clock = std::function<std::chrono::steady_clock::time_point()>;

    /// The most recipients the summary names. Past this it says how many more
    /// there were, because a hundred names is the flood again in one message.
    static constexpr std::size_t shownRecipients = 12;

    /// How long a gift keeps absorbing after its last arrival. Twitch sends a
    /// pile within seconds; a gift still open minutes later has lost some of
    /// its messages, and its stragglers are better shown than swallowed.
    static constexpr std::chrono::milliseconds idleFor{30'000};

    /// How many gifts can be open at once. Two people gifting simultaneously
    /// is ordinary; eight is not, and the cap stops a channel that sees a
    /// malformed id every message from growing this without bound.
    static constexpr std::size_t maxOpen = 8;

    /// `clock` is injected so the expiry can be tested without waiting.
    explicit MassGiftTracker(Clock clock = {});

    /// Starts tracking an announced gift, replacing any earlier one with the
    /// same id. Returns the stored gift.
    MassGift *announce(MassGift gift);

    /// Records a recipient against an open gift. Null when no announcement for
    /// `id` is open, which means the gift should be shown on its own.
    MassGift *absorb(const QString &id, const MassGiftRecipient &recipient);

    /// The open gift with this id, or null.
    MassGift *find(const QString &id);

    /// The summary message currently standing in for a gift, so it can be
    /// replaced as recipients arrive.
    [[nodiscard]] MessagePtr summaryOf(const QString &id) const;
    void setSummary(const QString &id, MessagePtr summary);

    [[nodiscard]] std::size_t openCount() const;

private:
    struct Open {
        MassGift gift;
        MessagePtr summary;
        std::chrono::steady_clock::time_point lastSeen;
    };

    /// Drops gifts that have gone quiet, and the oldest if there are too many.
    void prune();
    [[nodiscard]] Open *lookup(const QString &id);
    [[nodiscard]] const Open *lookup(const QString &id) const;

    Clock clock_;
    /// A deque rather than a vector: pointers into it stay valid across
    /// pushing a gift on the back and dropping one off the front, and callers
    /// hold one across an absorb.
    std::deque<Open> open_;
};

/// The recipients a summary should name, and how many it has to leave out.
struct MassGiftRecipientsView {
    std::vector<MassGiftRecipient> shown;
    /// Recipients beyond `shown`. Zero when all of them fit.
    int more = 0;
};

/// Caps a gift's recipients to what a single message should name.
MassGiftRecipientsView massGiftRecipientsView(const MassGift &gift);

/// The sentence the names go into, with `%1` where they belong.
///
/// Exposed rather than applied, because the built message puts clickable
/// mentions in place of `%1` and must not have to unpick a translated string
/// to find out where that is.
QString massGiftRecipientsTemplate();

/// "and 3 more", or empty when nothing was left out.
QString massGiftMoreText(int more);

/// The names, comma-separated, with any leftover count appended.
QString massGiftRecipientsList(const MassGift &gift);

/// What to add after the announcement to name who received the subs, or empty
/// when none have arrived yet. This is the message's own text, for search; the
/// built message says the same thing with the names clickable.
QString massGiftRecipientsText(const MassGift &gift);

}  // namespace chatterino

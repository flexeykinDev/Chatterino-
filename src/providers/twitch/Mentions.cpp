// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/twitch/Mentions.hpp"

#include "messages/Message.hpp"
#include "messages/MessageFlag.hpp"

#include <QCoreApplication>
#include <QStringBuilder>

#include <algorithm>
#include <ranges>

namespace chatterino {

QString mentionReplyProblem(const Message &message, bool channelIsOpen)
{
    // Order matters: Message::isReplyable() refuses whispers and system
    // messages along with everything else, so asking it first would answer
    // "too old" for a whisper that arrived a second ago.
    if (message.flags.has(MessageFlag::Whisper))
    {
        return QCoreApplication::translate(
            "Mentions", "Whispers are answered in the Whispers tab.");
    }

    if (message.flags.hasAny({
            MessageFlag::AutoMod,
            MessageFlag::AutoModOffendingMessage,
            MessageFlag::AutoModOffendingMessageHeader,
            MessageFlag::AutoModBlockedTerm,
        }))
    {
        return QCoreApplication::translate(
            "Mentions",
            "This is an AutoMod notice about somebody else's message.");
    }

    if (message.flags.has(MessageFlag::System))
    {
        return QCoreApplication::translate(
            "Mentions", "This is a notice, not a message somebody sent.");
    }

    if (message.channelName.isEmpty())
    {
        return QCoreApplication::translate(
            "Mentions", "This mention does not say which channel it was in.");
    }

    if (!channelIsOpen)
    {
        return QCoreApplication::translate("Mentions",
                                           "You have left #%1, so a reply "
                                           "would have nowhere to go.")
            .arg(message.channelName);
    }

    if (message.id.isEmpty())
    {
        // Twitch threads a reply onto a message id. Without one there is
        // nothing to thread onto, and sending the text on its own would answer
        // a different conversation than the one being read.
        return QCoreApplication::translate(
            "Mentions", "Twitch gave this message no id to reply to.");
    }

    switch (message.isReplyable())
    {
        case Message::ReplyStatus::Replyable:
        case Message::ReplyStatus::ReplyableWithThread:
            return {};

        case Message::ReplyStatus::NotReplyableDueToThread:
            return QCoreApplication::translate(
                "Mentions",
                "The message this one replies to can no longer be replied to.");

        case Message::ReplyStatus::NotReplyable:
        default:
            // Twitch refuses a reply to anything older than a day, and to a
            // deleted message.
            return QCoreApplication::translate(
                "Mentions", "This message is too old to reply to.");
    }
}

std::optional<MentionReplyTarget> mentionReplyTarget(const Message &message,
                                                     bool channelIsOpen)
{
    if (!mentionReplyProblem(message, channelIsOpen).isEmpty())
    {
        return std::nullopt;
    }

    return MentionReplyTarget{
        .channelName = message.channelName,
        .messageId = message.id,
        .userLogin = message.loginName,
        .displayName = message.displayName.isEmpty() ? message.loginName
                                                     : message.displayName,
    };
}

std::vector<MessagePtr> replyableMentions(const std::vector<MessagePtr> &mentions,
                                          const ChannelIsOpen &channelIsOpen)
{
    std::vector<MessagePtr> replyable;

    for (const auto &message : mentions)
    {
        if (message == nullptr)
        {
            continue;
        }

        bool isOpen =
            channelIsOpen ? channelIsOpen(message->channelName) : false;
        if (mentionReplyProblem(*message, isOpen).isEmpty())
        {
            replyable.push_back(message);
        }
    }

    // Newest first: the mention you want to answer is nearly always the last
    // one, so it should be what the reply box offers without any stepping.
    std::ranges::reverse(replyable);

    return replyable;
}

QString describeMention(const Message &message)
{
    auto who = message.displayName.isEmpty() ? message.loginName
                                             : message.displayName;

    auto text = message.messageText.simplified();
    if (text.length() > mentionPreviewLength)
    {
        text = text.left(mentionPreviewLength).trimmed() % u"…";
    }

    if (message.channelName.isEmpty())
    {
        return QCoreApplication::translate("Mentions", "@%1: %2")
            .arg(who, text);
    }

    return QCoreApplication::translate("Mentions", "@%1 in #%2: %3")
        .arg(who, message.channelName, text);
}

QString newestMentionId(const std::vector<MessagePtr> &mentions)
{
    for (const auto &message : std::ranges::reverse_view(mentions))
    {
        if (message != nullptr && !message->id.isEmpty())
        {
            return message->id;
        }
    }

    return {};
}

int unreadMentions(const std::vector<MessagePtr> &mentions,
                   const QString &lastSeenId)
{
    if (lastSeenId.isEmpty())
    {
        return static_cast<int>(mentions.size());
    }

    // Counting backwards from the end stops at the first thing already read,
    // rather than walking a thousand-message buffer to find where to start.
    int unread = 0;
    for (const auto &message : std::ranges::reverse_view(mentions))
    {
        if (message != nullptr && message->id == lastSeenId)
        {
            return unread;
        }
        unread++;
    }

    return unread;
}

QString mentionButtonLabel(int unread)
{
    if (unread <= 0)
    {
        return QStringLiteral("@");
    }

    if (unread > mentionCountCap)
    {
        return QStringLiteral("@ %1+").arg(mentionCountCap);
    }

    return QStringLiteral("@ %1").arg(unread);
}

}  // namespace chatterino

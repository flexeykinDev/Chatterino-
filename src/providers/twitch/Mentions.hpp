// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace chatterino {

struct Message;
using MessagePtr = std::shared_ptr<const Message>;

/// Where a reply to a mention would be sent.
struct MentionReplyTarget {
    /// The channel the mention was in, which is where the reply goes — not
    /// whichever channel happens to be in front.
    QString channelName;
    /// The message the reply threads onto.
    QString messageId;
    QString userLogin;
    QString displayName;
};

/// Answers whether a channel is currently joined.
///
/// Passed in rather than reached for, so every rule below can be tested
/// without an application around it.
using ChannelIsOpen = std::function<bool(const QString &)>;

/// Why this mention cannot be answered from the mentions list, or empty when it
/// can be.
///
/// Says what is wrong rather than only refusing: a reply box that is disabled
/// for no stated reason is worse than one that explains itself.
QString mentionReplyProblem(const Message &message, bool channelIsOpen);

/// Where a reply would go, or nothing when #mentionReplyProblem() has
/// something to say.
std::optional<MentionReplyTarget> mentionReplyTarget(const Message &message,
                                                     bool channelIsOpen);

/// The mentions that can be answered, newest first.
///
/// The list is filtered rather than the unanswerable ones being disabled,
/// because stepping through mentions only to find half of them refuse is worse
/// than being offered the ones that work.
std::vector<MessagePtr> replyableMentions(const std::vector<MessagePtr> &mentions,
                                          const ChannelIsOpen &channelIsOpen);

/// A short description of a mention, enough to tell which one is selected:
/// who said it, where, and the beginning of what they said.
QString describeMention(const Message &message);

/// How much of the message itself #describeMention() quotes.
inline constexpr int mentionPreviewLength = 48;

/// The id of the newest mention that has one, for remembering what has been
/// read. Empty when none of them have an id, which system notices do not.
QString newestMentionId(const std::vector<MessagePtr> &mentions);

/// How many mentions arrived after the one last looked at.
///
/// An id that is not in the list counts everything as unread: either it
/// scrolled out of the buffer, in which case everything after it did too, or it
/// was never there.
int unreadMentions(const std::vector<MessagePtr> &mentions,
                   const QString &lastSeenId);

/// The most a count is shown as before it becomes "99+". A button that grows to
/// fit four digits pushes the tabs around for no useful gain.
inline constexpr int mentionCountCap = 99;

/// What the button in the tab bar says.
QString mentionButtonLabel(int unread);

}  // namespace chatterino

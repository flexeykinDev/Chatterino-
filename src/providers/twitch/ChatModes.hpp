// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QCoreApplication>
#include <QString>

#include <vector>

namespace chatterino {

/// The restrictions a channel can put on its chat.
enum class ChatMode {
    /// Only followers may speak, optionally only after following for a while.
    Followers,
    /// A wait between messages.
    Slow,
    Subscribers,
    EmoteOnly,
    /// No repeating what somebody already said. Twitch has called this r9k.
    UniqueChat,
};

/// One of the durations a mode can be set to without typing a number.
struct ChatModePreset {
    /// Minutes for followers, seconds for slow. Negative turns the mode off.
    int amount = 0;
    QString label;
};

/// A mode as it should appear in the menu.
struct ChatModeRow {
    ChatMode mode;
    /// Whether the channel currently has it on.
    bool active = false;
    /// What it is set to, when that is a number worth seeing: "10m", "30s".
    /// Empty for modes that are simply on or off.
    QString value;
};

/// Reads the current state of a channel's modes.
///
/// `followerMinutes` is Twitch's own encoding: -1 for off, 0 for anyone who
/// follows, and otherwise how long they must have followed. Collapsing those
/// first two into a single "off" is the mistake this exists to avoid — a
/// channel where any follower may speak is restricted, and a menu that shows
/// it as off is lying.
std::vector<ChatModeRow> chatModeRows(int followerMinutes, int slowSeconds,
                                      bool subscribers, bool emoteOnly,
                                      bool uniqueChat);

/// The durations offered for a mode, newest-first as they should be listed.
/// Empty for a mode that is only on or off.
std::vector<ChatModePreset> chatModePresets(ChatMode mode);

/// The command that puts a mode into a state. `amount` is ignored by modes
/// that do not take one, and a negative amount turns the mode off.
QString chatModeCommand(ChatMode mode, int amount);

/// The name to show for a mode.
QString chatModeLabel(ChatMode mode);

}  // namespace chatterino

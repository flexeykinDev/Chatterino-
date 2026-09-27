// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/twitch/ChatModes.hpp"

namespace {

using namespace chatterino;

/// "10m", "2h", "1d" — the shortest thing that still says what it means. A
/// menu row has no space for "10 minutes" beside a label and a chevron.
QString shortMinutes(int minutes)
{
    if (minutes % (60 * 24) == 0)
    {
        return QCoreApplication::translate("ChatModes", "%1d")
            .arg(minutes / (60 * 24));
    }

    if (minutes % 60 == 0)
    {
        return QCoreApplication::translate("ChatModes", "%1h").arg(minutes / 60);
    }

    return QCoreApplication::translate("ChatModes", "%1m").arg(minutes);
}

QString shortSeconds(int seconds)
{
    return QCoreApplication::translate("ChatModes", "%1s").arg(seconds);
}

}  // namespace

namespace chatterino {

std::vector<ChatModeRow> chatModeRows(int followerMinutes, int slowSeconds,
                                      bool subscribers, bool emoteOnly,
                                      bool uniqueChat)
{
    std::vector<ChatModeRow> rows;

    // Follower-only is three states, not two: off, anyone who follows, and
    // only those who have followed for a while. Treating the middle one as
    // off would show a restricted chat as unrestricted.
    ChatModeRow followers{.mode = ChatMode::Followers,
                          .active = followerMinutes >= 0,
                          .value = {}};
    if (followerMinutes > 0)
    {
        followers.value = shortMinutes(followerMinutes);
    }
    rows.push_back(followers);

    rows.push_back({
        .mode = ChatMode::Slow,
        .active = slowSeconds > 0,
        .value = slowSeconds > 0 ? shortSeconds(slowSeconds) : QString{},
    });

    rows.push_back({.mode = ChatMode::Subscribers, .active = subscribers});
    rows.push_back({.mode = ChatMode::EmoteOnly, .active = emoteOnly});
    rows.push_back({.mode = ChatMode::UniqueChat, .active = uniqueChat});

    return rows;
}

std::vector<ChatModePreset> chatModePresets(ChatMode mode)
{
    switch (mode)
    {
        case ChatMode::Followers:
            return {
                // Zero is a real setting, not "off": anyone who follows may
                // speak, no matter how recently.
                {0, QCoreApplication::translate("ChatModes", "any follower")},
                {10, shortMinutes(10)},
                {30, shortMinutes(30)},
                {60, shortMinutes(60)},
                {60 * 24, shortMinutes(60 * 24)},
                {-1, QCoreApplication::translate("ChatModes", "off")},
            };

        case ChatMode::Slow:
            return {
                {3, shortSeconds(3)},
                {5, shortSeconds(5)},
                {10, shortSeconds(10)},
                {30, shortSeconds(30)},
                {60, shortSeconds(60)},
                {-1, QCoreApplication::translate("ChatModes", "off")},
            };

        default:
            // On or off; nothing to choose.
            return {};
    }
}

QString chatModeCommand(ChatMode mode, int amount)
{
    auto off = amount < 0;

    switch (mode)
    {
        case ChatMode::Followers:
            return off ? QStringLiteral("/followersoff")
                       : QStringLiteral("/followers %1").arg(amount);

        case ChatMode::Slow:
            return off ? QStringLiteral("/slowoff")
                       : QStringLiteral("/slow %1").arg(amount);

        case ChatMode::Subscribers:
            return off ? QStringLiteral("/subscribersoff")
                       : QStringLiteral("/subscribers");

        case ChatMode::EmoteOnly:
            return off ? QStringLiteral("/emoteonlyoff")
                       : QStringLiteral("/emoteonly");

        case ChatMode::UniqueChat:
            return off ? QStringLiteral("/uniquechatoff")
                       : QStringLiteral("/uniquechat");
    }

    return {};
}

QString chatModeLabel(ChatMode mode)
{
    switch (mode)
    {
        case ChatMode::Followers:
            return QCoreApplication::translate("ChatModes", "Followers only");
        case ChatMode::Slow:
            return QCoreApplication::translate("ChatModes", "Slow mode");
        case ChatMode::Subscribers:
            return QCoreApplication::translate("ChatModes", "Subscribers only");
        case ChatMode::EmoteOnly:
            return QCoreApplication::translate("ChatModes", "Emotes only");
        case ChatMode::UniqueChat:
            return QCoreApplication::translate("ChatModes", "Unique chat");
    }

    return {};
}

}  // namespace chatterino

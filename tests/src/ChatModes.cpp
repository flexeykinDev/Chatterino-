// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/twitch/ChatModes.hpp"

#include <gtest/gtest.h>

using namespace chatterino;

namespace {

/// Nothing switched on, which is how most chats sit.
std::vector<ChatModeRow> nothingOn()
{
    return chatModeRows(-1, 0, false, false, false);
}

const ChatModeRow &rowFor(const std::vector<ChatModeRow> &rows, ChatMode mode)
{
    for (const auto &row : rows)
    {
        if (row.mode == mode)
        {
            return row;
        }
    }

    ADD_FAILURE() << "no row for that mode";
    return rows.front();
}

}  // namespace

TEST(ChatModes, listsEveryMode)
{
    EXPECT_EQ(nothingOn().size(), 5u);
}

TEST(ChatModes, showsNothingActiveOnAnUnrestrictedChat)
{
    for (const auto &row : nothingOn())
    {
        EXPECT_FALSE(row.active) << "a mode was active on an open chat";
        EXPECT_TRUE(row.value.isEmpty());
    }
}

TEST(ChatModes, followerOnlyWithNoWaitIsStillOn)
{
    // Twitch encodes "anyone who follows" as zero minutes, and off as -1.
    // Collapsing the two shows a restricted chat as unrestricted, which is
    // the whole reason this is not a bool.
    auto rows = chatModeRows(0, 0, false, false, false);
    const auto &followers = rowFor(rows, ChatMode::Followers);

    EXPECT_TRUE(followers.active);
    // No number worth showing: there is no waiting period.
    EXPECT_TRUE(followers.value.isEmpty());
}

TEST(ChatModes, followerOnlyOffIsOff)
{
    EXPECT_FALSE(rowFor(nothingOn(), ChatMode::Followers).active);
}

TEST(ChatModes, showsHowLongSomebodyMustHaveFollowed)
{
    auto rows = chatModeRows(10, 0, false, false, false);
    const auto &followers = rowFor(rows, ChatMode::Followers);

    EXPECT_TRUE(followers.active);
    EXPECT_EQ(followers.value, "10m");
}

TEST(ChatModes, writesLongerPeriodsInLargerUnits)
{
    // A menu row has no space for "1440 minutes" beside a label.
    EXPECT_EQ(rowFor(chatModeRows(60, 0, false, false, false),
                     ChatMode::Followers)
                  .value,
              "1h");
    EXPECT_EQ(rowFor(chatModeRows(60 * 24, 0, false, false, false),
                     ChatMode::Followers)
                  .value,
              "1d");
    EXPECT_EQ(rowFor(chatModeRows(90, 0, false, false, false),
                     ChatMode::Followers)
                  .value,
              "90m");
}

TEST(ChatModes, showsTheSlowModeWait)
{
    auto rows = chatModeRows(-1, 30, false, false, false);
    const auto &slow = rowFor(rows, ChatMode::Slow);

    EXPECT_TRUE(slow.active);
    EXPECT_EQ(slow.value, "30s");
}

TEST(ChatModes, readsThePlainToggles)
{
    auto rows = chatModeRows(-1, 0, true, true, true);

    EXPECT_TRUE(rowFor(rows, ChatMode::Subscribers).active);
    EXPECT_TRUE(rowFor(rows, ChatMode::EmoteOnly).active);
    EXPECT_TRUE(rowFor(rows, ChatMode::UniqueChat).active);
}

// --- presets -----------------------------------------------------------

TEST(ChatModePresets, offersDurationsForTheModesThatTakeOne)
{
    EXPECT_FALSE(chatModePresets(ChatMode::Followers).empty());
    EXPECT_FALSE(chatModePresets(ChatMode::Slow).empty());
}

TEST(ChatModePresets, offersNoneForAPlainToggle)
{
    EXPECT_TRUE(chatModePresets(ChatMode::Subscribers).empty());
    EXPECT_TRUE(chatModePresets(ChatMode::EmoteOnly).empty());
    EXPECT_TRUE(chatModePresets(ChatMode::UniqueChat).empty());
}

TEST(ChatModePresets, everyPresetIsLabelled)
{
    for (auto mode : {ChatMode::Followers, ChatMode::Slow})
    {
        for (const auto &preset : chatModePresets(mode))
        {
            EXPECT_FALSE(preset.label.isEmpty());
        }
    }
}

TEST(ChatModePresets, offersAnyFollowerSeparatelyFromOff)
{
    auto presets = chatModePresets(ChatMode::Followers);

    bool anyFollower = false;
    bool off = false;
    for (const auto &preset : presets)
    {
        anyFollower = anyFollower || preset.amount == 0;
        off = off || preset.amount < 0;
    }

    // Both are real choices and they mean different things.
    EXPECT_TRUE(anyFollower);
    EXPECT_TRUE(off);
}

// --- commands ----------------------------------------------------------

TEST(ChatModeCommands, turnsModesOn)
{
    EXPECT_EQ(chatModeCommand(ChatMode::Followers, 10), "/followers 10");
    EXPECT_EQ(chatModeCommand(ChatMode::Followers, 0), "/followers 0");
    EXPECT_EQ(chatModeCommand(ChatMode::Slow, 30), "/slow 30");
    EXPECT_EQ(chatModeCommand(ChatMode::Subscribers, 0), "/subscribers");
    EXPECT_EQ(chatModeCommand(ChatMode::EmoteOnly, 0), "/emoteonly");
    EXPECT_EQ(chatModeCommand(ChatMode::UniqueChat, 0), "/uniquechat");
}

TEST(ChatModeCommands, turnsModesOff)
{
    EXPECT_EQ(chatModeCommand(ChatMode::Followers, -1), "/followersoff");
    EXPECT_EQ(chatModeCommand(ChatMode::Slow, -1), "/slowoff");
    EXPECT_EQ(chatModeCommand(ChatMode::Subscribers, -1), "/subscribersoff");
    EXPECT_EQ(chatModeCommand(ChatMode::EmoteOnly, -1), "/emoteonlyoff");
    EXPECT_EQ(chatModeCommand(ChatMode::UniqueChat, -1), "/uniquechatoff");
}

TEST(ChatModeCommands, zeroFollowerMinutesIsNotOff)
{
    // The one that would quietly do the opposite of what was clicked.
    EXPECT_NE(chatModeCommand(ChatMode::Followers, 0), "/followersoff");
}

TEST(ChatModeCommands, everyModeIsNamed)
{
    for (auto mode : {ChatMode::Followers, ChatMode::Slow,
                      ChatMode::Subscribers, ChatMode::EmoteOnly,
                      ChatMode::UniqueChat})
    {
        EXPECT_FALSE(chatModeLabel(mode).isEmpty());
    }
}

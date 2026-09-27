// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/companion/TypingTracker.hpp"

#include <gtest/gtest.h>

using namespace chatterino;

namespace {

constexpr qint64 expiry = TypingTracker::expiryMs;

}  // namespace

TEST(TypingTracker, startsWithNobodyTyping)
{
    TypingTracker tracker;

    EXPECT_TRUE(tracker.isEmpty());
    EXPECT_TRUE(tracker.typists("11", 0).isEmpty());
}

TEST(TypingTracker, showsSomebodyWhoStartedTyping)
{
    TypingTracker tracker;

    EXPECT_TRUE(tracker.set("11", "alice", true, 1000));
    EXPECT_EQ(tracker.typists("11", 1000), QStringList({"alice"}));
}

TEST(TypingTracker, repeatsChangeNothing)
{
    TypingTracker tracker;
    tracker.set("11", "alice", true, 1000);

    // Repeats are most of the traffic. Reporting each as a change would
    // redraw the line several times a second for no visible reason.
    EXPECT_FALSE(tracker.set("11", "alice", true, 2000));
    EXPECT_FALSE(tracker.set("11", "alice", true, 3000));
}

TEST(TypingTracker, aRepeatStillExtendsTheEntry)
{
    TypingTracker tracker;
    tracker.set("11", "alice", true, 0);
    tracker.set("11", "alice", true, expiry - 1);

    // Unchanged to look at, but the clock restarted, or a long message would
    // make the indicator vanish mid-sentence.
    EXPECT_EQ(tracker.typists("11", expiry + 1), QStringList({"alice"}));
}

TEST(TypingTracker, removesSomebodyWhoStopped)
{
    TypingTracker tracker;
    tracker.set("11", "alice", true, 1000);

    EXPECT_TRUE(tracker.set("11", "alice", false, 1500));
    EXPECT_TRUE(tracker.typists("11", 1500).isEmpty());
    EXPECT_TRUE(tracker.isEmpty());
}

TEST(TypingTracker, stoppingSomebodyWhoWasNotTypingChangesNothing)
{
    TypingTracker tracker;

    EXPECT_FALSE(tracker.set("11", "alice", false, 1000));
}

TEST(TypingTracker, forgetsSomebodyWhoWentQuietWithoutSayingSo)
{
    TypingTracker tracker;
    tracker.set("11", "alice", true, 0);

    // A client that crashes or is closed mid-sentence never sends a stop, and
    // an indicator that stays up forever is worse than one a second stale.
    EXPECT_TRUE(tracker.typists("11", expiry - 1).isEmpty() == false);
    EXPECT_TRUE(tracker.typists("11", expiry + 1).isEmpty());
}

TEST(TypingTracker, expiryIsLongerThanTheRepeatInterval)
{
    // Otherwise the line flickers between one repeat and the next whenever a
    // frame is slightly late.
    EXPECT_GT(TypingTracker::expiryMs, 4000);
}

TEST(TypingTracker, dropExpiredNamesOnlyTheChannelsThatChanged)
{
    TypingTracker tracker;
    tracker.set("11", "alice", true, 0);
    tracker.set("22", "bob", true, expiry);

    auto changed = tracker.dropExpired(expiry + 1);

    EXPECT_EQ(changed, QStringList({"11"}));
    EXPECT_EQ(tracker.typists("22", expiry + 1), QStringList({"bob"}));
}

TEST(TypingTracker, dropExpiredSaysNothingWhenNothingExpired)
{
    TypingTracker tracker;
    tracker.set("11", "alice", true, 1000);

    EXPECT_TRUE(tracker.dropExpired(1100).isEmpty());
}

TEST(TypingTracker, keepsChannelsApart)
{
    TypingTracker tracker;
    tracker.set("11", "alice", true, 1000);
    tracker.set("22", "bob", true, 1000);

    EXPECT_EQ(tracker.typists("11", 1000), QStringList({"alice"}));
    EXPECT_EQ(tracker.typists("22", 1000), QStringList({"bob"}));
}

TEST(TypingTracker, ordersNamesSoTheLineDoesNotReshuffle)
{
    TypingTracker tracker;
    tracker.set("11", "carol", true, 1000);
    tracker.set("11", "alice", true, 1000);
    tracker.set("11", "bob", true, 1000);

    // Repeats arrive in whatever order the network delivers them. Sorting is
    // what keeps the line from rearranging itself while being read.
    EXPECT_EQ(tracker.typists("11", 1000),
              QStringList({"alice", "bob", "carol"}));
}

TEST(TypingTracker, forgettingAChannelLeavesOthersAlone)
{
    TypingTracker tracker;
    tracker.set("11", "alice", true, 1000);
    tracker.set("22", "bob", true, 1000);

    tracker.forget("11");

    EXPECT_TRUE(tracker.typists("11", 1000).isEmpty());
    EXPECT_EQ(tracker.typists("22", 1000), QStringList({"bob"}));
}

TEST(TypingTracker, clearForgetsEverything)
{
    TypingTracker tracker;
    tracker.set("11", "alice", true, 1000);
    tracker.set("22", "bob", true, 1000);

    // The connection dropped; nothing learned before it dropped still holds.
    tracker.clear();

    EXPECT_TRUE(tracker.isEmpty());
}

TEST(TypingTracker, ignoresFramesWithNothingToIdentify)
{
    TypingTracker tracker;

    EXPECT_FALSE(tracker.set("", "alice", true, 0));
    EXPECT_FALSE(tracker.set("11", "", true, 0));
    EXPECT_TRUE(tracker.isEmpty());
}

// --- wording -----------------------------------------------------------

TEST(TypingTrackerWording, saysNothingForNobody)
{
    EXPECT_TRUE(TypingTracker::describe({}).isEmpty());
}

TEST(TypingTrackerWording, namesOneAndTwoPeople)
{
    EXPECT_TRUE(TypingTracker::describe({"alice"}).contains("alice"));

    auto two = TypingTracker::describe({"alice", "bob"});
    EXPECT_TRUE(two.contains("alice"));
    EXPECT_TRUE(two.contains("bob"));
}

TEST(TypingTrackerWording, countsRatherThanNamingACrowd)
{
    auto many =
        TypingTracker::describe({"alice", "bob", "carol", "dave", "erin"});

    // The line sits beside the message box. Five names is a wall of text that
    // nobody reads and that pushes everything else off the row.
    EXPECT_FALSE(many.contains("alice"));
    EXPECT_TRUE(many.contains("5"));
}

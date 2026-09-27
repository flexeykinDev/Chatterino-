// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/companion/ShadowBacklog.hpp"

#include <gtest/gtest.h>

using namespace chatterino;

TEST(ShadowBacklog, everythingIsNewAtFirst)
{
    ShadowBacklog backlog;

    EXPECT_TRUE(backlog.isNew(1));
    EXPECT_TRUE(backlog.isNew(9999));
}

TEST(ShadowBacklog, doesNotShowTheSameBacklogTwice)
{
    ShadowBacklog backlog;
    backlog.seen(1);
    backlog.seen(2);
    backlog.seen(3);

    // A room is rejoined on every reconnect and backfilled each time. Without
    // this the whole conversation reappears underneath itself.
    EXPECT_FALSE(backlog.isNew(1));
    EXPECT_FALSE(backlog.isNew(3));
    EXPECT_TRUE(backlog.isNew(4));
}

TEST(ShadowBacklog, treatsAnUnidentifiedMessageAsNew)
{
    ShadowBacklog backlog;
    backlog.seen(10);

    // Only the server assigns ids, so no id means a local echo of something
    // just typed, which cannot have been seen before.
    EXPECT_TRUE(backlog.isNew(0));
}

TEST(ShadowBacklog, aLocalEchoDoesNotAdvanceWhatHasBeenSeen)
{
    ShadowBacklog backlog;
    backlog.seen(10);
    backlog.seen(0);

    EXPECT_EQ(backlog.highestSeen(), 10);
}

TEST(ShadowBacklog, ownMessagesCountOnceAcknowledged)
{
    ShadowBacklog backlog;

    // Shown at once with no id, then the acknowledgement supplies one. Without
    // recording it, a reconnect brings your own messages back as history this
    // has never seen.
    EXPECT_TRUE(backlog.isNew(0));
    backlog.seen(7);

    EXPECT_FALSE(backlog.isNew(7));
    EXPECT_FALSE(backlog.isNew(5));
}

TEST(ShadowBacklog, outOfOrderIdsDoNotRewindIt)
{
    ShadowBacklog backlog;
    backlog.seen(10);
    backlog.seen(4);

    EXPECT_EQ(backlog.highestSeen(), 10);
    EXPECT_FALSE(backlog.isNew(9));
}

TEST(ShadowBacklog, ignoresNonsenseIds)
{
    ShadowBacklog backlog;
    backlog.seen(-5);

    EXPECT_EQ(backlog.highestSeen(), 0);
    EXPECT_TRUE(backlog.isNew(1));
}

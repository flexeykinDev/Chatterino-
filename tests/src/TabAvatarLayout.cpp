// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/helper/TabAvatarLayout.hpp"

#include <gtest/gtest.h>
#include <QRectF>

using namespace chatterino;

namespace {

/// A tab at the default height, which is the shape this normally gets.
const QRectF TAB{0, 0, 30, 30};

}  // namespace

TEST(TabAvatarLayout, AvatarIsCircular)
{
    auto layout = computeTabAvatarLayout(TAB, 1);

    EXPECT_DOUBLE_EQ(layout.avatar.width(), layout.avatar.height());
}

TEST(TabAvatarLayout, AvatarIsCentredInTheTab)
{
    auto layout = computeTabAvatarLayout(QRectF{10, 20, 30, 30}, 1);

    EXPECT_DOUBLE_EQ(layout.avatar.center().x(), 25);
    EXPECT_DOUBLE_EQ(layout.avatar.center().y(), 35);
}

TEST(TabAvatarLayout, AvatarLeavesRoomAroundItself)
{
    auto layout = computeTabAvatarLayout(TAB, 1);

    // Circles that touched would run into each other along a row of tabs.
    EXPECT_LT(layout.avatar.width(), TAB.width());
    EXPECT_GT(layout.avatar.width(), 0);
}

TEST(TabAvatarLayout, RingSurroundsTheAvatar)
{
    auto layout = computeTabAvatarLayout(TAB, 1);

    EXPECT_LT(layout.ring.left(), layout.avatar.left());
    EXPECT_GT(layout.ring.right(), layout.avatar.right());
    EXPECT_DOUBLE_EQ(layout.ring.center().x(), layout.avatar.center().x());
    EXPECT_DOUBLE_EQ(layout.ring.center().y(), layout.avatar.center().y());
}

TEST(TabAvatarLayout, RingStaysInsideTheTab)
{
    auto layout = computeTabAvatarLayout(TAB, 1);

    // Half the stroke falls outside the circle it is drawn on, so the circle
    // has to be pulled in far enough that the stroke is not clipped.
    auto outer = layout.ring.adjusted(-layout.ringWidth / 2,
                                      -layout.ringWidth / 2,
                                      layout.ringWidth / 2,
                                      layout.ringWidth / 2);

    EXPECT_GE(outer.left(), TAB.left());
    EXPECT_GE(outer.top(), TAB.top());
    EXPECT_LE(outer.right(), TAB.right());
    EXPECT_LE(outer.bottom(), TAB.bottom());
}

TEST(TabAvatarLayout, RingThickensWithTheInterfaceScale)
{
    auto normal = computeTabAvatarLayout(TAB, 1);
    auto doubled = computeTabAvatarLayout(TAB, 2);

    EXPECT_DOUBLE_EQ(doubled.ringWidth, normal.ringWidth * 2);
}

TEST(TabAvatarLayout, AvatarSizeFollowsTheTabNotTheScale)
{
    // The tab's own size already accounts for zoom, so scaling the avatar by it
    // again would compound.
    auto normal = computeTabAvatarLayout(TAB, 1);
    auto doubled = computeTabAvatarLayout(TAB, 2);

    EXPECT_DOUBLE_EQ(normal.avatar.width(), doubled.avatar.width());
}

TEST(TabAvatarLayout, AvatarGrowsWithTheTab)
{
    auto small = computeTabAvatarLayout(QRectF{0, 0, 30, 30}, 1);
    auto large = computeTabAvatarLayout(QRectF{0, 0, 60, 60}, 1);

    EXPECT_DOUBLE_EQ(large.avatar.width(), small.avatar.width() * 2);
}

TEST(TabAvatarLayout, BadgeSitsOnTheUpperTrailingCorner)
{
    auto layout = computeTabAvatarLayout(TAB, 1);

    EXPECT_GT(layout.badge.center().x(), layout.avatar.center().x());
    EXPECT_LT(layout.badge.center().y(), layout.avatar.center().y());
}

TEST(TabAvatarLayout, BadgeOverlapsTheAvatar)
{
    auto layout = computeTabAvatarLayout(TAB, 1);

    // Attached to the avatar rather than floating beside it.
    EXPECT_TRUE(layout.badge.intersects(layout.avatar));
}

TEST(TabAvatarLayout, BadgeNeverSwallowsTheAvatar)
{
    // At a large interface scale on a small tab, a fixed badge size would cover
    // the whole avatar.
    auto layout = computeTabAvatarLayout(QRectF{0, 0, 16, 16}, 4);

    EXPECT_LE(layout.badge.width(), layout.avatar.width() / 2);
}

TEST(TabAvatarLayout, UsesTheShorterSideOfARectangularTab)
{
    auto layout = computeTabAvatarLayout(QRectF{0, 0, 100, 30}, 1);

    EXPECT_LT(layout.avatar.width(), 30);
    EXPECT_DOUBLE_EQ(layout.avatar.center().x(), 50);
}

TEST(TabAvatarLayout, SurvivesADegenerateScale)
{
    auto layout = computeTabAvatarLayout(TAB, 0);

    EXPECT_GT(layout.avatar.width(), 0);
    EXPECT_GT(layout.ringWidth, 0);
}

TEST(TabAvatarLayout, SurvivesATinyTab)
{
    auto layout = computeTabAvatarLayout(QRectF{0, 0, 1, 1}, 1);

    EXPECT_GT(layout.avatar.width(), 0);
    EXPECT_GT(layout.badge.width(), 0);
}

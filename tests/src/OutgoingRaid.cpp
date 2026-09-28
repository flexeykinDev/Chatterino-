// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/twitch/OutgoingRaid.hpp"

#include "common/Literals.hpp"
#include "Test.hpp"

using namespace chatterino;
using namespace literals;

namespace {

const QDateTime NOW = QDateTime::fromSecsSinceEpoch(1700000000);

OutgoingRaid raidStartedAt(const QDateTime &when)
{
    return {
        .targetLogin = u"somestreamer"_s,
        .targetDisplayName = u"SomeStreamer"_s,
        .startedAt = when,
    };
}

}  // namespace

TEST(OutgoingRaid, countsDownFromTheFullWait)
{
    EXPECT_EQ(raidTimeLeft(raidStartedAt(NOW), NOW), raidCountdown);
}

TEST(OutgoingRaid, countsDownAsTimePasses)
{
    auto raid = raidStartedAt(NOW);

    EXPECT_EQ(raidTimeLeft(raid, NOW.addSecs(30)), std::chrono::seconds{60});
    EXPECT_EQ(raidTimeLeft(raid, NOW.addSecs(89)), std::chrono::seconds{1});
}

TEST(OutgoingRaid, stopsAtZeroRatherThanGoingNegative)
{
    auto raid = raidStartedAt(NOW);

    EXPECT_EQ(raidTimeLeft(raid, NOW.addSecs(90)), std::chrono::seconds{0});
    EXPECT_EQ(raidTimeLeft(raid, NOW.addSecs(10000)), std::chrono::seconds{0});
}

TEST(OutgoingRaid, refusesToCountDownFromMoreThanARaidCanHave)
{
    // A clock behind the one that stamped the raid would otherwise show a
    // countdown longer than ninety seconds, ticking upwards as they converge.
    auto raid = raidStartedAt(NOW.addSecs(500));

    EXPECT_EQ(raidTimeLeft(raid, NOW), raidCountdown);
}

TEST(OutgoingRaid, hasGoneOnceTheWaitIsUp)
{
    auto raid = raidStartedAt(NOW);

    EXPECT_FALSE(raidHasGone(raid, NOW));
    EXPECT_FALSE(raidHasGone(raid, NOW.addSecs(89)));
    EXPECT_TRUE(raidHasGone(raid, NOW.addSecs(90)));
}

TEST(OutgoingRaid, treatsAMalformedRaidAsGoneRatherThanShowingItForever)
{
    OutgoingRaid noTarget{.startedAt = NOW};
    EXPECT_TRUE(raidHasGone(noTarget, NOW));

    auto noTime = raidStartedAt({});
    EXPECT_TRUE(raidHasGone(noTime, NOW));
    EXPECT_EQ(raidTimeLeft(noTime, NOW), std::chrono::seconds{0});
}

TEST(OutgoingRaid, formatsTheCountdownAsMinutesAndSeconds)
{
    EXPECT_EQ(formatRaidCountdown(std::chrono::seconds{90}), u"1:30"_s);
    EXPECT_EQ(formatRaidCountdown(std::chrono::seconds{61}), u"1:01"_s);
    // The padding matters: "1:5" reads as five, not five seconds.
    EXPECT_EQ(formatRaidCountdown(std::chrono::seconds{65}), u"1:05"_s);
    EXPECT_EQ(formatRaidCountdown(std::chrono::seconds{9}), u"0:09"_s);
    EXPECT_EQ(formatRaidCountdown(std::chrono::seconds{0}), u"0:00"_s);
    EXPECT_EQ(formatRaidCountdown(std::chrono::seconds{-5}), u"0:00"_s);
}

TEST(OutgoingRaid, saysWhoIsBeingRaidedAndWhen)
{
    auto raid = raidStartedAt(NOW);

    auto text = raidBannerText(raid, NOW.addSecs(30));
    EXPECT_TRUE(text.contains(u"SomeStreamer"_s)) << text;
    EXPECT_TRUE(text.contains(u"1:00"_s)) << text;
}

TEST(OutgoingRaid, saysNowRatherThanZeroWhenTheWaitIsUp)
{
    // "Raiding SomeStreamer in 0:00" is a countdown that has stopped working.
    auto text = raidBannerText(raidStartedAt(NOW), NOW.addSecs(90));

    EXPECT_TRUE(text.contains(u"SomeStreamer"_s)) << text;
    EXPECT_FALSE(text.contains(u"0:00"_s)) << text;
}

TEST(OutgoingRaid, fallsBackToTheLoginWhenThereIsNoDisplayName)
{
    OutgoingRaid raid{
        .targetLogin = u"somestreamer"_s,
        .startedAt = NOW,
    };

    EXPECT_EQ(raid.name(), u"somestreamer"_s);
    EXPECT_TRUE(raidBannerText(raid, NOW).contains(u"somestreamer"_s));
}

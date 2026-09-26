// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/companion/GlobalBanRegistry.hpp"

#include "providers/companion/GlobalBan.hpp"
#include "providers/companion/GlobalBanMarker.hpp"

#include <gtest/gtest.h>
#include <QJsonDocument>
#include <QJsonObject>

using namespace chatterino;

namespace {

QJsonObject json(const char *text)
{
    QJsonParseError error{};
    auto document = QJsonDocument::fromJson(QByteArray(text), &error);
    EXPECT_EQ(error.error, QJsonParseError::NoError)
        << error.errorString().toStdString();
    return document.object();
}

}  // namespace

// --- parsing -----------------------------------------------------------

TEST(GlobalBanMarkers, readsCounts)
{
    auto markers =
        parseGlobalBanMarkers(json(R"({"markers": {"11": 2, "22": 1}})"));

    EXPECT_EQ(markers.value("11"), 2);
    EXPECT_EQ(markers.value("22"), 1);
    EXPECT_EQ(markers.size(), 2);
}

TEST(GlobalBanMarkers, dropsNonPositiveAndMalformedEntries)
{
    // A zero or a string must not become a marker. Absence is what carries
    // "no marker"; the registry turns that into a known zero itself.
    auto markers = parseGlobalBanMarkers(
        json(R"({"markers": {"11": 0, "22": -3, "33": "two", "44": 1}})"));

    EXPECT_EQ(markers.size(), 1);
    EXPECT_EQ(markers.value("44"), 1);
}

TEST(GlobalBanMarkers, missingMarkersObjectIsEmptyNotACrash)
{
    EXPECT_TRUE(parseGlobalBanMarkers(json("{}")).isEmpty());
    EXPECT_TRUE(parseGlobalBanMarkers(json(R"({"markers": 7})")).isEmpty());
}

TEST(GlobalBanRecordParsing, readsAFullRecord)
{
    auto record = GlobalBanRecord::fromJson(json(R"({
        "id": 4,
        "offenderId": "99",
        "channelId": "11",
        "channelLogin": "forsen",
        "reason": "spam",
        "bannedAt": 1700000000000,
        "liftedAt": null,
        "clearedAt": null,
        "clearedBy": null,
        "context": [
            {"body": "first", "sentAt": 1699999999000},
            {"body": "second", "sentAt": 1699999999500}
        ]
    })"));

    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->channelId, "11");
    EXPECT_EQ(record->channelLogin, "forsen");
    EXPECT_EQ(record->reason, "spam");
    EXPECT_EQ(record->bannedAt.toMSecsSinceEpoch(), 1700000000000);
    EXPECT_FALSE(record->isLifted());
    EXPECT_FALSE(record->isCleared());
    ASSERT_EQ(record->context.size(), 2u);
    EXPECT_EQ(record->context[0].body, "first");
    EXPECT_EQ(record->context[1].body, "second");
}

TEST(GlobalBanRecordParsing, liftedAndClearedAreDistinct)
{
    auto lifted = GlobalBanRecord::fromJson(json(R"({
        "channelId": "11", "bannedAt": 1700000000000,
        "liftedAt": 1700000500000, "clearedAt": null
    })"));
    auto cleared = GlobalBanRecord::fromJson(json(R"({
        "channelId": "11", "bannedAt": 1700000000000,
        "liftedAt": null, "clearedAt": 1700000500000, "clearedBy": "12"
    })"));

    ASSERT_TRUE(lifted.has_value());
    ASSERT_TRUE(cleared.has_value());
    EXPECT_TRUE(lifted->isLifted());
    EXPECT_FALSE(lifted->isCleared());
    EXPECT_FALSE(cleared->isLifted());
    EXPECT_TRUE(cleared->isCleared());
    EXPECT_EQ(cleared->clearedBy, "12");
}

TEST(GlobalBanRecordParsing, fallsBackToTheChannelIdWhenNoLoginIsGiven)
{
    auto record = GlobalBanRecord::fromJson(
        json(R"({"channelId": "11", "bannedAt": 1700000000000})"));

    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->channelLogin, "11");
}

TEST(GlobalBanRecordParsing, rejectsRecordsWithoutAChannelOrATime)
{
    EXPECT_FALSE(
        GlobalBanRecord::fromJson(json(R"({"bannedAt": 1700000000000})")));
    EXPECT_FALSE(GlobalBanRecord::fromJson(json(R"({"channelId": "11"})")));
    EXPECT_FALSE(GlobalBanRecord::fromJson(
        json(R"({"channelId": "11", "bannedAt": 0})")));
}

TEST(GlobalBanSummaryParsing, skipsUnusableHistoryEntriesButKeepsTheRest)
{
    auto summary = GlobalBanSummary::fromJson(json(R"({
        "offenderId": "99",
        "markerCount": 2,
        "history": [
            {"channelId": "11", "bannedAt": 1700000000000},
            {"channelId": "", "bannedAt": 1700000000000},
            "nonsense",
            {"channelId": "22", "bannedAt": 1690000000000}
        ],
        "activeChannels": ["11", "", "22"]
    })"));

    ASSERT_TRUE(summary.has_value());
    EXPECT_EQ(summary->offenderId, "99");
    EXPECT_EQ(summary->markerCount, 2);
    ASSERT_EQ(summary->history.size(), 2u);
    EXPECT_EQ(summary->history[0].channelId, "11");
    EXPECT_EQ(summary->history[1].channelId, "22");
    ASSERT_EQ(summary->activeChannels.size(), 2u);
}

TEST(GlobalBanSummaryParsing, rejectsAResponseWithNoOffender)
{
    EXPECT_FALSE(GlobalBanSummary::fromJson(json(R"({"markerCount": 1})")));
}

// --- registry ----------------------------------------------------------

TEST(GlobalBanRegistry, anUnaskedChatterIsUnknownRatherThanUnmarked)
{
    GlobalBanRegistry registry;

    // Unknown must not read as zero, or a marker would pop in late on every
    // join instead of simply not being drawn yet.
    EXPECT_FALSE(registry.markerCount("11", "99").has_value());
}

TEST(GlobalBanRegistry, notingAChatterQueuesThemExactlyOnce)
{
    GlobalBanRegistry registry;

    EXPECT_TRUE(registry.note("11", "99"));
    EXPECT_FALSE(registry.note("11", "99"));
    EXPECT_EQ(registry.pendingCount(), 1);
}

TEST(GlobalBanRegistry, theSameChatterIsAskedAboutOncePerChannel)
{
    GlobalBanRegistry registry;

    // The answer is "banned elsewhere", so it differs per channel and cannot
    // be shared between tabs.
    EXPECT_TRUE(registry.note("11", "99"));
    EXPECT_TRUE(registry.note("22", "99"));
    EXPECT_EQ(registry.pendingCount(), 2);
}

TEST(GlobalBanRegistry, ignoresEmptyIds)
{
    GlobalBanRegistry registry;

    EXPECT_FALSE(registry.note("", "99"));
    EXPECT_FALSE(registry.note("11", ""));
    EXPECT_EQ(registry.pendingCount(), 0);
}

TEST(GlobalBanRegistry, batchesInTheOrderChattersWereFirstSeen)
{
    GlobalBanRegistry registry;
    registry.note("11", "a");
    registry.note("11", "b");
    registry.note("11", "c");

    auto batch = registry.takeBatch(2);
    ASSERT_TRUE(batch.has_value());
    EXPECT_EQ(batch->channelId, "11");
    EXPECT_EQ(batch->userIds, QStringList({"a", "b"}));

    auto rest = registry.takeBatch(2);
    ASSERT_TRUE(rest.has_value());
    EXPECT_EQ(rest->userIds, QStringList({"c"}));

    EXPECT_FALSE(registry.takeBatch(2).has_value());
}

TEST(GlobalBanRegistry, aBatchInFlightIsNotAskedAboutAgain)
{
    GlobalBanRegistry registry;
    registry.note("11", "a");

    auto batch = registry.takeBatch();
    ASSERT_TRUE(batch.has_value());

    EXPECT_FALSE(registry.note("11", "a"));
    EXPECT_FALSE(registry.takeBatch().has_value());
}

TEST(GlobalBanRegistry, answersMakeChattersKnownIncludingTheUnmarkedOnes)
{
    GlobalBanRegistry registry;
    registry.note("11", "a");
    registry.note("11", "b");

    auto batch = registry.takeBatch();
    ASSERT_TRUE(batch.has_value());

    EXPECT_TRUE(registry.applyMarkers("11", batch->userIds, {{"a", 3}}));

    EXPECT_EQ(registry.markerCount("11", "a"), 3);
    // Asked about and not marked: known to be zero, so we stop asking.
    EXPECT_EQ(registry.markerCount("11", "b"), 0);
    EXPECT_FALSE(registry.note("11", "b"));
}

TEST(GlobalBanRegistry, repeatingAnAnswerReportsNoChange)
{
    GlobalBanRegistry registry;
    registry.note("11", "a");
    auto batch = registry.takeBatch();
    ASSERT_TRUE(batch.has_value());
    registry.applyMarkers("11", batch->userIds, {{"a", 2}});

    // The service saying the same thing again warrants no relayout.
    EXPECT_FALSE(registry.applyMarkers("11", {}, {{"a", 2}}));
    EXPECT_TRUE(registry.applyMarkers("11", {}, {{"a", 3}}));
}

TEST(GlobalBanRegistry, aBatchWithNobodyMarkedWarrantsNoRelayout)
{
    GlobalBanRegistry registry;
    registry.note("11", "a");
    registry.note("11", "b");
    auto batch = registry.takeBatch();
    ASSERT_TRUE(batch.has_value());

    // Unknown and zero look the same on screen — nothing — so the common case
    // of a chat where nobody has been banned elsewhere costs no relayout.
    EXPECT_FALSE(registry.applyMarkers("11", batch->userIds, {}));
    EXPECT_EQ(registry.markerCount("11", "a"), 0);
}

TEST(GlobalBanRegistry, aMarkerAppearingWarrantsARelayout)
{
    GlobalBanRegistry registry;
    registry.note("11", "a");
    auto batch = registry.takeBatch();
    ASSERT_TRUE(batch.has_value());

    EXPECT_TRUE(registry.applyMarkers("11", batch->userIds, {{"a", 1}}));
}

TEST(GlobalBanRegistry, aMarkerDisappearingWarrantsARelayout)
{
    GlobalBanRegistry registry;
    registry.note("11", "a");
    auto first = registry.takeBatch();
    ASSERT_TRUE(first.has_value());
    registry.applyMarkers("11", first->userIds, {{"a", 1}});

    // Vouched for since: the tag has to come off the messages already drawn.
    registry.refresh("11", "a");
    auto second = registry.takeBatch();
    ASSERT_TRUE(second.has_value());

    EXPECT_TRUE(registry.applyMarkers("11", second->userIds, {}));
}

TEST(GlobalBanRegistry, acceptsAMarkerForSomeoneTheBatchDidNotAskAbout)
{
    GlobalBanRegistry registry;
    registry.note("11", "a");
    auto batch = registry.takeBatch();
    ASSERT_TRUE(batch.has_value());

    EXPECT_TRUE(
        registry.applyMarkers("11", batch->userIds, {{"a", 1}, {"z", 4}}));
    EXPECT_EQ(registry.markerCount("11", "z"), 4);
}

TEST(GlobalBanRegistry, answersAreScopedToTheChannelThatAsked)
{
    GlobalBanRegistry registry;
    registry.note("11", "a");
    registry.note("22", "a");

    auto first = registry.takeBatch();
    ASSERT_TRUE(first.has_value());
    registry.applyMarkers(first->channelId, first->userIds, {{"a", 1}});

    auto second = registry.takeBatch();
    ASSERT_TRUE(second.has_value());
    EXPECT_NE(second->channelId, first->channelId);
    EXPECT_FALSE(registry.markerCount(second->channelId, "a").has_value());
}

TEST(GlobalBanRegistry, aFailedBatchIsQueuedAgain)
{
    GlobalBanRegistry registry;
    registry.note("11", "a");
    auto batch = registry.takeBatch();
    ASSERT_TRUE(batch.has_value());
    EXPECT_EQ(registry.pendingCount(), 0);

    registry.failBatch("11", batch->userIds);

    EXPECT_EQ(registry.pendingCount(), 1);
    EXPECT_FALSE(registry.markerCount("11", "a").has_value());
    auto retry = registry.takeBatch();
    ASSERT_TRUE(retry.has_value());
    EXPECT_EQ(retry->userIds, QStringList({"a"}));
}

TEST(GlobalBanRegistry, aFailedBatchDoesNotDuplicateAnAnsweredChatter)
{
    GlobalBanRegistry registry;
    registry.note("11", "a");
    auto batch = registry.takeBatch();
    ASSERT_TRUE(batch.has_value());
    registry.applyMarkers("11", batch->userIds, {{"a", 1}});

    // A late failure for a batch that was in fact answered must not undo it.
    registry.failBatch("11", batch->userIds);

    EXPECT_EQ(registry.pendingCount(), 0);
    EXPECT_EQ(registry.markerCount("11", "a"), 1);
}

TEST(GlobalBanRegistry, refreshingAChatterKeepsWhatIsDrawnAndAsksAgain)
{
    GlobalBanRegistry registry;
    registry.note("11", "a");
    auto batch = registry.takeBatch();
    ASSERT_TRUE(batch.has_value());
    registry.applyMarkers("11", batch->userIds, {{"a", 2}});

    registry.refresh("11", "a");

    // The tag stays put while the question is out, rather than blinking off.
    EXPECT_EQ(registry.markerCount("11", "a"), 2);
    EXPECT_EQ(registry.pendingCount(), 1);
}

TEST(GlobalBanRegistry, refreshingTwiceQueuesOneQuestion)
{
    GlobalBanRegistry registry;
    registry.refresh("11", "a");
    registry.refresh("11", "a");

    EXPECT_EQ(registry.pendingCount(), 1);
}

TEST(GlobalBanRegistry, aRefreshOutlivesAnOlderRequest)
{
    GlobalBanRegistry registry;
    registry.note("11", "a");
    auto first = registry.takeBatch();
    ASSERT_TRUE(first.has_value());

    registry.refresh("11", "a");

    // The older request answering must not mark the chatter settled and leave
    // the refresh unasked.
    registry.applyMarkers("11", first->userIds, {{"a", 1}});

    EXPECT_EQ(registry.pendingCount(), 1);
}

TEST(GlobalBanRegistry, forgettingAChannelDropsItsAnswersOnly)
{
    GlobalBanRegistry registry;
    registry.note("11", "a");
    registry.note("22", "a");
    while (auto batch = registry.takeBatch())
    {
        registry.applyMarkers(batch->channelId, batch->userIds, {{"a", 1}});
    }

    registry.forgetChannel("11");

    EXPECT_FALSE(registry.markerCount("11", "a").has_value());
    EXPECT_EQ(registry.markerCount("22", "a"), 1);
}

TEST(GlobalBanRegistry, batchesAreCappedAtWhatTheRouteAccepts)
{
    GlobalBanRegistry registry;
    for (int i = 0; i < GlobalBanRegistry::maxBatchSize + 5; ++i)
    {
        registry.note("11", QString::number(i));
    }

    auto batch = registry.takeBatch();
    ASSERT_TRUE(batch.has_value());
    EXPECT_EQ(batch->userIds.size(), GlobalBanRegistry::maxBatchSize);
    EXPECT_EQ(registry.pendingCount(), 5);
}

// --- marker geometry ---------------------------------------------------

TEST(GlobalBanMarkerMetrics, isNeverNarrowerThanItIsTall)
{
    // One digit must still read as a tag rather than a sliver.
    auto size = GlobalBanMarkerMetrics::size(20, 1);

    EXPECT_DOUBLE_EQ(size.height(), 20 * GlobalBanMarkerMetrics::heightRatio);
    EXPECT_DOUBLE_EQ(size.width(), size.height());
}

TEST(GlobalBanMarkerMetrics, growsWithTheDigitsItHasToHold)
{
    auto narrow = GlobalBanMarkerMetrics::size(20, 8);
    auto wide = GlobalBanMarkerMetrics::size(20, 30);

    EXPECT_GT(wide.width(), narrow.width());
    EXPECT_DOUBLE_EQ(wide.height(), narrow.height());

    auto padding = wide.height() * GlobalBanMarkerMetrics::horizontalPaddingRatio;
    EXPECT_DOUBLE_EQ(wide.width(), 30 + padding * 2);
}

TEST(GlobalBanMarkerMetrics, scalesWithTheLineItSitsOn)
{
    auto small = GlobalBanMarkerMetrics::size(20, 10);
    auto large = GlobalBanMarkerMetrics::size(40, 10);

    EXPECT_DOUBLE_EQ(large.height(), small.height() * 2);
}

TEST(GlobalBanMarkerMetrics, refusesNegativeInputsRatherThanInvertingTheShape)
{
    auto size = GlobalBanMarkerMetrics::size(-5, -5);

    EXPECT_DOUBLE_EQ(size.width(), 0);
    EXPECT_DOUBLE_EQ(size.height(), 0);
}

TEST(GlobalBanMarkerMetrics, textSitsCentredInsideTheTag)
{
    QRectF bounds(10, 4, 40, 20);
    auto text = GlobalBanMarkerMetrics::textRect(bounds);

    EXPECT_DOUBLE_EQ(text.center().x(), bounds.center().x());
    EXPECT_DOUBLE_EQ(text.top(), bounds.top());
    EXPECT_DOUBLE_EQ(text.height(), bounds.height());
    EXPECT_LT(text.width(), bounds.width());
}

TEST(GlobalBanMarkerMetrics, paddingNeverEatsTheWholeTag)
{
    // A tiny line height would otherwise give a negative width, which Qt draws
    // as nothing at all.
    QRectF bounds(0, 0, 2, 20);

    EXPECT_EQ(GlobalBanMarkerMetrics::textRect(bounds), bounds);
}

TEST(GlobalBanMarkerMetrics, cornerRadiusFollowsTheHeight)
{
    EXPECT_DOUBLE_EQ(GlobalBanMarkerMetrics::cornerRadius(20),
                     20 * GlobalBanMarkerMetrics::cornerRadiusRatio);
    EXPECT_DOUBLE_EQ(GlobalBanMarkerMetrics::cornerRadius(-1), 0);
}

TEST(GlobalBanRegistry, startsEmpty)
{
    GlobalBanRegistry registry;

    EXPECT_TRUE(registry.isEmpty());

    registry.note("11", "a");
    EXPECT_FALSE(registry.isEmpty());

    registry.clear();
    EXPECT_TRUE(registry.isEmpty());
}

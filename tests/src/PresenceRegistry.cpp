// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/companion/PresenceRegistry.hpp"

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

constexpr qint64 fresh = PresenceRegistry::freshForMs;

}  // namespace

// --- parsing -----------------------------------------------------------

TEST(PresenceParsing, readsTheThreeStates)
{
    auto states = parsePresenceStates(
        json(R"({"presence":{"1":"online","2":"offline","3":"unknown"}})"));

    EXPECT_EQ(states.value("1"), PresenceState::Online);
    EXPECT_EQ(states.value("2"), PresenceState::Offline);
    EXPECT_EQ(states.value("3"), PresenceState::Unknown);
}

TEST(PresenceParsing, dropsAStateThisBuildDoesNotKnow)
{
    // Guessing would put a wrong dot beside somebody's name, which is worse
    // than no dot.
    auto states =
        parsePresenceStates(json(R"({"presence":{"1":"away","2":"online"}})"));

    EXPECT_EQ(states.size(), 1);
    EXPECT_EQ(states.value("2"), PresenceState::Online);
}

TEST(PresenceParsing, survivesRubbish)
{
    EXPECT_TRUE(parsePresenceStates(json("{}")).isEmpty());
    EXPECT_TRUE(parsePresenceStates(json(R"({"presence":7})")).isEmpty());
    EXPECT_TRUE(
        parsePresenceStates(json(R"({"presence":{"1":5}})")).isEmpty());
}

// --- registry ----------------------------------------------------------

TEST(PresenceRegistry, anUnaskedChatterIsUnknownRatherThanOffline)
{
    PresenceRegistry registry;

    EXPECT_FALSE(registry.state("1", 0).has_value());
}

TEST(PresenceRegistry, queuesSomebodyOnce)
{
    PresenceRegistry registry;

    EXPECT_TRUE(registry.note("1", 0));
    EXPECT_FALSE(registry.note("1", 0));
    EXPECT_EQ(registry.pendingCount(), 1);
}

TEST(PresenceRegistry, answersMakeChattersKnown)
{
    PresenceRegistry registry;
    registry.note("1", 0);
    registry.note("2", 0);
    auto batch = registry.takeBatch();

    EXPECT_TRUE(registry.applyStates(batch, {{"1", PresenceState::Online}}, 0));

    EXPECT_EQ(registry.state("1", 0), PresenceState::Online);
    // Asked about and not mentioned: the service means "never seen".
    EXPECT_EQ(registry.state("2", 0), PresenceState::Unknown);
}

TEST(PresenceRegistry, aBatchOfStrangersWarrantsNoRelayout)
{
    PresenceRegistry registry;
    registry.note("1", 0);
    registry.note("2", 0);
    auto batch = registry.takeBatch();

    // Nearly every chatter has never run this client, so the common answer is
    // "nobody" and it must not cost a relayout.
    EXPECT_FALSE(registry.applyStates(batch, {}, 0));
}

TEST(PresenceRegistry, aStateChangeWarrantsARelayout)
{
    PresenceRegistry registry;
    registry.note("1", 0);
    registry.applyStates(registry.takeBatch(), {{"1", PresenceState::Online}},
                         0);

    registry.note("1", fresh + 1);
    EXPECT_TRUE(registry.applyStates(registry.takeBatch(),
                                     {{"1", PresenceState::Offline}},
                                     fresh + 1));
}

TEST(PresenceRegistry, repeatingAnAnswerWarrantsNothing)
{
    PresenceRegistry registry;
    registry.note("1", 0);
    registry.applyStates(registry.takeBatch(), {{"1", PresenceState::Online}},
                         0);

    registry.note("1", fresh + 1);
    EXPECT_FALSE(registry.applyStates(registry.takeBatch(),
                                      {{"1", PresenceState::Online}},
                                      fresh + 1));
}

TEST(PresenceRegistry, doesNotReaskWhileTheAnswerIsFresh)
{
    PresenceRegistry registry;
    registry.note("1", 0);
    registry.applyStates(registry.takeBatch(), {{"1", PresenceState::Online}},
                         0);

    // A busy chat mentions the same people constantly. Asking every time would
    // be a request per message.
    EXPECT_FALSE(registry.note("1", fresh - 1));
    EXPECT_EQ(registry.pendingCount(), 0);
}

TEST(PresenceRegistry, asksAgainOnceTheAnswerIsStale)
{
    PresenceRegistry registry;
    registry.note("1", 0);
    registry.applyStates(registry.takeBatch(), {{"1", PresenceState::Online}},
                         0);

    // Unlike a ban, presence changes on its own and nothing announces it.
    EXPECT_TRUE(registry.note("1", fresh + 1));
}

TEST(PresenceRegistry, keepsShowingAStaleAnswerWhileReasking)
{
    PresenceRegistry registry;
    registry.note("1", 0);
    registry.applyStates(registry.takeBatch(), {{"1", PresenceState::Online}},
                         0);
    registry.note("1", fresh + 1);

    // A dot that blinks out while being refreshed is worse than one that is a
    // minute out of date.
    EXPECT_EQ(registry.state("1", fresh + 1), PresenceState::Online);
}

TEST(PresenceRegistry, doesNotAskAboutSomebodyAlreadyInFlight)
{
    PresenceRegistry registry;
    registry.note("1", 0);
    registry.takeBatch();

    EXPECT_FALSE(registry.note("1", 0));
    EXPECT_TRUE(registry.takeBatch().isEmpty());
}

TEST(PresenceRegistry, aFailedBatchIsQueuedAgain)
{
    PresenceRegistry registry;
    registry.note("1", 0);
    auto batch = registry.takeBatch();

    registry.failBatch(batch);

    EXPECT_EQ(registry.pendingCount(), 1);
    EXPECT_FALSE(registry.state("1", 0).has_value());
}

TEST(PresenceRegistry, batchesAreCappedAtWhatTheRouteAccepts)
{
    PresenceRegistry registry;
    for (int i = 0; i < PresenceRegistry::maxBatchSize + 3; i++)
    {
        registry.note(QString::number(i), 0);
    }

    EXPECT_EQ(registry.takeBatch().size(), PresenceRegistry::maxBatchSize);
    EXPECT_EQ(registry.pendingCount(), 3);
}

TEST(PresenceRegistry, ignoresEmptyIds)
{
    PresenceRegistry registry;

    EXPECT_FALSE(registry.note("", 0));
    EXPECT_EQ(registry.pendingCount(), 0);
}

TEST(PresenceRegistry, clearForgetsEverything)
{
    PresenceRegistry registry;
    registry.note("1", 0);
    registry.applyStates(registry.takeBatch(), {{"1", PresenceState::Online}},
                         0);

    registry.clear();

    EXPECT_TRUE(registry.isEmpty());
    EXPECT_FALSE(registry.state("1", 0).has_value());
}

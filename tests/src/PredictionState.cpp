// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/twitch/PredictionState.hpp"

#include "common/Literals.hpp"
#include "providers/twitch/api/Helix.hpp"
#include "Test.hpp"
#include "util/PercentageShares.hpp"

#include <QJsonDocument>
#include <QJsonObject>

using namespace chatterino;
using namespace literals;

namespace {

const QDateTime NOW = QDateTime::fromSecsSinceEpoch(1700000000, Qt::UTC);

PredictionOutcome outcome(const QString &id, qint64 points, int users = 1)
{
    return {
        .id = id,
        .title = u"Outcome "_s % id,
        .users = users,
        .channelPoints = points,
        .color = PredictionColor::Blue,
    };
}

Prediction running(std::vector<PredictionOutcome> outcomes)
{
    return {
        .id = u"p1"_s,
        .title = u"Will it work"_s,
        .status = PredictionStatus::Active,
        .winningOutcomeId = {},
        .createdAt = NOW,
        .window = std::chrono::seconds{120},
        .outcomes = std::move(outcomes),
    };
}

/// A real Helix response body, so the field names are Twitch's own rather than
/// ones this test made up.
const char *HELIX_BODY = R"({
    "id": "d6676d5c-c86e-44d2-bfc4-100fb48f0656",
    "broadcaster_id": "559931613",
    "title": "Will it work",
    "winning_outcome_id": null,
    "outcomes": [
        {
            "id": "02535c3e-2e9f-4ba0-9d77-c40d5db23c3b",
            "title": "Yes",
            "users": 3,
            "channel_points": 300,
            "top_predictors": [],
            "color": "BLUE"
        },
        {
            "id": "0f8c9d67-c16b-4d3f-a1f9-a70b7a0e4d1e",
            "title": "No",
            "users": 1,
            "channel_points": 100,
            "top_predictors": [],
            "color": "PINK"
        }
    ],
    "prediction_window": 120,
    "status": "ACTIVE",
    "created_at": "2026-09-28T00:14:04.0964348Z",
    "ended_at": null,
    "locked_at": null
})";

HelixPrediction fromBody()
{
    auto doc = QJsonDocument::fromJson(QByteArray(HELIX_BODY));
    EXPECT_FALSE(doc.isNull()) << "the fixture is not valid JSON";
    return HelixPrediction(doc.object());
}

}  // namespace

TEST(PercentageShares, addsUpToOneHundred)
{
    auto shares = percentageShares({1, 1, 1});

    EXPECT_EQ(shares[0] + shares[1] + shares[2], 100)
        << "columns reading 33/33/33 look like a rounding bug";
}

TEST(PercentageShares, givesNothingAwayBeforeAnythingIsCounted)
{
    EXPECT_EQ(percentageShares({0, 0}), (std::vector<int>{0, 0}));
    EXPECT_TRUE(percentageShares({}).empty());
}

TEST(PercentageShares, ignoresNegativeCounts)
{
    auto shares = percentageShares({-5, 100});

    EXPECT_EQ(shares[0], 0);
    EXPECT_EQ(shares[1], 100);
}

TEST(PredictionState, sharesThePotBetweenTheSides)
{
    auto prediction = running({outcome(u"a"_s, 300), outcome(u"b"_s, 100)});
    auto shares = predictionShares(prediction.outcomes);

    EXPECT_EQ(shares[0], 75);
    EXPECT_EQ(shares[1], 25);
}

TEST(PredictionState, sharesNothingWhenNobodyHasBet)
{
    auto prediction = running({outcome(u"a"_s, 0), outcome(u"b"_s, 0)});
    auto shares = predictionShares(prediction.outcomes);

    // Not 50/50: nobody has bet, so no side is winning.
    EXPECT_EQ(shares[0], 0);
    EXPECT_EQ(shares[1], 0);
}

TEST(PredictionState, worksOutWhatAPointComesBackAs)
{
    auto outcomes = std::vector{outcome(u"a"_s, 300), outcome(u"b"_s, 100)};

    // 400 staked in total; a point on the smaller side comes back as four.
    EXPECT_DOUBLE_EQ(predictionReturn(outcomes, 1), 4.0);
    EXPECT_NEAR(predictionReturn(outcomes, 0), 1.3333, 0.001);
}

TEST(PredictionState, offersNoReturnOnASideNobodyIsAgainst)
{
    // Everything is on one side, so there is nothing to win off anybody.
    auto outcomes = std::vector{outcome(u"a"_s, 500), outcome(u"b"_s, 0)};

    EXPECT_DOUBLE_EQ(predictionReturn(outcomes, 0), 0.0);
    EXPECT_DOUBLE_EQ(predictionReturn(outcomes, 1), 0.0);
    EXPECT_TRUE(formatPredictionReturn(predictionReturn(outcomes, 0)).isEmpty());
}

TEST(PredictionState, formatsTheReturnTheWayPeopleReadIt)
{
    EXPECT_EQ(formatPredictionReturn(1.8), u"1.80x"_s);
    EXPECT_EQ(formatPredictionReturn(4.0), u"4.00x"_s);
    // At or below evens there is nothing worth saying.
    EXPECT_TRUE(formatPredictionReturn(1.0).isEmpty());
    EXPECT_TRUE(formatPredictionReturn(0.0).isEmpty());
}

TEST(PredictionState, countsDownWhileBettingIsOpen)
{
    auto prediction = running({outcome(u"a"_s, 1)});

    EXPECT_EQ(prediction.secondsRemaining(NOW), 120);
    EXPECT_EQ(prediction.secondsRemaining(NOW.addSecs(90)), 30);
    EXPECT_EQ(prediction.secondsRemaining(NOW.addSecs(120)), 0);
    EXPECT_EQ(prediction.secondsRemaining(NOW.addSecs(9999)), 0);
}

TEST(PredictionState, refusesToCountDownFromMoreThanTheWindow)
{
    auto prediction = running({outcome(u"a"_s, 1)});

    // A clock behind the one that stamped it would otherwise count upwards.
    EXPECT_EQ(prediction.secondsRemaining(NOW.addSecs(-500)), 120);
}

TEST(PredictionState, hasNoCountdownWithoutBothHalvesOfOne)
{
    auto prediction = running({outcome(u"a"_s, 1)});
    prediction.window = std::chrono::seconds{0};
    EXPECT_EQ(prediction.secondsRemaining(NOW), 0);

    prediction = running({outcome(u"a"_s, 1)});
    prediction.createdAt = {};
    EXPECT_EQ(prediction.secondsRemaining(NOW), 0);
}

TEST(PredictionState, namesTheWinnerOnlyOnceThereIsOne)
{
    auto prediction = running({outcome(u"a"_s, 300), outcome(u"b"_s, 100)});
    EXPECT_EQ(prediction.winner(), nullptr);
    EXPECT_FALSE(prediction.hasShowableResult());

    prediction.status = PredictionStatus::Resolved;
    prediction.winningOutcomeId = u"b"_s;

    ASSERT_NE(prediction.winner(), nullptr);
    EXPECT_EQ(prediction.winner()->id, u"b"_s);
    EXPECT_TRUE(prediction.hasShowableResult());
}

TEST(PredictionState, hasNothingToShowForAPredictionThatWasCalledOff)
{
    auto prediction = running({outcome(u"a"_s, 300)});
    prediction.status = PredictionStatus::Canceled;

    EXPECT_FALSE(prediction.hasShowableResult())
        << "everybody got their points back; there is no result";
}

TEST(PredictionState, survivesAWinnerIdThatNamesNoOutcome)
{
    auto prediction = running({outcome(u"a"_s, 300)});
    prediction.status = PredictionStatus::Resolved;
    prediction.winningOutcomeId = u"gone"_s;

    EXPECT_EQ(prediction.winner(), nullptr);
    EXPECT_FALSE(prediction.hasShowableResult());
}

TEST(PredictionState, readsTheStatusesTwitchSends)
{
    EXPECT_EQ(parsePredictionStatus(u"ACTIVE"_s), PredictionStatus::Active);
    EXPECT_EQ(parsePredictionStatus(u"LOCKED"_s), PredictionStatus::Locked);
    EXPECT_EQ(parsePredictionStatus(u"RESOLVED"_s), PredictionStatus::Resolved);
    EXPECT_EQ(parsePredictionStatus(u"CANCELED"_s), PredictionStatus::Canceled);
    // Twitch has used both spellings over the years.
    EXPECT_EQ(parsePredictionStatus(u"CANCELLED"_s),
              PredictionStatus::Canceled);
    // Something new must still be shown rather than silently dropped.
    EXPECT_EQ(parsePredictionStatus(u"SOMETHING_NEW"_s),
              PredictionStatus::Active);
}

TEST(PredictionState, readsTheTwoColours)
{
    EXPECT_EQ(parsePredictionColor(u"BLUE"_s), PredictionColor::Blue);
    EXPECT_EQ(parsePredictionColor(u"PINK"_s), PredictionColor::Pink);
    EXPECT_EQ(parsePredictionColor(u"pink"_s), PredictionColor::Pink);
    EXPECT_EQ(parsePredictionColor(QString()), PredictionColor::Blue);
}

TEST(PredictionState, readsWhatTheOfficialApiReturns)
{
    auto prediction = predictionFromHelix(fromBody());

    EXPECT_EQ(prediction.title, u"Will it work"_s);
    EXPECT_EQ(prediction.status, PredictionStatus::Active);
    EXPECT_EQ(prediction.window, std::chrono::seconds{120});
    EXPECT_TRUE(prediction.createdAt.isValid())
        << "the countdown has nothing to run from";

    ASSERT_EQ(prediction.outcomes.size(), 2u);
    EXPECT_EQ(prediction.outcomes[0].title, u"Yes"_s);
    EXPECT_EQ(prediction.outcomes[0].users, 3);
    EXPECT_EQ(prediction.outcomes[0].channelPoints, 300);
    EXPECT_EQ(prediction.outcomes[0].color, PredictionColor::Blue);
    EXPECT_EQ(prediction.outcomes[1].color, PredictionColor::Pink);

    EXPECT_EQ(prediction.totalPoints(), 400);
    EXPECT_EQ(predictionShares(prediction.outcomes)[0], 75);
}

TEST(PredictionState, survivesAnApiResponseWithNothingInIt)
{
    auto prediction = predictionFromHelix(HelixPrediction(QJsonObject{}));

    EXPECT_TRUE(prediction.outcomes.empty());
    EXPECT_EQ(prediction.secondsRemaining(NOW), 0);
    EXPECT_EQ(prediction.winner(), nullptr);
    EXPECT_EQ(prediction.totalPoints(), 0);
}

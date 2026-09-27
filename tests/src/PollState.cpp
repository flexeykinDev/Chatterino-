// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/twitch/PollState.hpp"

#include <gtest/gtest.h>
#include <QJsonDocument>
#include <QJsonObject>

#include <numeric>

using namespace chatterino;

namespace {

Poll pollWith(std::vector<int> votes, Poll::Status status = Poll::Status::Active)
{
    Poll poll;
    poll.status = status;
    for (std::size_t i = 0; i < votes.size(); i++)
    {
        poll.choices.push_back({
            .id = QString::number(i),
            .title = QStringLiteral("choice %1").arg(i),
            .votes = votes[i],
        });
    }
    return poll;
}

int sum(const std::vector<int> &values)
{
    return std::accumulate(values.begin(), values.end(), 0);
}

}  // namespace

TEST(PollState, countsEveryVote)
{
    EXPECT_EQ(pollWith({3, 4, 5}).totalVotes(), 12);
    EXPECT_EQ(pollWith({}).totalVotes(), 0);
    EXPECT_EQ(pollWith({0, 0}).totalVotes(), 0);
}

TEST(PollState, ignoresNonsenseVoteCounts)
{
    // A negative count is not something to subtract from the total.
    EXPECT_EQ(pollWith({5, -3}).totalVotes(), 5);
}

TEST(PollState, knowsWhetherItIsStillRunning)
{
    EXPECT_TRUE(pollWith({1}, Poll::Status::Active).isRunning());
    EXPECT_FALSE(pollWith({1}, Poll::Status::Completed).isRunning());
    EXPECT_FALSE(pollWith({1}, Poll::Status::Terminated).isRunning());
}

TEST(PollState, showsAResultForAPollThatRanOrWasEndedEarly)
{
    EXPECT_TRUE(pollWith({1}, Poll::Status::Completed).hasShowableResult());
    // Ended early, but the votes still stand and are worth seeing.
    EXPECT_TRUE(pollWith({1}, Poll::Status::Terminated).hasShowableResult());
}

TEST(PollState, showsNoResultForARetiredPoll)
{
    // Observed on a real poll: Twitch sends POLL_COMPLETE when voting ends and
    // then POLL_ARCHIVE about a minute later, on its own. By the time archive
    // arrives the banner should be long gone, so this is a backstop.
    EXPECT_FALSE(pollWith({1}, Poll::Status::Archived).hasShowableResult());
}

TEST(PollState, countsDownWhileRunning)
{
    auto now = QDateTime::currentDateTime();
    auto poll = pollWith({1});
    poll.endsAt = now.addSecs(45);

    EXPECT_EQ(poll.secondsRemaining(now), 45);
}

TEST(PollState, neverCountsBelowZero)
{
    auto now = QDateTime::currentDateTime();
    auto poll = pollWith({1});
    poll.endsAt = now.addSecs(-30);

    // A clock that is behind must not produce a countdown running backwards.
    EXPECT_EQ(poll.secondsRemaining(now), 0);
}

TEST(PollState, anEndedPollHasNoTimeLeftWhateverItsClockSays)
{
    auto now = QDateTime::currentDateTime();
    auto poll = pollWith({1}, Poll::Status::Completed);
    poll.endsAt = now.addSecs(60);

    EXPECT_EQ(poll.secondsRemaining(now), 0);
}

TEST(PollState, namesTheLeader)
{
    auto poll = pollWith({3, 9, 4});

    ASSERT_NE(poll.leader(), nullptr);
    EXPECT_EQ(poll.leader()->votes, 9);
}

TEST(PollState, hasNoLeaderWhenTheLeadIsTied)
{
    // Picking whichever came first would be inventing a winner.
    EXPECT_EQ(pollWith({5, 5}).leader(), nullptr);
    EXPECT_EQ(pollWith({2, 5, 5}).leader(), nullptr);
}

TEST(PollState, hasNoLeaderBeforeAnybodyVotes)
{
    EXPECT_EQ(pollWith({0, 0, 0}).leader(), nullptr);
    EXPECT_EQ(pollWith({}).leader(), nullptr);
}

TEST(PollState, aTieBelowTheLeadStillHasALeader)
{
    auto poll = pollWith({4, 4, 9});

    ASSERT_NE(poll.leader(), nullptr);
    EXPECT_EQ(poll.leader()->votes, 9);
}

// --- percentages -------------------------------------------------------

TEST(PollPercentages, splitsAnEvenVote)
{
    EXPECT_EQ(pollPercentages(pollWith({1, 1}).choices),
              std::vector<int>({50, 50}));
}

TEST(PollPercentages, addsUpToOneHundred)
{
    // Rounding each share on its own gives 33/33/33, which reads as a bug to
    // anyone who adds them up.
    for (auto votes : {std::vector<int>{1, 1, 1}, std::vector<int>{1, 1, 1, 1, 1, 1},
                       std::vector<int>{7, 11, 13}, std::vector<int>{1, 2, 3, 4, 5, 6, 7}})
    {
        auto shares = pollPercentages(pollWith(votes).choices);
        EXPECT_EQ(sum(shares), 100)
            << "shares did not total 100 for " << votes.size() << " choices";
    }
}

TEST(PollPercentages, givesTheRemainderToWhoLostMostToRounding)
{
    // 1/1/1 of three: each floors to 33, and the extra point goes to the first
    // by the tie-break, so the same votes always draw the same picture.
    EXPECT_EQ(pollPercentages(pollWith({1, 1, 1}).choices),
              std::vector<int>({34, 33, 33}));
}

TEST(PollPercentages, isAllZeroBeforeAnybodyVotes)
{
    EXPECT_EQ(pollPercentages(pollWith({0, 0}).choices),
              std::vector<int>({0, 0}));
}

TEST(PollPercentages, handlesAPollWithNoChoices)
{
    EXPECT_TRUE(pollPercentages({}).empty());
}

TEST(PollPercentages, keepsTheOrderOfTheChoices)
{
    // The bars are drawn in the order the broadcaster wrote them, so the
    // shares have to line up with that rather than with the ranking.
    auto shares = pollPercentages(pollWith({10, 70, 20}).choices);

    ASSERT_EQ(shares.size(), 3u);
    EXPECT_EQ(shares[0], 10);
    EXPECT_EQ(shares[1], 70);
    EXPECT_EQ(shares[2], 20);
}

TEST(PollPercentages, ignoresNonsenseVoteCounts)
{
    auto shares = pollPercentages(pollWith({5, -5}).choices);

    EXPECT_EQ(shares[0], 100);
    EXPECT_EQ(shares[1], 0);
}

// --- parsing the real payload ------------------------------------------

namespace {

/// The frame Twitch actually sent for a live poll on a real channel, kept
/// verbatim. Guessing at this shape is how a parser ends up silently
/// returning nothing.
constexpr const char *REAL_POLL_FRAME = R"({
  "type": "POLL_UPDATE",
  "data": {"poll": {
    "poll_id": "6e9696d0-197a-4247-80e5-09ddbe66a243",
    "owned_by": "559931613", "created_by": "559931613",
    "title": "test",
    "started_at": "2026-09-27T17:59:55.899117365Z",
    "ended_at": null, "ended_by": null,
    "duration_seconds": 300,
    "settings": {"multi_choice": {"is_enabled": true},
                 "bits_votes": {"is_enabled": false, "cost": 0},
                 "channel_points_votes": {"is_enabled": false, "cost": 0}},
    "status": "ACTIVE",
    "choices": [
      {"choice_id": "a1b0116f-aaa0-4fc4-8e61-f3d7f276ace8", "title": "y",
       "votes": {"total": 0, "bits": 0, "channel_points": 0, "base": 0, "granted": 0},
       "tokens": {"bits": 0, "channel_points": 0}, "total_voters": 0},
      {"choice_id": "61810d1c-9cdc-43e9-8621-0da766710deb", "title": "n",
       "votes": {"total": 1, "bits": 0, "channel_points": 0, "base": 1, "granted": 0},
       "tokens": {"bits": 0, "channel_points": 0}, "total_voters": 1}
    ],
    "votes": {"total": 1, "bits": 0, "channel_points": 0, "base": 1, "granted": 0},
    "tokens": {"bits": 0, "channel_points": 0},
    "total_voters": 1,
    "remaining_duration_milliseconds": 209681,
    "top_contributor": null
  }}
})";

QJsonObject asJson(const char *text)
{
    QJsonParseError error{};
    auto document = QJsonDocument::fromJson(QByteArray(text), &error);
    EXPECT_EQ(error.error, QJsonParseError::NoError)
        << error.errorString().toStdString();
    return document.object();
}

}  // namespace

TEST(PollParsing, readsTheFrameTwitchActuallySends)
{
    auto poll = parsePollFrame(asJson(REAL_POLL_FRAME));

    ASSERT_TRUE(poll.has_value());
    EXPECT_EQ(poll->id, "6e9696d0-197a-4247-80e5-09ddbe66a243");
    EXPECT_EQ(poll->title, "test");
    EXPECT_TRUE(poll->isRunning());
    EXPECT_EQ(poll->totalVoters, 1);

    ASSERT_EQ(poll->choices.size(), 2u);
    EXPECT_EQ(poll->choices[0].title, "y");
    EXPECT_EQ(poll->choices[0].votes, 0);
    EXPECT_EQ(poll->choices[1].title, "n");
    EXPECT_EQ(poll->choices[1].votes, 1);
    EXPECT_EQ(poll->totalVotes(), 1);
}

TEST(PollParsing, takesTheVoteFromTheTotalRatherThanTheBaseCount)
{
    // Votes arrive split across base, bits and channel points. Only `total`
    // is the number a viewer sees on the bar.
    auto poll = parsePollFrame(asJson(REAL_POLL_FRAME));

    ASSERT_TRUE(poll.has_value());
    EXPECT_EQ(pollPercentages(poll->choices), std::vector<int>({0, 100}));
}

TEST(PollParsing, usesTwitchsOwnCountdown)
{
    auto now = QDateTime::currentDateTime();
    auto poll = parsePollFrame(asJson(REAL_POLL_FRAME));

    ASSERT_TRUE(poll.has_value());
    // 209681ms remaining. Derived from the countdown Twitch sends rather than
    // from started_at plus duration, which disagree whenever the clocks do.
    EXPECT_NEAR(poll->secondsRemaining(now), 209, 2);
}

TEST(PollParsing, readsTheTerminalStatuses)
{
    for (auto [text, running] : {std::pair{"COMPLETED", false},
                                 std::pair{"TERMINATED", false},
                                 std::pair{"ACTIVE", true}})
    {
        auto frame = QString(REAL_POLL_FRAME)
                         .replace("\"status\": \"ACTIVE\"",
                                  QString("\"status\": \"%1\"").arg(text));
        auto poll = parsePollFrame(asJson(frame.toUtf8().constData()));

        ASSERT_TRUE(poll.has_value()) << text;
        EXPECT_EQ(poll->isRunning(), running) << text;
    }
}

TEST(PollParsing, treatsAnUnknownStatusAsNotWorthShowing)
{
    // Showing a poll in a state this build does not understand is the worse
    // of the two mistakes.
    auto frame = QString(REAL_POLL_FRAME)
                     .replace("\"status\": \"ACTIVE\"",
                              "\"status\": \"SOMETHING_NEW\"");
    auto poll = parsePollFrame(asJson(frame.toUtf8().constData()));

    ASSERT_TRUE(poll.has_value());
    EXPECT_FALSE(poll->isRunning());
    EXPECT_FALSE(poll->hasShowableResult());
}

TEST(PollParsing, ignoresFramesWithNoPollInThem)
{
    EXPECT_FALSE(parsePollFrame(asJson(R"({"type":"SOMETHING_ELSE"})")));
    EXPECT_FALSE(parsePollFrame(asJson(R"({"data":{}})")));
    EXPECT_FALSE(parsePollFrame(asJson("{}")));
}

TEST(PollParsing, rejectsAPollWithNoChoices)
{
    // Nothing to draw, and a bar chart of nothing is worse than no banner.
    EXPECT_FALSE(parsePollFrame(asJson(
        R"({"data":{"poll":{"poll_id":"x","title":"t","choices":[]}}})")));
}

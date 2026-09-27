// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/twitch/PollState.hpp"

#include <gtest/gtest.h>

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

TEST(PollState, showsNoResultForAWithdrawnPoll)
{
    // Archived means the broadcaster took it down. Putting the result on
    // screen anyway would announce something they chose to hide.
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

// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/dialogs/CreatePollDialog.hpp"

#include <gtest/gtest.h>

using namespace chatterino;

namespace {

PollDraft valid()
{
    return {
        .title = "what are we playing",
        .choices = {"one", "two"},
        .duration = std::chrono::seconds(120),
        .pointsPerVote = 0,
    };
}

}  // namespace

TEST(PollDraft, acceptsAFilledInPoll)
{
    EXPECT_EQ(valid().problem(), QString());
}

TEST(PollDraft, asksForAQuestion)
{
    auto draft = valid();
    draft.title = "   ";

    // A question that is only spaces is not one, and Twitch would refuse it
    // after the round trip rather than before.
    EXPECT_FALSE(draft.problem().isEmpty());
}

TEST(PollDraft, refusesAQuestionTwitchWouldReject)
{
    auto draft = valid();
    draft.title = QString(PollDraft::maxTitleLength + 1, 'x');

    auto problem = draft.problem();
    EXPECT_FALSE(problem.isEmpty());
    // Says the limit and the length, so the fix is obvious without counting.
    EXPECT_TRUE(problem.contains(QString::number(PollDraft::maxTitleLength)));
}

TEST(PollDraft, needsTwoChoices)
{
    auto draft = valid();
    draft.choices = {"only one", ""};

    EXPECT_FALSE(draft.problem().isEmpty());
}

TEST(PollDraft, ignoresBlankChoiceRows)
{
    auto draft = valid();
    // The form always carries empty rows; they are not choices.
    draft.choices = {"one", "  ", "two", ""};

    EXPECT_EQ(draft.problem(), QString());
    EXPECT_EQ(draft.filledChoices(), QStringList({"one", "two"}));
}

TEST(PollDraft, trimsChoicesBeforeSending)
{
    auto draft = valid();
    draft.choices = {"  one  ", "two "};

    EXPECT_EQ(draft.filledChoices(), QStringList({"one", "two"}));
}

TEST(PollDraft, refusesTooManyChoices)
{
    auto draft = valid();
    draft.choices.clear();
    for (int i = 0; i <= PollDraft::maxChoices; i++)
    {
        draft.choices.append(QString("choice %1").arg(i));
    }

    EXPECT_FALSE(draft.problem().isEmpty());
}

TEST(PollDraft, refusesAChoiceTwitchWouldReject)
{
    auto draft = valid();
    draft.choices = {"fine", QString(PollDraft::maxChoiceLength + 1, 'y')};

    auto problem = draft.problem();
    EXPECT_FALSE(problem.isEmpty());
    // Names the offending choice: with five rows, "a choice is too long" on
    // its own leaves you hunting.
    EXPECT_TRUE(problem.contains("yyy"));
}

TEST(PollDraft, refusesADurationOutsideTwitchsRange)
{
    auto tooShort = valid();
    tooShort.duration = std::chrono::seconds(PollDraft::minDurationSeconds - 1);
    EXPECT_FALSE(tooShort.problem().isEmpty());

    auto tooLong = valid();
    tooLong.duration = std::chrono::seconds(PollDraft::maxDurationSeconds + 1);
    EXPECT_FALSE(tooLong.problem().isEmpty());
}

TEST(PollDraft, acceptsTheEdgesOfTheRange)
{
    auto shortest = valid();
    shortest.duration = std::chrono::seconds(PollDraft::minDurationSeconds);
    EXPECT_EQ(shortest.problem(), QString());

    auto longest = valid();
    longest.duration = std::chrono::seconds(PollDraft::maxDurationSeconds);
    EXPECT_EQ(longest.problem(), QString());
}

TEST(PollDraft, saysWhatToFixRatherThanThatSomethingIsWrong)
{
    // Each message names the thing to change. A disabled button with no
    // explanation is the failure mode this is meant to avoid.
    auto noTitle = valid();
    noTitle.title = "";
    EXPECT_TRUE(noTitle.problem().contains("question", Qt::CaseInsensitive));

    auto oneChoice = valid();
    oneChoice.choices = {"one", ""};
    EXPECT_TRUE(oneChoice.problem().contains("choice", Qt::CaseInsensitive));
}

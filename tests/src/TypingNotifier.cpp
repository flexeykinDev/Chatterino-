// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/companion/TypingNotifier.hpp"

#include <gtest/gtest.h>

using namespace chatterino;

namespace {

constexpr qint64 repeat = TypingNotifier::repeatAfterMs;

}  // namespace

TEST(TypingNotifier, saysNothingBeforeAnythingIsTyped)
{
    TypingNotifier notifier;

    EXPECT_FALSE(notifier.inputChanged(false, 0).has_value());
    EXPECT_FALSE(notifier.isTyping());
}

TEST(TypingNotifier, announcesTheFirstKeystroke)
{
    TypingNotifier notifier;

    auto decision = notifier.inputChanged(true, 1000);

    ASSERT_TRUE(decision.has_value());
    EXPECT_TRUE(*decision);
    EXPECT_TRUE(notifier.isTyping());
}

TEST(TypingNotifier, staysSilentForEveryKeystrokeAfterThat)
{
    TypingNotifier notifier;
    notifier.inputChanged(true, 1000);

    // A fast typist produces dozens of these a second. Telling the server
    // about each one would be the entire cost of the feature.
    for (qint64 t = 1010; t < 1000 + repeat; t += 10)
    {
        EXPECT_FALSE(notifier.inputChanged(true, t).has_value())
            << "sent a frame at t=" << t;
    }
}

TEST(TypingNotifier, repeatsSoTheIndicatorDoesNotExpireMidSentence)
{
    TypingNotifier notifier;
    notifier.inputChanged(true, 1000);

    auto decision = notifier.inputChanged(true, 1000 + repeat);

    ASSERT_TRUE(decision.has_value());
    EXPECT_TRUE(*decision);
}

TEST(TypingNotifier, repeatsOnItsOwnSchedule)
{
    TypingNotifier notifier;
    notifier.inputChanged(true, 0);

    // The clock for the next repeat restarts from the frame that was sent,
    // not from when typing began, or a long sentence would send a burst.
    ASSERT_TRUE(notifier.inputChanged(true, repeat).has_value());
    EXPECT_FALSE(notifier.inputChanged(true, repeat + 10).has_value());
    EXPECT_TRUE(notifier.inputChanged(true, repeat * 2).has_value());
}

TEST(TypingNotifier, saysSoWhenTheBoxIsEmptied)
{
    TypingNotifier notifier;
    notifier.inputChanged(true, 1000);

    auto decision = notifier.inputChanged(false, 1200);

    ASSERT_TRUE(decision.has_value());
    EXPECT_FALSE(*decision);
    EXPECT_FALSE(notifier.isTyping());
}

TEST(TypingNotifier, doesNotRepeatThatTheBoxIsEmpty)
{
    TypingNotifier notifier;
    notifier.inputChanged(true, 1000);
    notifier.inputChanged(false, 1200);

    EXPECT_FALSE(notifier.inputChanged(false, 1300).has_value());
    EXPECT_FALSE(notifier.inputChanged(false, 9999).has_value());
}

TEST(TypingNotifier, announcesTypingAgainAfterTheBoxWasCleared)
{
    TypingNotifier notifier;
    notifier.inputChanged(true, 1000);
    notifier.inputChanged(false, 1200);

    // Starting over is news even though it is well inside the repeat window:
    // the server was told they had stopped.
    auto decision = notifier.inputChanged(true, 1300);

    ASSERT_TRUE(decision.has_value());
    EXPECT_TRUE(*decision);
}

TEST(TypingNotifier, saysTheyStoppedWhenTheMessageGoes)
{
    TypingNotifier notifier;
    notifier.inputChanged(true, 1000);

    auto decision = notifier.messageSent(1500);

    ASSERT_TRUE(decision.has_value());
    EXPECT_FALSE(*decision);
    EXPECT_FALSE(notifier.isTyping());
}

TEST(TypingNotifier, saysNothingWhenAMessageGoesWithoutTyping)
{
    TypingNotifier notifier;

    // A command, or a message pasted and sent in one motion. Nobody was told
    // anyone was typing, so there is nothing to take back.
    EXPECT_FALSE(notifier.messageSent(1000).has_value());
}

TEST(TypingNotifier, resetForgetsWithoutSending)
{
    TypingNotifier notifier;
    notifier.inputChanged(true, 1000);

    // The connection dropped. There is nobody to tell, and the next connection
    // starts from nothing.
    notifier.reset();

    EXPECT_FALSE(notifier.isTyping());
    EXPECT_FALSE(notifier.inputChanged(false, 1100).has_value());

    auto decision = notifier.inputChanged(true, 1200);
    ASSERT_TRUE(decision.has_value());
    EXPECT_TRUE(*decision);
}

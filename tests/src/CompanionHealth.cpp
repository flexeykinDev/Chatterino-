// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/companion/CompanionHealth.hpp"

#include <gtest/gtest.h>

using namespace chatterino;

namespace {

/// A health tracker that is configured and signed in, which is the only state
/// in which reachability is a meaningful question.
CompanionHealth ready()
{
    CompanionHealth health;
    health.setAvailability(true, true);
    return health;
}

void fail(CompanionHealth &health, int times)
{
    for (int i = 0; i < times; i++)
    {
        health.recordFailure();
    }
}

}  // namespace

TEST(CompanionHealth, startsOff)
{
    CompanionHealth health;

    // The default is no address, and that is a choice rather than a fault.
    EXPECT_EQ(health.status(), CompanionStatus::Disabled);
}

TEST(CompanionHealth, distinguishesSignedOutFromBroken)
{
    CompanionHealth health;
    health.setAvailability(true, false);

    // Anonymous clients can ask for nothing, so every feature shows nothing.
    // Reporting that as "not answering" would send someone looking for a
    // server fault that is not there.
    EXPECT_EQ(health.status(), CompanionStatus::SignedOut);
}

TEST(CompanionHealth, configuredButUntriedIsNotYetReachable)
{
    auto health = ready();

    // Claiming it answers before anything has been asked is a guess.
    EXPECT_EQ(health.status(), CompanionStatus::Unknown);
}

TEST(CompanionHealth, oneFailureIsNotEnoughToCallItDown)
{
    auto health = ready();
    health.recordSuccess();

    health.recordFailure();

    // A request lost to a dropped connection or a sleeping laptop is ordinary,
    // and a status that flickers on one is worse than no status.
    EXPECT_EQ(health.status(), CompanionStatus::Reachable);
}

TEST(CompanionHealth, repeatedFailuresMeanUnreachable)
{
    auto health = ready();
    health.recordSuccess();

    fail(health, CompanionHealth::failuresBeforeUnreachable);

    EXPECT_EQ(health.status(), CompanionStatus::Unreachable);
}

TEST(CompanionHealth, failsFromColdWithoutEverSucceeding)
{
    auto health = ready();

    fail(health, CompanionHealth::failuresBeforeUnreachable);

    // A service that was never reachable is still unreachable; there is no
    // requirement to have worked once first.
    EXPECT_EQ(health.status(), CompanionStatus::Unreachable);
}

TEST(CompanionHealth, oneSuccessIsEnoughToRecover)
{
    auto health = ready();
    fail(health, CompanionHealth::failuresBeforeUnreachable);

    health.recordSuccess();

    EXPECT_EQ(health.status(), CompanionStatus::Reachable);
}

TEST(CompanionHealth, aLongOutageStillRecoversOnTheFirstAnswer)
{
    auto health = ready();

    // The failure count saturates, so an hour of being down does not need an
    // hour of successes to climb back out of.
    fail(health, 1000);
    health.recordSuccess();

    EXPECT_EQ(health.status(), CompanionStatus::Reachable);
}

TEST(CompanionHealth, reportsWhetherTheStatusActuallyChanged)
{
    auto health = ready();

    EXPECT_TRUE(health.recordSuccess());
    // Still reachable: nothing to redraw.
    EXPECT_FALSE(health.recordSuccess());

    EXPECT_FALSE(health.recordFailure());
    EXPECT_TRUE(health.recordFailure());
    EXPECT_FALSE(health.recordFailure());

    EXPECT_TRUE(health.recordSuccess());
}

TEST(CompanionHealth, changingTheAddressForgetsWhatWasLearned)
{
    auto health = ready();
    fail(health, CompanionHealth::failuresBeforeUnreachable);
    ASSERT_EQ(health.status(), CompanionStatus::Unreachable);

    // A different address is a different service, and the old one's failures
    // say nothing about it.
    health.setAvailability(false, true);
    health.setAvailability(true, true);

    EXPECT_EQ(health.status(), CompanionStatus::Unknown);
}

TEST(CompanionHealth, signingInForgetsFailuresFromWhileSignedOut)
{
    CompanionHealth health;
    health.setAvailability(true, false);
    fail(health, CompanionHealth::failuresBeforeUnreachable);

    health.setAvailability(true, true);

    EXPECT_EQ(health.status(), CompanionStatus::Unknown);
}

TEST(CompanionHealth, everyStatusExplainsItself)
{
    for (auto status : {
             CompanionStatus::Disabled,
             CompanionStatus::SignedOut,
             CompanionStatus::Unknown,
             CompanionStatus::Reachable,
             CompanionStatus::Unreachable,
         })
    {
        EXPECT_FALSE(describeCompanionStatus(status).isEmpty())
            << "a status with no explanation is a status nobody can act on";
    }
}

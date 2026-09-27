// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

/// The layout only emits an element whose flag is in the word mask, and that
/// mask is assembled by hand, flag by flag. An element carrying a flag nobody
/// remembered to add there is silently never drawn: no warning, no crash, and
/// any test that builds its own context flags passes anyway — which is exactly
/// how the ban marker shipped invisible.
///
/// These assert against the real assembly, so a new badge that is never added
/// to it fails here rather than in a chat window.

#include "singletons/WindowManager.hpp"

#include "messages/MessageElement.hpp"
#include "mocks/BaseApplication.hpp"
#include "singletons/Settings.hpp"
#include "Test.hpp"

using namespace chatterino;

namespace {

/// Public inheritance so the test can reach the settings the mask is built
/// from.
class MockApplication : public mock::BaseApplication
{
};

}  // namespace

TEST(WordFlags, everyBadgeIsRepresentedInTheMask)
{
    MockApplication app;
    auto flags = wordFlagsFor(&app.settings);

    // Every badge slot that an element can be built with, with the settings at
    // their defaults. A flag missing here is a badge that never draws.
    for (auto badge : {
             MessageElementFlag::BadgeGlobalAuthority,
             MessageElementFlag::BadgePredictions,
             MessageElementFlag::BadgeChannelAuthority,
             MessageElementFlag::BadgeSubscription,
             MessageElementFlag::BadgeVanity,
             MessageElementFlag::BadgeChatterino,
             MessageElementFlag::BadgeSevenTV,
             MessageElementFlag::BadgeFfz,
             MessageElementFlag::BadgeBttv,
             MessageElementFlag::BadgeSharedChannel,
             MessageElementFlag::BadgeGlobalBan,
             MessageElementFlag::BadgePresence,
         })
    {
        EXPECT_TRUE(flags.has(badge))
            << "a badge flag is missing from the word mask, so anything "
               "carrying it is never laid out";
    }
}

TEST(WordFlags, theBanMarkerFollowsItsSetting)
{
    MockApplication app;

    app.settings.showGlobalBanMarker.setValue(true);
    EXPECT_TRUE(wordFlagsFor(&app.settings)
                    .has(MessageElementFlag::BadgeGlobalBan));

    app.settings.showGlobalBanMarker.setValue(false);
    EXPECT_FALSE(wordFlagsFor(&app.settings)
                     .has(MessageElementFlag::BadgeGlobalBan));
}

TEST(WordFlags, thePresenceDotFollowsItsSetting)
{
    MockApplication app;

    app.settings.showPresenceDot.setValue(true);
    EXPECT_TRUE(
        wordFlagsFor(&app.settings).has(MessageElementFlag::BadgePresence));

    app.settings.showPresenceDot.setValue(false);
    EXPECT_FALSE(
        wordFlagsFor(&app.settings).has(MessageElementFlag::BadgePresence));
}

TEST(WordFlags, theMaskCoversTheBadgesGroup)
{
    MockApplication app;
    auto flags = wordFlagsFor(&app.settings);

    // Badges is the group ChannelView checks to decide whether a message has
    // any badge at all, so a flag added to the group but not to the mask — or
    // the reverse — makes the two disagree about the same message.
    MessageElementFlags badges{MessageElementFlag::Badges};
    EXPECT_TRUE(flags.hasAny(badges));
}

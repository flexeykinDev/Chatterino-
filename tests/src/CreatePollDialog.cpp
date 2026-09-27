// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

/// Builds the dialog for real.
///
/// The validation rules are covered by PollDraft tests, which never construct
/// a widget — and that is precisely how this shipped crashing: adding a choice
/// row refreshed the form, and the refresh touched widgets the constructor had
/// not built yet. No test that avoids the widgets can see that.

#include "widgets/dialogs/CreatePollDialog.hpp"

#include "common/Channel.hpp"
#include "mocks/BaseApplication.hpp"
#include "singletons/Fonts.hpp"
#include "singletons/Theme.hpp"
#include "Test.hpp"

using namespace chatterino;

namespace {

class MockApplication : mock::BaseApplication
{
public:
    MockApplication()
        : theme(this->paths_)
        , fonts(this->settings)
    {
    }

    Theme *getThemes() override
    {
        return &this->theme;
    }

    Fonts *getFonts() override
    {
        return &this->fonts;
    }

    Theme theme;
    Fonts fonts;
};

}  // namespace

TEST(CreatePollDialog, buildsWithoutFallingOver)
{
    MockApplication app;
    auto channel = std::make_shared<Channel>("test", Channel::Type::Twitch);

    CreatePollDialog dialog(channel);

    // Opens with the two choice rows a poll needs at minimum, and with nothing
    // filled in it is not yet sendable.
    EXPECT_FALSE(dialog.isVisible());
}

TEST(CreatePollDialog, survivesBeingFilledIn)
{
    MockApplication app;
    auto channel = std::make_shared<Channel>("test", Channel::Type::Twitch);

    CreatePollDialog dialog(channel);

    // Adding rows is what refreshes the form, and the refresh is what reached
    // through half-built widgets.
    for (int i = 0; i < PollDraft::maxChoices + 2; i++)
    {
        dialog.addChoiceForTest(QString("choice %1").arg(i));
    }

    SUCCEED();
}

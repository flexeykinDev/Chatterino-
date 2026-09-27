// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

/// Builds the panel for real.
///
/// The model behind it is covered separately and never touches a widget, which
/// is how the poll dialog managed to pass eleven tests while crashing on sight.

#include "widgets/splits/ChatModesPopup.hpp"

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

TEST(ChatModesPopup, buildsWithNothingSwitchedOn)
{
    MockApplication app;

    ChatModesPopup popup(chatModeRows(-1, 0, false, false, false),
                         [](const QString &) {});

    SUCCEED();
}

TEST(ChatModesPopup, buildsWithEveryModeSwitchedOn)
{
    MockApplication app;

    // Active rows are drawn differently and the ones with durations carry an
    // extra label, so this is a different path through the construction.
    ChatModesPopup popup(chatModeRows(10, 30, true, true, true),
                         [](const QString &) {});

    SUCCEED();
}

TEST(ChatModesPopup, sendsTheCommandForAPlainToggle)
{
    MockApplication app;

    QStringList sent;
    ChatModesPopup popup(chatModeRows(-1, 0, false, false, false),
                         [&sent](const QString &command) {
                             sent.append(command);
                         });

    popup.applyForTest(ChatMode::EmoteOnly, 0);

    EXPECT_EQ(sent, QStringList({"/emoteonly"}));
}

TEST(ChatModesPopup, sendsTheOffCommandForAModeThatIsOn)
{
    MockApplication app;

    QStringList sent;
    ChatModesPopup popup(chatModeRows(-1, 0, true, false, false),
                         [&sent](const QString &command) {
                             sent.append(command);
                         });

    popup.applyForTest(ChatMode::Subscribers, -1);

    EXPECT_EQ(sent, QStringList({"/subscribersoff"}));
}

// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/splits/RaidBannerWidget.hpp"

#include "common/Literals.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "mocks/BaseApplication.hpp"
#include "mocks/EmoteController.hpp"
#include "mocks/Logging.hpp"
#include "mocks/TwitchIrcServer.hpp"
#include "mocks/UserData.hpp"
#include "providers/twitch/OutgoingRaid.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "Test.hpp"

#include <QDateTime>
#include <QWidget>

#include <memory>

using namespace chatterino;
using namespace literals;

namespace {

/// A TwitchChannel reaches for rather more of the application than the model
/// beside it does, and a missing piece is an access violation rather than a
/// failed assertion once asserts are compiled out.
class MockApplication : public mock::BaseApplication
{
public:
    AccountController *getAccounts() override
    {
        return &this->accounts;
    }

    ITwitchIrcServer *getTwitch() override
    {
        return &this->twitch;
    }

    EmoteController *getEmotes() override
    {
        return &this->emotes;
    }

    IUserDataController *getUserData() override
    {
        return &this->userData;
    }

    ILogging *getChatLogger() override
    {
        return &this->logging;
    }

    AccountController accounts;
    mock::MockTwitchIrcServer twitch;
    mock::EmoteController emotes;
    mock::UserDataController userData;
    mock::EmptyLogging logging;
};

}  // namespace

/// Builds the real banner against a real channel.
///
/// The model above says what the text should be; this checks the part that
/// only shows up in the wiring — that starting a raid makes the banner appear,
/// that calling it off takes it away, and that the button is connected to
/// anything at all.
class TestRaidBanner : public ::testing::Test
{
public:
    void SetUp() override
    {
        this->mockApplication = std::make_unique<MockApplication>();
        this->parent = std::make_unique<BaseWidget>(nullptr);
        this->channel = std::make_shared<TwitchChannel>(u"forsen"_s);
        this->cancelled = 0;

        this->banner = new RaidBannerWidget(this->parent.get(), [this] {
            this->cancelled++;
        });
        this->banner->setChannel(this->channel.get());
        this->parent->show();
    }

    void TearDown() override
    {
        this->parent.reset();
        this->channel.reset();
        this->mockApplication.reset();
    }

    void startRaid(int secondsAgo = 0)
    {
        this->channel->setOutgoingRaid(OutgoingRaid{
            .targetLogin = u"somestreamer"_s,
            .targetDisplayName = u"SomeStreamer"_s,
            .startedAt = QDateTime::currentDateTime().addSecs(-secondsAgo),
        });
    }

    std::unique_ptr<MockApplication> mockApplication;
    std::unique_ptr<BaseWidget> parent;
    std::shared_ptr<TwitchChannel> channel;
    RaidBannerWidget *banner = nullptr;
    int cancelled = 0;
};

TEST_F(TestRaidBanner, staysOutOfTheWayUntilThereIsARaid)
{
    EXPECT_FALSE(this->banner->isVisible());
    EXPECT_TRUE(this->banner->text().isEmpty());
}

TEST_F(TestRaidBanner, appearsWhenARaidIsStarted)
{
    this->startRaid();

    EXPECT_TRUE(this->banner->isVisible());
    EXPECT_TRUE(this->banner->text().contains(u"SomeStreamer"_s))
        << this->banner->text();
    EXPECT_TRUE(this->banner->text().contains(u"1:3"_s))
        << "no countdown: " << this->banner->text();
}

TEST_F(TestRaidBanner, goesAwayWhenTheRaidIsCalledOff)
{
    this->startRaid();
    ASSERT_TRUE(this->banner->isVisible());

    this->channel->setOutgoingRaid({});

    EXPECT_FALSE(this->banner->isVisible());
}

TEST_F(TestRaidBanner, staysHiddenForARaidThatHasAlreadyGone)
{
    // Reconnecting, or a split opened after the fact, must not resurrect a
    // countdown that ran out while nothing was looking.
    this->startRaid(200);

    EXPECT_FALSE(this->banner->isVisible());
}

TEST_F(TestRaidBanner, cancelsTheRaidWhenItsButtonIsPressed)
{
    this->startRaid();

    this->banner->cancelForTest();

    EXPECT_EQ(this->cancelled, 1)
        << "the button is not wired to anything";
}

TEST_F(TestRaidBanner, forgetsARaidWhenTheSplitMovesToAnotherChannel)
{
    this->startRaid();
    ASSERT_TRUE(this->banner->isVisible());

    auto other = std::make_shared<TwitchChannel>(u"pajlada"_s);
    this->banner->setChannel(other.get());

    EXPECT_FALSE(this->banner->isVisible())
        << "the previous channel's raid is still on screen";
}

TEST_F(TestRaidBanner, survivesLosingItsChannelAltogether)
{
    this->startRaid();

    this->banner->setChannel(nullptr);

    EXPECT_FALSE(this->banner->isVisible());
}

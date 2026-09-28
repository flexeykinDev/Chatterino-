// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/splits/PollBannerWidget.hpp"

#include "common/Literals.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "mocks/BaseApplication.hpp"
#include "mocks/EmoteController.hpp"
#include "mocks/Logging.hpp"
#include "mocks/TwitchIrcServer.hpp"
#include "mocks/UserData.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "Test.hpp"

#include <memory>

using namespace chatterino;
using namespace literals;

namespace {

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

class TestPollBanner : public ::testing::Test
{
public:
    void SetUp() override
    {
        this->mockApplication = std::make_unique<MockApplication>();
        this->parent = std::make_unique<BaseWidget>(nullptr);
        this->banner = new PollBannerWidget(this->parent.get());
    }

    void TearDown() override
    {
        this->parent.reset();
        this->mockApplication.reset();
    }

    std::unique_ptr<MockApplication> mockApplication;
    std::unique_ptr<BaseWidget> parent;
    PollBannerWidget *banner = nullptr;
};

TEST_F(TestPollBanner, hasNowhereToSendYouWithoutAChannel)
{
    EXPECT_TRUE(this->banner->voteUrl().isEmpty());
}

TEST_F(TestPollBanner, sendsYouToTheChannelsOwnChat)
{
    auto channel = std::make_shared<TwitchChannel>(u"forsen"_s);
    this->banner->setChannel(channel.get());

    // A third-party client has no API to vote through, so the nearest thing to
    // voting from here is the page where the vote can actually be cast. The
    // popout chat rather than the channel page: the poll appears there, and
    // getting to it does not start playing a stream.
    EXPECT_EQ(this->banner->voteUrl(),
              u"https://www.twitch.tv/popout/forsen/chat?popout="_s);
}

TEST_F(TestPollBanner, forgetsTheChannelWhenTheSplitLeavesIt)
{
    auto channel = std::make_shared<TwitchChannel>(u"forsen"_s);
    this->banner->setChannel(channel.get());
    ASSERT_FALSE(this->banner->voteUrl().isEmpty());

    this->banner->setChannel(nullptr);

    EXPECT_TRUE(this->banner->voteUrl().isEmpty())
        << "clicking would open the channel this split has left";
}

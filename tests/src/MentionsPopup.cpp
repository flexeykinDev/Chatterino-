// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/dialogs/MentionsPopup.hpp"

#include "common/Channel.hpp"
#include "common/Literals.hpp"
#include "messages/Message.hpp"
#include "messages/MessageFlag.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/hotkeys/HotkeyController.hpp"
#include "mocks/BaseApplication.hpp"
#include "mocks/EmoteController.hpp"
#include "mocks/Logging.hpp"
#include "mocks/TwitchIrcServer.hpp"
#include "mocks/UserData.hpp"
#include "providers/twitch/Mentions.hpp"
#include "singletons/Settings.hpp"
#include "singletons/WindowManager.hpp"
#include "Test.hpp"

#include <QDateTime>
#include <QSet>

#include <memory>
#include <vector>

using namespace chatterino;
using namespace literals;

namespace {

/// A channel view reaches for rather more of the application than a model does:
/// the word flags live in the window manager, emotes and user colours are
/// looked up while laying a message out.
class MockApplication : public mock::BaseApplication
{
public:
    MockApplication()
        : windowManager(this->args_, this->paths_, this->settings, this->theme,
                        this->fonts)
    {
    }

    WindowManager *getWindows() override
    {
        return &this->windowManager;
    }

    HotkeyController *getHotkeys() override
    {
        return &this->hotkeys;
    }

    EmoteController *getEmotes() override
    {
        return &this->emotes;
    }

    IUserDataController *getUserData() override
    {
        return &this->userData;
    }

    ITwitchIrcServer *getTwitch() override
    {
        return &this->twitch;
    }

    ILogging *getChatLogger() override
    {
        // Every message a channel takes gets logged, so without this adding one
        // in a test dereferences nothing at all.
        return &this->logging;
    }

    AccountController *getAccounts() override
    {
        // A view asks who is signed in for every message it takes: that is how
        // it knows not to filter out your own.
        return &this->accounts;
    }

    WindowManager windowManager;
    AccountController accounts;
    HotkeyController hotkeys;
    mock::EmoteController emotes;
    mock::UserDataController userData;
    mock::MockTwitchIrcServer twitch;
    mock::EmptyLogging logging;
};

/// What the popup was asked to send.
struct SentReply {
    QString channelName;
    QString messageId;
    QString text;
};

std::shared_ptr<Message> mention(const QString &id, const QString &channel,
                                 const QString &who, const QString &text)
{
    auto message = std::make_shared<Message>();
    message->id = id;
    message->channelName = channel;
    message->loginName = who.toLower();
    message->displayName = who;
    message->messageText = text;
    message->serverReceivedTime = QDateTime::currentDateTime();

    return message;
}

}  // namespace

/// Builds the real popup, with the channel it reads and the sending it does
/// both injected.
///
/// The model is tested separately; this exists because every defect in this
/// codebase has been in the wiring — a widget touched before it was built, a
/// value read before it was set — and none of those show up in a model test.
class TestMentionsPopup : public ::testing::Test
{
public:
    void SetUp() override
    {
        this->mockApplication = std::make_unique<MockApplication>();
        this->mentions = std::make_shared<Channel>(
            u"/mentions"_s, Channel::Type::TwitchMentions);
        this->sent.clear();
        this->openChannels = {u"forsen"_s, u"pajlada"_s};
    }

    void TearDown() override
    {
        if (this->popup != nullptr)
        {
            delete this->popup;
            this->popup = nullptr;
        }
        this->mentions.reset();
        this->mockApplication.reset();
    }

    /// Builds the popup. Called after the mentions have been added, or in the
    /// middle of a test to check what it makes of the list as it stands.
    MentionsPopup *build()
    {
        delete this->popup;
        this->popup = new MentionsPopup(
            this->mentions,
            [this](const QString &name) {
                return this->openChannels.contains(name);
            },
            [this](const MessagePtr &, const MentionReplyTarget &target,
                   const QString &text) {
                this->sent.push_back({target.channelName, target.messageId,
                                      text});
            });

        return this->popup;
    }

    void arrive(const std::shared_ptr<Message> &message)
    {
        this->mentions->addMessage(message, MessageContext::Original);
    }

    std::unique_ptr<MockApplication> mockApplication;
    ChannelPtr mentions;
    MentionsPopup *popup = nullptr;
    std::vector<SentReply> sent;
    QSet<QString> openChannels;
};

TEST_F(TestMentionsPopup, opensOnAnEmptyListWithoutFallingOver)
{
    auto *popup = this->build();

    EXPECT_EQ(popup->currentTarget(), nullptr);
    EXPECT_FALSE(popup->statusText().isEmpty())
        << "an empty list should say so rather than showing nothing";
}

TEST_F(TestMentionsPopup, aimsAtTheNewestMentionWithoutBeingAsked)
{
    this->arrive(mention(u"a"_s, u"forsen"_s, u"Alice"_s, u"first"_s));
    this->arrive(mention(u"b"_s, u"pajlada"_s, u"Bob"_s, u"second"_s));

    auto *popup = this->build();

    auto target = popup->currentTarget();
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->id, u"b"_s);
    EXPECT_TRUE(popup->statusText().contains(u"Bob"_s)) << popup->statusText();
    EXPECT_TRUE(popup->statusText().contains(u"pajlada"_s))
        << popup->statusText();
}

TEST_F(TestMentionsPopup, sendsTheReplyToTheChannelTheMentionCameFrom)
{
    this->arrive(mention(u"a"_s, u"forsen"_s, u"Alice"_s, u"first"_s));
    this->arrive(mention(u"b"_s, u"pajlada"_s, u"Bob"_s, u"second"_s));

    auto *popup = this->build();
    ASSERT_TRUE(popup->sendForTest(u"answering bob"_s));

    ASSERT_EQ(this->sent.size(), 1u);
    // Not whichever channel happens to be in front, which is the whole point.
    EXPECT_EQ(this->sent[0].channelName, u"pajlada"_s);
    EXPECT_EQ(this->sent[0].messageId, u"b"_s);
    EXPECT_EQ(this->sent[0].text, u"answering bob"_s);
}

TEST_F(TestMentionsPopup, stepsBackToAnEarlierMention)
{
    this->arrive(mention(u"a"_s, u"forsen"_s, u"Alice"_s, u"first"_s));
    this->arrive(mention(u"b"_s, u"pajlada"_s, u"Bob"_s, u"second"_s));

    auto *popup = this->build();
    popup->step(-1);

    auto target = popup->currentTarget();
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->id, u"a"_s);

    ASSERT_TRUE(popup->sendForTest(u"answering alice"_s));
    ASSERT_EQ(this->sent.size(), 1u);
    EXPECT_EQ(this->sent[0].channelName, u"forsen"_s);
    EXPECT_EQ(this->sent[0].messageId, u"a"_s);
}

TEST_F(TestMentionsPopup, stopsAtTheEndsRatherThanWrappingAround)
{
    this->arrive(mention(u"a"_s, u"forsen"_s, u"Alice"_s, u"first"_s));
    this->arrive(mention(u"b"_s, u"pajlada"_s, u"Bob"_s, u"second"_s));

    auto *popup = this->build();

    // Wrapping from the oldest back to the newest is a good way to answer a
    // message you did not mean to.
    popup->step(-5);
    ASSERT_NE(popup->currentTarget(), nullptr);
    EXPECT_EQ(popup->currentTarget()->id, u"a"_s);

    popup->step(5);
    ASSERT_NE(popup->currentTarget(), nullptr);
    EXPECT_EQ(popup->currentTarget()->id, u"b"_s);
}

TEST_F(TestMentionsPopup, keepsAimingAtTheSameMentionWhenAnotherArrives)
{
    this->arrive(mention(u"a"_s, u"forsen"_s, u"Alice"_s, u"first"_s));
    this->arrive(mention(u"b"_s, u"pajlada"_s, u"Bob"_s, u"second"_s));

    auto *popup = this->build();
    popup->step(-1);
    ASSERT_EQ(popup->currentTarget()->id, u"a"_s);

    // Half way through typing an answer to Alice, somebody else mentions you.
    this->arrive(mention(u"c"_s, u"forsen"_s, u"Carol"_s, u"third"_s));

    ASSERT_NE(popup->currentTarget(), nullptr);
    EXPECT_EQ(popup->currentTarget()->id, u"a"_s)
        << "the reply was silently redirected at the new mention";

    ASSERT_TRUE(popup->sendForTest(u"still answering alice"_s));
    ASSERT_EQ(this->sent.size(), 1u);
    EXPECT_EQ(this->sent[0].messageId, u"a"_s);
}

TEST_F(TestMentionsPopup, offersNothingAndSaysWhyForAWhisper)
{
    auto whisper = mention(u"w"_s, u"forsen"_s, u"Alice"_s, u"psst"_s);
    whisper->flags.set(MessageFlag::Whisper);
    this->arrive(whisper);

    auto *popup = this->build();

    EXPECT_EQ(popup->currentTarget(), nullptr);
    EXPECT_TRUE(popup->statusText().contains(u"Whisper"_s))
        << "the box is disabled with no reason given: " << popup->statusText();
    EXPECT_FALSE(popup->sendForTest(u"should not go anywhere"_s));
    EXPECT_TRUE(this->sent.empty());
}

TEST_F(TestMentionsPopup, saysSoWhenTheChannelHasBeenClosed)
{
    this->arrive(mention(u"a"_s, u"somechannel"_s, u"Alice"_s, u"hello"_s));

    auto *popup = this->build();

    EXPECT_EQ(popup->currentTarget(), nullptr);
    EXPECT_TRUE(popup->statusText().contains(u"somechannel"_s))
        << popup->statusText();
    EXPECT_TRUE(this->sent.empty());
}

TEST_F(TestMentionsPopup, sendsNothingForAnEmptyBox)
{
    this->arrive(mention(u"a"_s, u"forsen"_s, u"Alice"_s, u"hello"_s));

    auto *popup = this->build();

    EXPECT_FALSE(popup->sendForTest(u"   "_s));
    EXPECT_TRUE(this->sent.empty());
}

TEST_F(TestMentionsPopup, remembersWhatWasReadSoTheCountGoesBackToZero)
{
    this->arrive(mention(u"a"_s, u"forsen"_s, u"Alice"_s, u"first"_s));
    this->arrive(mention(u"b"_s, u"pajlada"_s, u"Bob"_s, u"second"_s));

    auto *popup = this->build();
    EXPECT_EQ(unreadMentions(this->mentions->getMessageSnapshot(),
                             getSettings()->lastSeenMention.getValue()),
              2);

    popup->markRead();

    EXPECT_EQ(getSettings()->lastSeenMention.getValue(), u"b"_s);
    EXPECT_EQ(unreadMentions(this->mentions->getMessageSnapshot(),
                             getSettings()->lastSeenMention.getValue()),
              0);
}

TEST_F(TestMentionsPopup, countsAMentionThatArrivesAfterItWasRead)
{
    this->arrive(mention(u"a"_s, u"forsen"_s, u"Alice"_s, u"first"_s));

    auto *popup = this->build();
    popup->markRead();

    this->arrive(mention(u"b"_s, u"pajlada"_s, u"Bob"_s, u"second"_s));

    EXPECT_EQ(unreadMentions(this->mentions->getMessageSnapshot(),
                             getSettings()->lastSeenMention.getValue()),
              1);
}

TEST_F(TestMentionsPopup, readsTheCountThroughTheApplication)
{
    // The button in the tab bar goes through this, not through a channel handed
    // to it. If the static path cannot find the mentions channel the count is
    // silently always zero and the button never says anything.
    auto appMentions = this->mockApplication->getTwitch()->getMentionsChannel();
    ASSERT_NE(appMentions, nullptr);

    getSettings()->lastSeenMention.setValue(QString());
    EXPECT_EQ(MentionsPopup::unreadCount(), 0);

    appMentions->addMessage(mention(u"a"_s, u"forsen"_s, u"Alice"_s, u"hi"_s),
                            MessageContext::Original);
    appMentions->addMessage(mention(u"b"_s, u"forsen"_s, u"Bob"_s, u"hi"_s),
                            MessageContext::Original);

    EXPECT_EQ(MentionsPopup::unreadCount(), 2);

    getSettings()->lastSeenMention.setValue(u"b"_s);
    EXPECT_EQ(MentionsPopup::unreadCount(), 0);
}

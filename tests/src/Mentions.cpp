// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/twitch/Mentions.hpp"

#include "common/Literals.hpp"
#include "messages/Message.hpp"
#include "messages/MessageFlag.hpp"
#include "Test.hpp"

#include <QDateTime>

#include <memory>

using namespace chatterino;
using namespace literals;

namespace {

/// An ordinary mention: somebody said your name in a channel, just now.
std::shared_ptr<Message> mention(const QString &id = u"msg-1"_s,
                                const QString &channel = u"forsen"_s)
{
    auto message = std::make_shared<Message>();
    message->id = id;
    message->channelName = channel;
    message->loginName = u"bob"_s;
    message->displayName = u"Bob"_s;
    message->messageText = u"hey @you how is it going"_s;
    // Twitch refuses a reply to anything older than a day, and isReplyable()
    // reads the clock, so this has to be genuinely recent.
    message->serverReceivedTime = QDateTime::currentDateTime();

    return message;
}

const ChannelIsOpen ALL_OPEN = [](const QString &) {
    return true;
};

const ChannelIsOpen NONE_OPEN = [](const QString &) {
    return false;
};

}  // namespace

TEST(Mentions, answersAnOrdinaryMention)
{
    auto message = mention();

    EXPECT_EQ(mentionReplyProblem(*message, true), QString());

    auto target = mentionReplyTarget(*message, true);
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target->channelName, u"forsen"_s);
    EXPECT_EQ(target->messageId, u"msg-1"_s);
    EXPECT_EQ(target->userLogin, u"bob"_s);
    EXPECT_EQ(target->displayName, u"Bob"_s);
}

TEST(Mentions, refusesAWhisperRatherThanAnsweringItInAChannel)
{
    auto message = mention();
    message->flags.set(MessageFlag::Whisper);

    auto problem = mentionReplyProblem(*message, true);
    // The specific reason, not "too old": isReplyable() refuses whispers along
    // with stale messages, so asking it first would say the wrong thing about a
    // whisper that arrived a second ago.
    EXPECT_TRUE(problem.contains(u"Whisper"_s)) << problem;
    EXPECT_FALSE(mentionReplyTarget(*message, true).has_value());
}

TEST(Mentions, refusesAnAutoModNoticeAboutSomebodyElse)
{
    for (auto flag : {MessageFlag::AutoMod,
                      MessageFlag::AutoModOffendingMessage,
                      MessageFlag::AutoModOffendingMessageHeader,
                      MessageFlag::AutoModBlockedTerm})
    {
        auto message = mention();
        message->flags.set(flag);

        auto problem = mentionReplyProblem(*message, true);
        EXPECT_TRUE(problem.contains(u"AutoMod"_s))
            << "flag " << static_cast<int>(flag) << " gave: " << problem;
    }
}

TEST(Mentions, refusesASystemNotice)
{
    auto message = mention();
    message->flags.set(MessageFlag::System);

    EXPECT_FALSE(mentionReplyProblem(*message, true).isEmpty());
}

TEST(Mentions, refusesAMentionWhoseChannelIsClosed)
{
    auto message = mention();

    auto problem = mentionReplyProblem(*message, false);
    EXPECT_TRUE(problem.contains(u"forsen"_s))
        << "the reason should name the channel: " << problem;
    EXPECT_FALSE(mentionReplyTarget(*message, false).has_value());
}

TEST(Mentions, refusesAMentionWithNoChannel)
{
    auto message = mention(u"msg-1"_s, QString());

    EXPECT_FALSE(mentionReplyProblem(*message, true).isEmpty());
}

TEST(Mentions, refusesAMentionWithNoMessageId)
{
    // A reply is threaded onto an id. Sending the text without one would answer
    // a different conversation than the one being read.
    auto message = mention(QString());

    EXPECT_FALSE(mentionReplyProblem(*message, true).isEmpty());
}

TEST(Mentions, refusesAMentionTwitchWillNotAcceptAReplyTo)
{
    auto message = mention();
    message->serverReceivedTime =
        QDateTime::currentDateTime().addDays(-2);

    auto problem = mentionReplyProblem(*message, true);
    EXPECT_TRUE(problem.contains(u"old"_s)) << problem;
}

TEST(Mentions, offersTheNewestMentionFirst)
{
    std::vector<MessagePtr> mentions{
        mention(u"a"_s),
        mention(u"b"_s),
        mention(u"c"_s),
    };

    auto replyable = replyableMentions(mentions, ALL_OPEN);
    ASSERT_EQ(replyable.size(), 3u);
    // The one you want is nearly always the last that arrived, so it is what
    // the reply box offers before any stepping.
    EXPECT_EQ(replyable[0]->id, u"c"_s);
    EXPECT_EQ(replyable[2]->id, u"a"_s);
}

TEST(Mentions, leavesOutTheMentionsThatCannotBeAnswered)
{
    auto whisper = mention(u"w"_s);
    whisper->flags.set(MessageFlag::Whisper);

    auto stale = mention(u"s"_s);
    stale->serverReceivedTime = QDateTime::currentDateTime().addDays(-3);

    std::vector<MessagePtr> mentions{whisper, mention(u"ok"_s), stale};

    auto replyable = replyableMentions(mentions, ALL_OPEN);
    ASSERT_EQ(replyable.size(), 1u);
    EXPECT_EQ(replyable[0]->id, u"ok"_s);
}

TEST(Mentions, offersNothingWhenEveryChannelHasBeenClosed)
{
    std::vector<MessagePtr> mentions{mention(u"a"_s), mention(u"b"_s)};

    EXPECT_TRUE(replyableMentions(mentions, NONE_OPEN).empty());
}

TEST(Mentions, survivesANullMessageInTheList)
{
    std::vector<MessagePtr> mentions{nullptr, mention(u"a"_s), nullptr};

    auto replyable = replyableMentions(mentions, ALL_OPEN);
    ASSERT_EQ(replyable.size(), 1u);
    EXPECT_EQ(replyable[0]->id, u"a"_s);
}

TEST(Mentions, describesAMentionWellEnoughToTellWhichOneItIs)
{
    auto message = mention();

    auto described = describeMention(*message);
    EXPECT_TRUE(described.contains(u"Bob"_s)) << described;
    EXPECT_TRUE(described.contains(u"forsen"_s)) << described;
    EXPECT_TRUE(described.contains(u"how is it going"_s)) << described;
}

TEST(Mentions, cutsALongMentionDownRatherThanStretchingTheRow)
{
    auto message = mention();
    message->messageText = QString(u'x').repeated(500);

    auto described = describeMention(*message);
    EXPECT_LT(described.length(), 200)
        << "the row would be as wide as the message: " << described.length();
    EXPECT_TRUE(described.endsWith(u"…"_s)) << described;
}

TEST(Mentions, collapsesTheWhitespaceInAPreview)
{
    auto message = mention();
    message->messageText = u"hey\n\n   you   there"_s;

    EXPECT_TRUE(describeMention(*message).contains(u"hey you there"_s))
        << describeMention(*message);
}

TEST(Mentions, findsTheNewestIdToRememberAsRead)
{
    std::vector<MessagePtr> mentions{mention(u"a"_s), mention(u"b"_s)};

    EXPECT_EQ(newestMentionId(mentions), u"b"_s);
}

TEST(Mentions, skipsPastNoticesWithNoIdWhenRememberingWhatWasRead)
{
    // System notices land in the mentions list without an id. Taking the very
    // last message would remember an empty id, which reads as "nothing seen"
    // and leaves the count stuck.
    auto notice = mention(QString());
    notice->flags.set(MessageFlag::System);

    std::vector<MessagePtr> mentions{mention(u"a"_s), notice};

    EXPECT_EQ(newestMentionId(mentions), u"a"_s);
}

TEST(Mentions, hasNothingToRememberWhenTheListIsEmpty)
{
    EXPECT_EQ(newestMentionId({}), QString());
}

TEST(Mentions, countsWhatArrivedAfterTheOneLastLookedAt)
{
    std::vector<MessagePtr> mentions{
        mention(u"a"_s),
        mention(u"b"_s),
        mention(u"c"_s),
    };

    EXPECT_EQ(unreadMentions(mentions, u"c"_s), 0);
    EXPECT_EQ(unreadMentions(mentions, u"b"_s), 1);
    EXPECT_EQ(unreadMentions(mentions, u"a"_s), 2);
}

TEST(Mentions, countsEverythingBeforeAnythingHasBeenLookedAt)
{
    std::vector<MessagePtr> mentions{mention(u"a"_s), mention(u"b"_s)};

    EXPECT_EQ(unreadMentions(mentions, QString()), 2);
}

TEST(Mentions, countsEverythingWhenWhatWasReadHasScrolledAway)
{
    std::vector<MessagePtr> mentions{mention(u"a"_s), mention(u"b"_s)};

    EXPECT_EQ(unreadMentions(mentions, u"long-gone"_s), 2);
}

TEST(Mentions, labelsTheButtonWithTheCount)
{
    EXPECT_EQ(mentionButtonLabel(0), u"@"_s);
    EXPECT_EQ(mentionButtonLabel(-1), u"@"_s);
    EXPECT_EQ(mentionButtonLabel(3), u"@ 3"_s);
    EXPECT_EQ(mentionButtonLabel(mentionCountCap), u"@ 99"_s);
    // Past the cap the button would start pushing the tabs around.
    EXPECT_EQ(mentionButtonLabel(mentionCountCap + 1), u"@ 99+"_s);
    EXPECT_EQ(mentionButtonLabel(100000), u"@ 99+"_s);
}

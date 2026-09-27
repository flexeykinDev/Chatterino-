// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/twitch/MassGift.hpp"

#include "common/Literals.hpp"
#include "messages/Message.hpp"
#include "Test.hpp"

#include <IrcMessage>
#include <QStringBuilder>

#include <memory>

using namespace chatterino;
using namespace literals;

namespace {

/// Parses a raw Twitch line, so the tests read the tag names Twitch really
/// sends rather than ones handed to them.
std::optional<MassGift> announcementFrom(const QString &line)
{
    std::unique_ptr<Communi::IrcMessage> message{
        Communi::IrcMessage::fromData(line.toUtf8(), nullptr)};
    if (!message)
    {
        ADD_FAILURE() << "the line did not parse as IRC";
        return std::nullopt;
    }

    return parseMassGiftAnnouncement(message->tags());
}

template <typename F>
auto withTags(const QString &line, F &&f)
{
    std::unique_ptr<Communi::IrcMessage> message{
        Communi::IrcMessage::fromData(line.toUtf8(), nullptr)};
    EXPECT_TRUE(message) << "the line did not parse as IRC";
    return f(message->tags());
}

/// A real announcement, as Twitch sends it.
const QString ANNOUNCEMENT =
    R"(@badges=;color=;display-name=Gifter;emotes=;id=8a8f7c9c-0000-4000-8000-000000000001;login=gifter;mod=0;msg-id=submysterygift;msg-param-community-gift-id=1111111111111111111;msg-param-goal-contribution-type=SUB_POINTS;msg-param-mass-gift-count=5;msg-param-origin-id=1111111111111111111;msg-param-sender-count=137;msg-param-sub-plan=1000;room-id=11148817;subscriber=0;system-msg=Gifter\sis\sgifting\s5\sTier\s1\sSubs\sto\spajlada's\scommunity!\sThey've\sgifted\sa\stotal\sof\s137\sin\sthe\schannel!;tmi-sent-ts=1700000000000;user-id=1337;user-type= :tmi.twitch.tv USERNOTICE #pajlada)";

/// An anonymous one. Twitch attributes it to a real account whose display name
/// is "AnAnonymousGifter", which must not be shown as the gifter.
const QString ANONYMOUS_ANNOUNCEMENT =
    R"(@badges=;color=;display-name=AnAnonymousGifter;emotes=;id=8a8f7c9c-0000-4000-8000-000000000002;login=ananonymousgifter;mod=0;msg-id=submysterygift;msg-param-community-gift-id=2222222222222222222;msg-param-mass-gift-count=10;msg-param-origin-id=2222222222222222222;msg-param-sub-plan=3000;room-id=11148817;subscriber=0;system-msg=An\sanonymous\suser\sis\sgifting\s10\sTier\s3\sSubs\sto\spajlada's\scommunity!;tmi-sent-ts=1700000000000;user-id=274598607;user-type= :tmi.twitch.tv USERNOTICE #pajlada)";

/// A gift out of the pile above.
const QString GIFT_IN_PILE =
    R"(@badges=;color=;display-name=Gifter;emotes=;id=8a8f7c9c-0000-4000-8000-000000000003;login=gifter;mod=0;msg-id=subgift;msg-param-community-gift-id=1111111111111111111;msg-param-gift-months=1;msg-param-months=1;msg-param-origin-id=1111111111111111111;msg-param-recipient-display-name=Lucky;msg-param-recipient-id=555;msg-param-recipient-user-name=lucky;msg-param-sender-count=0;msg-param-sub-plan=1000;room-id=11148817;subscriber=0;system-msg=Gifter\sgifted\sa\sTier\s1\ssub\sto\sLucky!;tmi-sent-ts=1700000000001;user-id=1337;user-type= :tmi.twitch.tv USERNOTICE #pajlada)";

/// A single gift that nobody piled. It still carries a community gift id,
/// which is exactly why the id alone cannot mean "part of a pile".
const QString LONE_GIFT =
    R"(@badges=;color=;display-name=Solo;emotes=;id=8a8f7c9c-0000-4000-8000-000000000004;login=solo;mod=0;msg-id=subgift;msg-param-community-gift-id=9999999999999999999;msg-param-gift-months=1;msg-param-months=1;msg-param-origin-id=9999999999999999999;msg-param-recipient-display-name=Friend;msg-param-recipient-id=666;msg-param-recipient-user-name=friend;msg-param-sender-count=0;msg-param-sub-plan=1000;room-id=11148817;subscriber=0;system-msg=Solo\sgifted\sa\sTier\s1\ssub\sto\sFriend!;tmi-sent-ts=1700000000002;user-id=42;user-type= :tmi.twitch.tv USERNOTICE #pajlada)";

const QString FIRST_ID = u"1111111111111111111"_s;
const QString SECOND_ID = u"2222222222222222222"_s;

MassGift giftWith(int recipients)
{
    MassGift gift;
    gift.id = FIRST_ID;
    gift.promisedCount = recipients;
    for (int i = 0; i < recipients; i++)
    {
        gift.recipients.push_back({
            .login = QStringLiteral("user%1").arg(i),
            .displayName = QStringLiteral("User%1").arg(i),
            .userId = QString::number(i),
        });
    }

    return gift;
}

}  // namespace

TEST(MassGift, readsAnAnnouncement)
{
    auto gift = announcementFrom(ANNOUNCEMENT);
    ASSERT_TRUE(gift.has_value());

    EXPECT_EQ(gift->id, FIRST_ID);
    EXPECT_EQ(gift->gifterLogin, u"gifter"_s);
    EXPECT_EQ(gift->gifterDisplayName, u"Gifter"_s);
    EXPECT_FALSE(gift->anonymous);
    EXPECT_EQ(gift->tier, 1);
    // Five, not 137: the sender count beside it is a lifetime total.
    EXPECT_EQ(gift->promisedCount, 5);
    EXPECT_TRUE(gift->announcement.contains(u"is gifting 5 Tier 1 Subs"_s));
    // The escaped spaces in the tag have been unescaped.
    EXPECT_FALSE(gift->announcement.contains(u"\\s"_s));
    EXPECT_EQ(gift->messageId, u"8a8f7c9c-0000-4000-8000-000000000001"_s);
}

TEST(MassGift, recognisesTheAnonymousGifter)
{
    auto gift = announcementFrom(ANONYMOUS_ANNOUNCEMENT);
    ASSERT_TRUE(gift.has_value());

    EXPECT_TRUE(gift->anonymous);
    EXPECT_EQ(gift->tier, 3);
    EXPECT_EQ(gift->promisedCount, 10);
}

TEST(MassGift, ignoresAnythingThatIsNotAnAnnouncement)
{
    EXPECT_FALSE(announcementFrom(GIFT_IN_PILE).has_value());
    EXPECT_FALSE(announcementFrom(LONE_GIFT).has_value());
}

TEST(MassGift, refusesAnAnnouncementWithNoGiftId)
{
    // Without an id the individual gifts cannot be matched, and folding on the
    // gifter's name instead would swallow somebody else's gifts.
    auto line = QString(ANNOUNCEMENT)
                    .replace(u"msg-param-community-gift-id=" % FIRST_ID % u";",
                             QString())
                    .replace(u"msg-param-origin-id=" % FIRST_ID % u";",
                             QString());

    EXPECT_FALSE(announcementFrom(line).has_value());
}

TEST(MassGift, fallsBackToTheOriginIdOnOlderMessages)
{
    auto line =
        QString(ANNOUNCEMENT)
            .replace(u"msg-param-community-gift-id=" % FIRST_ID % u";",
                     QString());

    auto gift = announcementFrom(line);
    ASSERT_TRUE(gift.has_value());
    EXPECT_EQ(gift->id, FIRST_ID);
}

TEST(MassGift, readsAGiftsRecipient)
{
    auto recipient = withTags(GIFT_IN_PILE, [](auto tags) {
        return parseMassGiftRecipient(tags);
    });

    EXPECT_EQ(recipient.login, u"lucky"_s);
    EXPECT_EQ(recipient.displayName, u"Lucky"_s);
    EXPECT_EQ(recipient.userId, u"555"_s);
}

TEST(MassGift, readsTheOlderRecipientNameTag)
{
    // Twitch sent `msg-param-recipient-name` before renaming it, and cached
    // history still carries the old spelling.
    auto line = QString(GIFT_IN_PILE)
                    .replace(u"msg-param-recipient-user-name="_s,
                             u"msg-param-recipient-name="_s);

    auto recipient = withTags(line, [](auto tags) {
        return parseMassGiftRecipient(tags);
    });

    EXPECT_EQ(recipient.login, u"lucky"_s);
}

TEST(MassGift, readsTheTierAndNeverAnswersPrime)
{
    EXPECT_EQ(massGiftTier(u"1000"_s), 1);
    EXPECT_EQ(massGiftTier(u"2000"_s), 2);
    EXPECT_EQ(massGiftTier(u"3000"_s), 3);
    // Prime subs cannot be gifted, but taking the first character of the plan
    // — which the single-sub path does — would answer 'P' here.
    EXPECT_EQ(massGiftTier(u"Prime"_s), 1);
    EXPECT_EQ(massGiftTier(QString()), 1);
    EXPECT_EQ(massGiftTier(u"9000"_s), 1);
}

TEST(MassGiftTracker, absorbsGiftsBelongingToAnAnnouncement)
{
    MassGiftTracker tracker;
    auto announced = announcementFrom(ANNOUNCEMENT);
    ASSERT_TRUE(announced.has_value());
    tracker.announce(*announced);

    auto giftId = withTags(GIFT_IN_PILE, [](auto tags) {
        return massGiftIdOf(tags);
    });
    auto recipient = withTags(GIFT_IN_PILE, [](auto tags) {
        return parseMassGiftRecipient(tags);
    });

    auto *gift = tracker.absorb(giftId, recipient);
    ASSERT_NE(gift, nullptr);
    ASSERT_EQ(gift->recipients.size(), 1u);
    EXPECT_EQ(gift->recipients.front().displayName, u"Lucky"_s);
}

TEST(MassGiftTracker, leavesALoneGiftAlone)
{
    MassGiftTracker tracker;
    auto announced = announcementFrom(ANNOUNCEMENT);
    ASSERT_TRUE(announced.has_value());
    tracker.announce(*announced);

    // Carries a community gift id, but not one that was announced.
    auto giftId = withTags(LONE_GIFT, [](auto tags) {
        return massGiftIdOf(tags);
    });
    auto recipient = withTags(LONE_GIFT, [](auto tags) {
        return parseMassGiftRecipient(tags);
    });

    EXPECT_EQ(tracker.absorb(giftId, recipient), nullptr);
}

TEST(MassGiftTracker, absorbsNothingWithoutAnId)
{
    MassGiftTracker tracker;
    tracker.announce(*announcementFrom(ANNOUNCEMENT));

    EXPECT_EQ(tracker.absorb(QString(), {}), nullptr);
}

TEST(MassGiftTracker, keepsGiftsApartWhenTwoPeopleGiftAtOnce)
{
    MassGiftTracker tracker;
    tracker.announce(*announcementFrom(ANNOUNCEMENT));
    tracker.announce(*announcementFrom(ANONYMOUS_ANNOUNCEMENT));

    tracker.absorb(FIRST_ID, {.displayName = u"A"_s});
    tracker.absorb(SECOND_ID, {.displayName = u"B"_s});
    tracker.absorb(SECOND_ID, {.displayName = u"C"_s});

    auto *first = tracker.find(FIRST_ID);
    auto *second = tracker.find(SECOND_ID);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(first->recipients.size(), 1u);
    EXPECT_EQ(second->recipients.size(), 2u);
}

TEST(MassGiftTracker, stopsAbsorbingOnceAGiftHasGoneQuiet)
{
    auto now = std::chrono::steady_clock::now();
    MassGiftTracker tracker([&now] {
        return now;
    });
    tracker.announce(*announcementFrom(ANNOUNCEMENT));

    now += MassGiftTracker::idleFor - std::chrono::milliseconds(1);
    EXPECT_NE(tracker.absorb(FIRST_ID, {}), nullptr);

    // The absorb above moved the gift's clock on, so the wait starts again.
    now += MassGiftTracker::idleFor;
    EXPECT_EQ(tracker.absorb(FIRST_ID, {}), nullptr);
}

TEST(MassGiftTracker, holdsOnlySoManyGiftsAtOnce)
{
    MassGiftTracker tracker;
    for (std::size_t i = 0; i < MassGiftTracker::maxOpen + 3; i++)
    {
        MassGift gift;
        gift.id = QString::number(i);
        tracker.announce(gift);
    }

    EXPECT_EQ(tracker.openCount(), MassGiftTracker::maxOpen);
    // The oldest went, the newest stayed.
    EXPECT_EQ(tracker.find(u"0"_s), nullptr);
    EXPECT_NE(tracker.find(QString::number(MassGiftTracker::maxOpen + 2)),
              nullptr);
}

TEST(MassGiftTracker, keepsRecipientsWhenAnAnnouncementRepeats)
{
    MassGiftTracker tracker;
    tracker.announce(*announcementFrom(ANNOUNCEMENT));
    tracker.absorb(FIRST_ID, {.displayName = u"A"_s});

    auto *gift = tracker.announce(*announcementFrom(ANNOUNCEMENT));
    ASSERT_NE(gift, nullptr);
    EXPECT_EQ(gift->recipients.size(), 1u);
}

TEST(MassGiftTracker, remembersWhichMessageStandsInForAGift)
{
    MassGiftTracker tracker;
    tracker.announce(*announcementFrom(ANNOUNCEMENT));

    EXPECT_EQ(tracker.summaryOf(FIRST_ID), nullptr);

    auto message = std::make_shared<const Message>();
    tracker.setSummary(FIRST_ID, message);
    EXPECT_EQ(tracker.summaryOf(FIRST_ID), message);
    EXPECT_EQ(tracker.summaryOf(u"nope"_s), nullptr);
}

TEST(MassGift, namesEveryRecipientWhileThereAreFewEnough)
{
    auto gift = giftWith(3);
    auto view = massGiftRecipientsView(gift);

    EXPECT_EQ(view.shown.size(), 3u);
    EXPECT_EQ(view.more, 0);
    EXPECT_EQ(massGiftRecipientsList(gift), u"User0, User1, User2"_s);
}

TEST(MassGift, saysHowManyItLeftOutOnceThereAreTooMany)
{
    auto gift =
        giftWith(static_cast<int>(MassGiftTracker::shownRecipients) + 8);
    auto view = massGiftRecipientsView(gift);

    EXPECT_EQ(view.shown.size(), MassGiftTracker::shownRecipients);
    EXPECT_EQ(view.more, 8);

    auto list = massGiftRecipientsList(gift);
    EXPECT_TRUE(list.contains(u"8"_s))
        << "the leftover count is missing from: " << list;
    EXPECT_FALSE(list.contains(
        u"User"_s % QString::number(MassGiftTracker::shownRecipients)))
        << "a recipient past the cap was named: " << list;
}

TEST(MassGift, saysNothingBeforeAnyGiftHasArrived)
{
    EXPECT_TRUE(massGiftRecipientsList(giftWith(0)).isEmpty());
    EXPECT_TRUE(massGiftRecipientsText(giftWith(0)).isEmpty());
}

TEST(MassGift, leavesAPlaceholderForTheNamesInItsSentence)
{
    // The built message splits this template to put clickable names where the
    // placeholder is; a translation that drops it would silently lose them.
    EXPECT_TRUE(massGiftRecipientsTemplate().contains(u"%1"_s));
}

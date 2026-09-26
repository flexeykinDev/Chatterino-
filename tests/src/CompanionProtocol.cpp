// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/companion/CompanionProtocol.hpp"

#include <gtest/gtest.h>
#include <QJsonDocument>
#include <QJsonObject>

using namespace chatterino;

namespace {

/// Decodes a frame this client sent, so the tests can assert on its shape
/// rather than on an exact byte sequence.
QJsonObject sent(const QByteArray &payload)
{
    QJsonParseError error{};
    auto document = QJsonDocument::fromJson(payload, &error);
    EXPECT_EQ(error.error, QJsonParseError::NoError)
        << error.errorString().toStdString();
    return document.object();
}

template <typename T>
const T *as(const std::optional<CompanionFrame> &frame)
{
    if (!frame.has_value())
    {
        return nullptr;
    }

    return std::get_if<T>(&*frame);
}

}  // namespace

// --- what we send ------------------------------------------------------

TEST(CompanionFramesOut, helloCarriesTheTokenAndPresenceChoice)
{
    auto frame = sent(companionFrames::hello("tok", "2.5.5", true));

    EXPECT_EQ(frame.value("t").toString(), "hello");
    EXPECT_EQ(frame.value("token").toString(), "tok");
    EXPECT_EQ(frame.value("version").toString(), "2.5.5");
    EXPECT_TRUE(frame.value("hidePresence").toBool());
}

TEST(CompanionFramesOut, joinAndPartNameTheChannelById)
{
    EXPECT_EQ(sent(companionFrames::join("11")).value("channel").toString(),
              "11");
    EXPECT_EQ(sent(companionFrames::part("11")).value("t").toString(), "part");
}

TEST(CompanionFramesOut, sayCarriesItsNonce)
{
    auto frame = sent(companionFrames::say("11", "hello", "n1"));

    EXPECT_EQ(frame.value("body").toString(), "hello");
    EXPECT_EQ(frame.value("nonce").toString(), "n1");
}

TEST(CompanionFramesOut, sayOmitsAnEmptyNonceRatherThanSendingOne)
{
    // The server validates a nonce as a non-empty string, so sending "" is a
    // malformed frame rather than an absent nonce.
    auto frame = sent(companionFrames::say("11", "hello", ""));

    EXPECT_FALSE(frame.contains("nonce"));
}

TEST(CompanionFramesOut, typingSaysWhetherItStartedOrStopped)
{
    EXPECT_TRUE(sent(companionFrames::typing("11", true))
                    .value("active")
                    .toBool());
    EXPECT_FALSE(sent(companionFrames::typing("11", false))
                     .value("active")
                     .toBool());
}

TEST(CompanionFramesOut, beatIsJustItsType)
{
    auto frame = sent(companionFrames::beat());

    EXPECT_EQ(frame.value("t").toString(), "beat");
    EXPECT_EQ(frame.size(), 1);
}

// --- what we receive ---------------------------------------------------

TEST(CompanionFramesIn, readsReady)
{
    auto frame = parseCompanionFrame(
        R"({"t":"ready","user":{"id":"1","login":"someone",
            "displayName":"SomeOne"}})");

    auto *ready = as<CompanionReady>(frame);
    ASSERT_NE(ready, nullptr);
    EXPECT_EQ(ready->user.login, "someone");
    EXPECT_EQ(ready->user.preferredName(), "SomeOne");
}

TEST(CompanionFramesIn, fallsBackToTheLoginWhenThereIsNoDisplayName)
{
    CompanionUser user{.id = "1", .login = "someone", .displayName = ""};

    EXPECT_EQ(user.preferredName(), "someone");
}

TEST(CompanionFramesIn, readsTyping)
{
    auto frame = parseCompanionFrame(
        R"({"t":"typing","channel":"11","login":"mod","active":false})");

    auto *typing = as<CompanionTyping>(frame);
    ASSERT_NE(typing, nullptr);
    EXPECT_EQ(typing->channel, "11");
    EXPECT_EQ(typing->login, "mod");
    EXPECT_FALSE(typing->active);
}

TEST(CompanionFramesIn, typingWithoutActiveMeansStarted)
{
    // The server leaves `active` out in the affirmative case, so a missing
    // field must not read as "stopped" — that would make the indicator vanish
    // the moment it should appear.
    auto frame =
        parseCompanionFrame(R"({"t":"typing","channel":"11","login":"mod"})");

    auto *typing = as<CompanionTyping>(frame);
    ASSERT_NE(typing, nullptr);
    EXPECT_TRUE(typing->active);
}

TEST(CompanionFramesIn, readsAMessage)
{
    auto frame = parseCompanionFrame(
        R"({"t":"message","id":7,"channel":"11",
            "author":{"id":"2","login":"someone","displayName":"SomeOne"},
            "body":"hello","sentAt":1700000000000})");

    auto *said = as<CompanionSaid>(frame);
    ASSERT_NE(said, nullptr);
    EXPECT_EQ(said->message.id, 7);
    EXPECT_EQ(said->message.body, "hello");
    EXPECT_EQ(said->message.author.login, "someone");
    EXPECT_EQ(said->message.sentAt.toMSecsSinceEpoch(), 1700000000000);
}

TEST(CompanionFramesIn, dropsAMessageWithNobodyBehindIt)
{
    // Unattributable text cannot be moderated or replied to, so it is better
    // dropped than shown as coming from nowhere.
    EXPECT_FALSE(parseCompanionFrame(
        R"({"t":"message","id":7,"channel":"11","body":"hello"})"));
    EXPECT_FALSE(parseCompanionFrame(
        R"({"t":"message","id":7,"channel":"11","author":{"id":"2"},
            "body":"hello"})"));
}

TEST(CompanionFramesIn, keepsTheReadableHistoryAndSkipsTheRest)
{
    auto frame = parseCompanionFrame(
        R"({"t":"joined","channel":"11","history":[
            {"id":1,"channel":"11","author":{"id":"2","login":"a"},
             "body":"first","sentAt":1700000000000},
            {"id":2,"channel":"11","body":"orphaned"},
            "nonsense",
            {"id":3,"channel":"11","author":{"id":"3","login":"b"},
             "body":"third","sentAt":1700000001000}
        ]})");

    auto *joined = as<CompanionJoined>(frame);
    ASSERT_NE(joined, nullptr);
    ASSERT_EQ(joined->history.size(), 2u);
    EXPECT_EQ(joined->history[0].body, "first");
    EXPECT_EQ(joined->history[1].body, "third");
}

TEST(CompanionFramesIn, readsARestrictionAndItsExpiry)
{
    auto frame = parseCompanionFrame(
        R"({"t":"restricted","channel":"11","until":1700000000000,
            "reason":"spam"})");

    auto *restricted = as<CompanionRestricted>(frame);
    ASSERT_NE(restricted, nullptr);
    ASSERT_TRUE(restricted->until.has_value());
    EXPECT_EQ(restricted->until->toMSecsSinceEpoch(), 1700000000000);
    EXPECT_EQ(restricted->reason, "spam");
}

TEST(CompanionFramesIn, aNullExpiryIsAPermanentBanNotAMissingField)
{
    auto frame = parseCompanionFrame(
        R"({"t":"restricted","channel":"11","until":null,"reason":"banned"})");

    auto *restricted = as<CompanionRestricted>(frame);
    ASSERT_NE(restricted, nullptr);
    EXPECT_FALSE(restricted->until.has_value());
}

TEST(CompanionFramesIn, readsAnErrorWithTheMessageItIsAbout)
{
    auto frame = parseCompanionFrame(
        R"({"t":"error","code":"restricted","message":"you are banned",
            "nonce":"n1"})");

    auto *error = as<CompanionError>(frame);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, "restricted");
    EXPECT_EQ(error->nonce, "n1");
}

TEST(CompanionFramesIn, readsAck)
{
    auto frame = parseCompanionFrame(R"({"t":"ack","nonce":"n1","id":42})");

    auto *ack = as<CompanionAck>(frame);
    ASSERT_NE(ack, nullptr);
    EXPECT_EQ(ack->nonce, "n1");
    EXPECT_EQ(ack->id, 42);
}

TEST(CompanionFramesIn, readsParted)
{
    auto *parted = as<CompanionParted>(
        parseCompanionFrame(R"({"t":"parted","channel":"11"})"));

    ASSERT_NE(parted, nullptr);
    EXPECT_EQ(parted->channel, "11");
}

TEST(CompanionFramesIn, ignoresAFrameTypeThisBuildDoesNotKnow)
{
    // A server that learns a new frame should not break a client that has no
    // use for it, so this is nothing rather than a failed connection.
    EXPECT_FALSE(parseCompanionFrame(R"({"t":"somethingNew","x":1})"));
}

TEST(CompanionFramesIn, survivesRubbish)
{
    EXPECT_FALSE(parseCompanionFrame(""));
    EXPECT_FALSE(parseCompanionFrame("not json"));
    EXPECT_FALSE(parseCompanionFrame("[]"));
    EXPECT_FALSE(parseCompanionFrame("{}"));
    EXPECT_FALSE(parseCompanionFrame(R"({"t":123})"));
}

TEST(CompanionFramesIn, rejectsFramesMissingWhatTheyAreAbout)
{
    EXPECT_FALSE(parseCompanionFrame(R"({"t":"joined"})"));
    EXPECT_FALSE(parseCompanionFrame(R"({"t":"parted"})"));
    EXPECT_FALSE(parseCompanionFrame(R"({"t":"typing","channel":"11"})"));
    EXPECT_FALSE(parseCompanionFrame(R"({"t":"ack","id":1})"));
    EXPECT_FALSE(parseCompanionFrame(R"({"t":"ready"})"));
}

// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/companion/ShadowChannel.hpp"

#include "Application.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "providers/companion/CompanionController.hpp"
#include "providers/companion/CompanionProtocol.hpp"
#include "providers/companion/CompanionSocket.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"

namespace chatterino {

const QString ShadowChannel::prefix = QStringLiteral("shadow:");

ShadowChannel::ShadowChannel(const QString &login)
    : Channel(ShadowChannel::prefix + login, Channel::Type::Misc)
    , login_(login)
{
    auto *companion = getApp()->getCompanion();
    if (companion == nullptr)
    {
        return;
    }

    auto *socket = companion->socket();

    this->socketConnections_.push_back(QObject::connect(
        socket, &CompanionSocket::messageReceived, socket,
        [this](const CompanionMessage &message) {
            if (message.channel == this->roomId_)
            {
                this->addCompanionMessage(message);
            }
        }));

    this->socketConnections_.push_back(QObject::connect(
        socket, &CompanionSocket::roomJoined, socket,
        [this](const QString &channelId,
               const std::vector<CompanionMessage> &history) {
            if (channelId != this->roomId_)
            {
                return;
            }

            // Backfill is what makes this a room rather than a broadcast: the
            // conversation was going on before this window opened. A rejoin
            // sends it all again, though, so only what is new is shown.
            for (const auto &message : history)
            {
                if (!this->backlog_.isNew(message.id))
                {
                    continue;
                }

                this->addCompanionMessage(message);
            }
        }));

    this->socketConnections_.push_back(QObject::connect(
        socket, &CompanionSocket::errorReceived, socket,
                     [this](const QString &code, const QString &message,
                            const QString &nonce) {
                         auto sent = this->pending_.take(nonce);
                         if (sent.isEmpty())
                         {
                             return;
                         }

                         // Named, because "rate limited" on its own leaves
                         // somebody wondering which of three messages vanished.
                         this->addSystemMessage(
                             tr("Not sent (%1): %2").arg(code, sent));
                     }));

    this->socketConnections_.push_back(QObject::connect(
        socket, &CompanionSocket::messageAccepted, socket,
                     [this](const QString &nonce, qint64 id) {
                         this->pending_.remove(nonce);

                         // Own messages are shown before the server has given
                         // them an id, so it has to be learned here or a
                         // reconnect replays them as unseen history.
                         this->backlog_.seen(id);
                     }));

    this->socketConnections_.push_back(QObject::connect(
        socket, &CompanionSocket::restricted, socket,
                     [this](const QString &channelId, const QString &reason) {
                         if (channelId != this->roomId_)
                         {
                             return;
                         }

                         this->addSystemMessage(
                             reason.isEmpty()
                                 ? tr("You can no longer speak in this room.")
                                 : tr("You can no longer speak in this room: "
                                      "%1")
                                       .arg(reason));
                     }));

}

void ShadowChannel::initialize()
{
    this->resolveRoom();
}

ShadowChannel::~ShadowChannel()
{
    for (const auto &connection : this->socketConnections_)
    {
        QObject::disconnect(connection);
    }

    auto *companion = getApp()->getCompanion();
    if (companion != nullptr && !this->roomId_.isEmpty())
    {
        companion->unwatchChannel(this->roomId_);
    }
}

const QString &ShadowChannel::twitchLogin() const
{
    return this->login_;
}

void ShadowChannel::resolveRoom()
{
    auto twitch = getApp()->getTwitch()->getChannelOrEmpty(this->login_);
    auto *twitchChannel = dynamic_cast<TwitchChannel *>(twitch.get());
    if (twitchChannel == nullptr)
    {
        this->addSystemMessage(
            tr("Open %1 as well: this room follows that channel, and needs it "
               "to know which room it is.")
                .arg(this->login_));
        return;
    }

    auto roomId = twitchChannel->roomId();
    if (roomId.isEmpty())
    {
        // The id arrives with ROOMSTATE, after the channel object exists.
        this->signalHolder_.managedConnect(twitchChannel->roomIdSet, [this] {
            this->resolveRoom();
        });
        return;
    }

    if (roomId == this->roomId_)
    {
        return;
    }

    this->roomId_ = roomId;

    if (auto *companion = getApp()->getCompanion(); companion != nullptr)
    {
        companion->watchChannel(this->roomId_);
    }
}

bool ShadowChannel::canSendMessage() const
{
    return true;
}

void ShadowChannel::sendMessage(const QString &message)
{
    auto trimmed = message.trimmed();
    if (trimmed.isEmpty())
    {
        return;
    }

    auto *companion = getApp()->getCompanion();
    if (companion == nullptr || this->roomId_.isEmpty())
    {
        this->addSystemMessage(
            tr("This room is not connected, so nothing was sent."));
        return;
    }

    auto nonce = companion->socket()->say(this->roomId_, trimmed);
    if (!nonce)
    {
        // Deliberately not queued. A message delivered minutes later arrives
        // in a conversation that has moved on, which is worse than being told
        // it did not send.
        this->addSystemMessage(
            tr("Not connected, so nothing was sent: %1").arg(trimmed));
        return;
    }

    this->pending_.insert(*nonce, trimmed);

    // Shown at once rather than on acknowledgement. The round trip is short
    // and a message box that seems to swallow what was typed feels broken; a
    // refusal arrives as its own line naming the message it is about.
    auto account = getApp()->getAccounts()->twitch.getCurrent();
    CompanionMessage own{
        .id = 0,
        .channel = this->roomId_,
        .author =
            {
                .id = account ? account->getUserId() : QString{},
                .login = account ? account->getUserName() : QString{},
                .displayName = account ? account->getUserName() : QString{},
            },
        .body = trimmed,
        .sentAt = QDateTime::currentDateTime(),
    };
    this->addCompanionMessage(own);
}

void ShadowChannel::addCompanionMessage(const CompanionMessage &message)
{
    this->backlog_.seen(message.id);

    MessageBuilder builder;
    builder.emplace<TimestampElement>(message.sentAt.time());
    builder
        .emplace<TextElement>(message.author.preferredName() + ":",
                              MessageElementFlag::Username,
                              MessageColor::System, FontStyle::ChatMediumBold)
        ->setLink({Link::UserInfo, message.author.login});

    builder.emplace<TextElement>(message.body, MessageElementFlag::Text,
                                 MessageColor::Text);

    builder.message().loginName = message.author.login;
    builder.message().displayName = message.author.preferredName();
    builder.message().messageText = message.body;
    builder.message().searchText = message.author.login + ": " + message.body;
    builder.message().serverReceivedTime = message.sentAt;

    this->addMessage(builder.release(), MessageContext::Original);
}

void ShadowChannel::addSystemMessage(const QString &text)
{
    Channel::addSystemMessage(text);
}

}  // namespace chatterino

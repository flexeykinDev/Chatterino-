// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/companion/CompanionProtocol.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace {

using namespace chatterino;

QByteArray encode(const QJsonObject &frame)
{
    return QJsonDocument(frame).toJson(QJsonDocument::Compact);
}

std::optional<QDateTime> parseEpochMillis(const QJsonValue &value)
{
    if (!value.isDouble())
    {
        return std::nullopt;
    }

    auto millis = static_cast<qint64>(value.toDouble());
    if (millis <= 0)
    {
        return std::nullopt;
    }

    return QDateTime::fromMSecsSinceEpoch(millis);
}

std::optional<CompanionUser> parseUser(const QJsonValue &value)
{
    if (!value.isObject())
    {
        return std::nullopt;
    }

    auto object = value.toObject();
    auto id = object.value("id").toString();
    auto login = object.value("login").toString();

    // A message with nobody behind it cannot be attributed or moderated, so it
    // is better dropped than shown as coming from nowhere.
    if (id.isEmpty() || login.isEmpty())
    {
        return std::nullopt;
    }

    return CompanionUser{
        .id = id,
        .login = login,
        .displayName = object.value("displayName").toString(),
    };
}

std::optional<CompanionMessage> parseMessage(const QJsonObject &object)
{
    auto author = parseUser(object.value("author"));
    auto channel = object.value("channel").toString();
    auto body = object.value("body").toString();

    if (!author || channel.isEmpty() || body.isEmpty())
    {
        return std::nullopt;
    }

    return CompanionMessage{
        .id = static_cast<qint64>(object.value("id").toDouble()),
        .channel = channel,
        .author = *author,
        .body = body,
        .sentAt = parseEpochMillis(object.value("sentAt")).value_or(
            QDateTime::currentDateTime()),
    };
}

}  // namespace

namespace chatterino {

QString CompanionUser::preferredName() const
{
    return this->displayName.isEmpty() ? this->login : this->displayName;
}

std::optional<CompanionFrame> parseCompanionFrame(const QByteArray &payload)
{
    QJsonParseError error{};
    auto document = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::nullopt;
    }

    auto object = document.object();
    auto type = object.value("t").toString();

    if (type == "ready")
    {
        auto user = parseUser(object.value("user"));
        if (!user)
        {
            return std::nullopt;
        }

        return CompanionReady{.user = *user};
    }

    if (type == "joined")
    {
        auto channel = object.value("channel").toString();
        if (channel.isEmpty())
        {
            return std::nullopt;
        }

        CompanionJoined joined{.channel = channel, .history = {}};
        for (const auto &entry : object.value("history").toArray())
        {
            if (!entry.isObject())
            {
                continue;
            }

            // One unreadable line of history is not worth discarding the rest
            // of the room's backlog over.
            if (auto message = parseMessage(entry.toObject()))
            {
                joined.history.push_back(std::move(*message));
            }
        }

        return joined;
    }

    if (type == "parted")
    {
        auto channel = object.value("channel").toString();
        if (channel.isEmpty())
        {
            return std::nullopt;
        }

        return CompanionParted{.channel = channel};
    }

    if (type == "message")
    {
        auto message = parseMessage(object);
        if (!message)
        {
            return std::nullopt;
        }

        return CompanionSaid{.message = *message};
    }

    if (type == "ack")
    {
        auto nonce = object.value("nonce").toString();
        if (nonce.isEmpty())
        {
            return std::nullopt;
        }

        return CompanionAck{
            .nonce = nonce,
            .id = static_cast<qint64>(object.value("id").toDouble()),
        };
    }

    if (type == "typing")
    {
        auto channel = object.value("channel").toString();
        auto login = object.value("login").toString();
        if (channel.isEmpty() || login.isEmpty())
        {
            return std::nullopt;
        }

        return CompanionTyping{
            .channel = channel,
            .login = login,
            // Absent means started typing: the server only omits it in the
            // affirmative case.
            .active = object.value("active").toBool(true),
        };
    }

    if (type == "restricted")
    {
        auto channel = object.value("channel").toString();
        if (channel.isEmpty())
        {
            return std::nullopt;
        }

        return CompanionRestricted{
            .channel = channel,
            // A null `until` is a permanent ban, not a missing field.
            .until = parseEpochMillis(object.value("until")),
            .reason = object.value("reason").toString(),
        };
    }

    if (type == "error")
    {
        return CompanionError{
            .code = object.value("code").toString(),
            .message = object.value("message").toString(),
            .nonce = object.value("nonce").toString(),
        };
    }

    return std::nullopt;
}

namespace companionFrames {

QByteArray hello(const QString &token, const QString &version,
                 bool hidePresence)
{
    return encode({
        {"t", "hello"},
        {"token", token},
        {"version", version},
        {"hidePresence", hidePresence},
    });
}

QByteArray join(const QString &channelId)
{
    return encode({{"t", "join"}, {"channel", channelId}});
}

QByteArray part(const QString &channelId)
{
    return encode({{"t", "part"}, {"channel", channelId}});
}

QByteArray say(const QString &channelId, const QString &body,
               const QString &nonce)
{
    QJsonObject frame{
        {"t", "say"},
        {"channel", channelId},
        {"body", body},
    };

    if (!nonce.isEmpty())
    {
        frame.insert("nonce", nonce);
    }

    return encode(frame);
}

QByteArray typing(const QString &channelId, bool active)
{
    return encode({
        {"t", "typing"},
        {"channel", channelId},
        {"active", active},
    });
}

QByteArray beat()
{
    return encode({{"t", "beat"}});
}

}  // namespace companionFrames

}  // namespace chatterino

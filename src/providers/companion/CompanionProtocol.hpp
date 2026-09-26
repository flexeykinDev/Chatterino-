// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>

#include <optional>
#include <variant>
#include <vector>

namespace chatterino {

/// Who sent a shadow chat message.
struct CompanionUser {
    QString id;
    QString login;
    QString displayName;

    /// The name to show: the display name when there is one, the login
    /// otherwise. A display name that is only a different capitalisation of the
    /// login is still the better choice, since that is what the person chose.
    [[nodiscard]] QString preferredName() const;
};

struct CompanionMessage {
    qint64 id = 0;
    QString channel;
    CompanionUser author;
    QString body;
    QDateTime sentAt;
};

/// The socket authenticated; the server says who we are.
struct CompanionReady {
    CompanionUser user;
};

/// A room was joined, with whatever was said in it recently.
struct CompanionJoined {
    QString channel;
    std::vector<CompanionMessage> history;
};

struct CompanionParted {
    QString channel;
};

/// A message was sent to a room we are in.
struct CompanionSaid {
    CompanionMessage message;
};

/// Our own message was accepted, matched to the nonce we sent with it.
struct CompanionAck {
    QString nonce;
    qint64 id = 0;
};

/// Somebody started or stopped typing in a room.
struct CompanionTyping {
    QString channel;
    QString login;
    bool active = true;
};

/// We are timed out or banned from a room's shadow chat.
struct CompanionRestricted {
    QString channel;
    /// When the restriction lifts, or nothing when it is permanent.
    std::optional<QDateTime> until;
    QString reason;
};

struct CompanionError {
    QString code;
    QString message;
    /// Set when the error is about a particular message we sent.
    QString nonce;
};

using CompanionFrame =
    std::variant<CompanionReady, CompanionJoined, CompanionParted,
                 CompanionSaid, CompanionAck, CompanionTyping,
                 CompanionRestricted, CompanionError>;

/// Parses one frame from the socket.
///
/// Returns nothing for anything unrecognised rather than failing the
/// connection: a server that has learned a new frame type should not break an
/// older client that simply has no use for it.
std::optional<CompanionFrame> parseCompanionFrame(const QByteArray &payload);

/// The frames this client sends. Kept together so the wire format lives in one
/// place rather than being assembled at each call site.
namespace companionFrames {

/// Must be the first frame on a connection.
QByteArray hello(const QString &token, const QString &version,
                 bool hidePresence);
QByteArray join(const QString &channelId);
QByteArray part(const QString &channelId);
QByteArray say(const QString &channelId, const QString &body,
               const QString &nonce);
/// `active` false says the input box was emptied.
QByteArray typing(const QString &channelId, bool active);
QByteArray beat();

}  // namespace companionFrames

}  // namespace chatterino

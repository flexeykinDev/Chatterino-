// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/websockets/WebSocketPool.hpp"
#include "providers/companion/CompanionProtocol.hpp"

#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

#include <memory>
#include <optional>

namespace chatterino {

/// The realtime half of the companion service.
///
/// Shadow chat, presence and the moderator typing indicator are all the same
/// socket carrying different frames, so this owns the connection and hands the
/// frames out as signals rather than knowing what any of them are for.
///
/// Everything a caller touches happens on the GUI thread. The pool delivers on
/// its own thread, so incoming frames are marshalled across before anyone sees
/// them — a signal that fired on the socket thread would be a data race in
/// whichever widget happened to be connected.
class CompanionSocket : public QObject
{
    Q_OBJECT

public:
    CompanionSocket();
    ~CompanionSocket() override;

    CompanionSocket(const CompanionSocket &) = delete;
    CompanionSocket &operator=(const CompanionSocket &) = delete;
    CompanionSocket(CompanionSocket &&) = delete;
    CompanionSocket &operator=(CompanionSocket &&) = delete;

    /// Points the socket at a service, or at none when `baseUrl` is empty.
    /// Reconnects if it was already connected somewhere else.
    void setBaseUrl(const QString &baseUrl);

    /// Whether the socket is currently authenticated and usable.
    [[nodiscard]] bool isConnected() const;

    /// Asks to be in a channel's room, and to stay in it across reconnects.
    /// Safe to call before the socket is up: the room is remembered and joined
    /// once it is.
    void join(const QString &channelId);
    void part(const QString &channelId);

    /// Tells the room somebody is or is not typing. Silently does nothing when
    /// the socket is down — a typing indicator is not worth queueing, since by
    /// the time it arrived it would be describing the past.
    void sendTyping(const QString &channelId, bool active);

    /// Says something in a room. Returns the nonce the server will echo, or
    /// nothing when the socket is down and there was nobody to say it to.
    ///
    /// Nothing is queued for later. A message held back and delivered minutes
    /// afterwards arrives in a conversation that has moved on, which is worse
    /// than being told it did not send.
    std::optional<QString> say(const QString &channelId, const QString &body);

Q_SIGNALS:
    /// The server accepted a message, matched by the nonce {@link say}
    /// returned.
    void messageAccepted(const QString &nonce, qint64 id);

    /// The socket authenticated. Rooms have been rejoined by the time this
    /// fires.
    void connected();
    /// The socket went away. Anything drawn from its frames is now stale.
    void disconnected();

    void typingChanged(const QString &channelId, const QString &login,
                       bool active);
    void messageReceived(const CompanionMessage &message);
    void roomJoined(const QString &channelId,
                    const std::vector<CompanionMessage> &history);
    void restricted(const QString &channelId, const QString &reason);
    /// `nonce` names the message this is about, when it is about one.
    void errorReceived(const QString &code, const QString &message,
                       const QString &nonce);

private:
    class Listener;

    /// Opens a connection, if there is somewhere to open one to and nobody is
    /// signed out.
    void open();
    void close();
    void scheduleReconnect();

    void onOpened();
    void onFrame(const QByteArray &payload);
    void onClosed();

    void send(const QByteArray &frame);
    void handle(const CompanionFrame &frame);

    QString baseUrl_;
    WebSocketPool pool_;
    WebSocketHandle handle_;

    /// Rooms the client wants to be in, whether or not it currently is. This
    /// is what makes a reconnect invisible to everything above.
    QSet<QString> desiredRooms_;

    bool connected_ = false;
    bool wantConnection_ = false;
    /// Grows with each failed attempt so a service that is down is not
    /// hammered, and resets once a connection authenticates.
    int reconnectAttempts_ = 0;

    QTimer reconnectTimer_;
    QTimer heartbeatTimer_;
};

/// How long to wait before the nth reconnection attempt.
///
/// Exposed for testing: backoff that never stops growing pins a dead service
/// at a wait nobody will sit through, and backoff that grows too slowly is
/// indistinguishable from no backoff at all.
[[nodiscard]] int companionReconnectDelayMs(int attempt);

}  // namespace chatterino

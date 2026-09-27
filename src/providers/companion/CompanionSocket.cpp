// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/companion/CompanionSocket.hpp"

#include "Application.hpp"
#include "common/QLogging.hpp"
#include "common/Version.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "providers/companion/CompanionApi.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "util/PostToThread.hpp"

#include <QPointer>

#include <algorithm>
#include <utility>

namespace {

/// Presence expires on the server if it hears nothing, so beat comfortably
/// inside that window rather than at its edge.
constexpr int heartbeatIntervalMs = 30000;

constexpr int reconnectBaseMs = 1000;
constexpr int reconnectCeilingMs = 60000;

}  // namespace

namespace chatterino {

int companionReconnectDelayMs(int attempt)
{
    // Doubling, capped. The cap matters more than the curve: without one a
    // service that is down overnight comes back to a client that will not try
    // again for hours, which looks exactly like the client being broken.
    auto clamped = std::clamp(attempt, 0, 16);

    auto delay = static_cast<qint64>(reconnectBaseMs) * (qint64{1} << clamped);

    return static_cast<int>(std::min<qint64>(delay, reconnectCeilingMs));
}

/// Bridges the pool's thread to the GUI thread.
///
/// The pool owns this and may destroy it on its own thread at any point, so it
/// holds a QPointer rather than a raw pointer: a frame that arrives after the
/// socket is gone lands on nothing instead of on freed memory.
class CompanionSocket::Listener : public WebSocketListener
{
public:
    explicit Listener(CompanionSocket *socket)
        : socket_(socket)
    {
    }

    void onOpen() override
    {
        auto socket = this->socket_;
        runInGuiThread([socket] {
            if (!socket.isNull())
            {
                socket->onOpened();
            }
        });
    }

    void onTextMessage(QByteArray data) override
    {
        auto socket = this->socket_;
        runInGuiThread([socket, data = std::move(data)] {
            if (!socket.isNull())
            {
                socket->onFrame(data);
            }
        });
    }

    void onBinaryMessage(QByteArray /*data*/) override
    {
        // The protocol is JSON text. A binary frame is not something this
        // build knows how to be wrong about, so it is ignored rather than
        // treated as an error.
    }

    void onClose(std::unique_ptr<WebSocketListener> self) override
    {
        auto socket = this->socket_;
        runInGuiThread([socket, self = std::move(self)]() mutable {
            // `self` is carried into the GUI thread and destroyed there, so
            // this object outlives the call that reported its own closure.
            self.reset();

            if (!socket.isNull())
            {
                socket->onClosed();
            }
        });
    }

private:
    QPointer<CompanionSocket> socket_;
};

CompanionSocket::CompanionSocket()
    : pool_(u"companion"_qs)
{
    this->reconnectTimer_.setSingleShot(true);
    QObject::connect(&this->reconnectTimer_, &QTimer::timeout, this, [this] {
        this->open();
    });

    this->heartbeatTimer_.setInterval(heartbeatIntervalMs);
    QObject::connect(&this->heartbeatTimer_, &QTimer::timeout, this, [this] {
        this->send(companionFrames::beat());
    });
}

CompanionSocket::~CompanionSocket() = default;

void CompanionSocket::setBaseUrl(const QString &baseUrl)
{
    if (baseUrl == this->baseUrl_)
    {
        return;
    }

    this->baseUrl_ = baseUrl;
    this->close();

    this->wantConnection_ = CompanionUrls(baseUrl).socket().has_value();
    this->reconnectAttempts_ = 0;

    if (this->wantConnection_)
    {
        this->open();
    }
}

bool CompanionSocket::isConnected() const
{
    return this->connected_;
}

void CompanionSocket::join(const QString &channelId)
{
    if (channelId.isEmpty() || this->desiredRooms_.contains(channelId))
    {
        return;
    }

    this->desiredRooms_.insert(channelId);

    if (this->connected_)
    {
        this->send(companionFrames::join(channelId));
    }
}

void CompanionSocket::part(const QString &channelId)
{
    if (!this->desiredRooms_.remove(channelId))
    {
        return;
    }

    if (this->connected_)
    {
        this->send(companionFrames::part(channelId));
    }
}

void CompanionSocket::sendTyping(const QString &channelId, bool active)
{
    if (!this->connected_ || !this->desiredRooms_.contains(channelId))
    {
        return;
    }

    this->send(companionFrames::typing(channelId, active));
}

void CompanionSocket::open()
{
    if (!this->wantConnection_ || this->connected_)
    {
        return;
    }

    auto url = CompanionUrls(this->baseUrl_).socket();
    if (!url)
    {
        return;
    }

    // Every frame needs the caller's own token, and the socket authenticates
    // with hello rather than with a header, so a signed-out client has nothing
    // to say. Trying anyway would produce a connection that is immediately
    // refused, on a retry loop.
    auto *accounts = getApp()->getAccounts();
    if (accounts == nullptr)
    {
        return;
    }

    auto account = accounts->twitch.getCurrent();
    if (!account || account->isAnon() || account->getOAuthToken().isEmpty())
    {
        return;
    }

    this->handle_ =
        this->pool_.createSocket({.url = *url},
                                 std::make_unique<Listener>(this));
}

void CompanionSocket::close()
{
    this->heartbeatTimer_.stop();
    this->reconnectTimer_.stop();

    auto wasConnected = std::exchange(this->connected_, false);

    // Dropping the handle closes the connection; the listener's onClose will
    // still arrive, and finds wantConnection_ deciding whether to retry.
    this->handle_ = {};

    if (wasConnected)
    {
        Q_EMIT this->disconnected();
    }
}

void CompanionSocket::scheduleReconnect()
{
    if (!this->wantConnection_ || this->reconnectTimer_.isActive())
    {
        return;
    }

    auto delay = companionReconnectDelayMs(this->reconnectAttempts_);
    this->reconnectAttempts_++;

    this->reconnectTimer_.start(delay);
}

void CompanionSocket::onOpened()
{
    auto *accounts = getApp()->getAccounts();
    if (accounts == nullptr)
    {
        return;
    }

    auto account = accounts->twitch.getCurrent();
    if (!account)
    {
        return;
    }

    // Not connected until the server says hello back: until then nothing may
    // be sent but the hello itself.
    this->send(companionFrames::hello(account->getOAuthToken(),
                                      Version::instance().version(), false));
}

void CompanionSocket::onFrame(const QByteArray &payload)
{
    auto frame = parseCompanionFrame(payload);
    if (!frame)
    {
        return;
    }

    this->handle(*frame);
}

void CompanionSocket::handle(const CompanionFrame &frame)
{
    if (std::holds_alternative<CompanionReady>(frame))
    {
        this->connected_ = true;
        this->reconnectAttempts_ = 0;
        this->heartbeatTimer_.start();

        // Rejoin before telling anyone we are up, so a slot connected to
        // `connected` never sees a socket that is in no rooms.
        for (const auto &room : this->desiredRooms_)
        {
            this->send(companionFrames::join(room));
        }

        Q_EMIT this->connected();
        return;
    }

    if (const auto *typing = std::get_if<CompanionTyping>(&frame))
    {
        Q_EMIT this->typingChanged(typing->channel, typing->login,
                                   typing->active);
        return;
    }

    if (const auto *said = std::get_if<CompanionSaid>(&frame))
    {
        Q_EMIT this->messageReceived(said->message);
        return;
    }

    if (const auto *joined = std::get_if<CompanionJoined>(&frame))
    {
        Q_EMIT this->roomJoined(joined->channel, joined->history);
        return;
    }

    if (const auto *restricted = std::get_if<CompanionRestricted>(&frame))
    {
        Q_EMIT this->restricted(restricted->channel, restricted->reason);
        return;
    }

    if (const auto *error = std::get_if<CompanionError>(&frame))
    {
        qCWarning(chatterinoApp)
            << "Companion socket reported" << error->code << error->message;
        Q_EMIT this->errorReceived(error->code, error->message);
        return;
    }
}

void CompanionSocket::onClosed()
{
    this->heartbeatTimer_.stop();

    auto wasConnected = std::exchange(this->connected_, false);
    if (wasConnected)
    {
        Q_EMIT this->disconnected();
    }

    this->scheduleReconnect();
}

void CompanionSocket::send(const QByteArray &frame)
{
    this->handle_.sendText(frame);
}

}  // namespace chatterino

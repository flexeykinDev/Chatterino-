// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/Channel.hpp"

#include <pajlada/signals/signalholder.hpp>
#include <QCoreApplication>
#include <QHash>
#include <QObject>

#include <vector>
#include <QString>

namespace chatterino {

struct CompanionMessage;

/// A room that runs alongside a Twitch channel without touching it.
///
/// Shadow chat is a separate channel rather than a different colour of message
/// in the real one, and deliberately so. A shadow message looks like a chat
/// message but Twitch has never seen it: its moderators cannot delete it, it
/// is not in anyone's logs, and it does not exist for viewers on the website.
/// Mixing the two would put messages side by side that differ in who can act
/// on them, which is exactly the distinction a moderator needs to keep.
///
/// The prefix for these is `shadow:`, so `shadow:forsen` is the room beside
/// forsen's chat.
class ShadowChannel : public Channel
{
    // Channel is not a QObject, so tr() needs its context declared explicitly
    // — the same trap that makes a translated string silently come back in
    // English.
    Q_DECLARE_TR_FUNCTIONS(ShadowChannel)

public:
    /// The channel-name prefix that opens one of these.
    static const QString prefix;

    /// `login` is the Twitch channel this room sits beside.
    explicit ShadowChannel(const QString &login);
    ~ShadowChannel() override;

    [[nodiscard]] bool canSendMessage() const override;
    void sendMessage(const QString &message) override;

    /// The Twitch login this room belongs to.
    [[nodiscard]] const QString &twitchLogin() const;

    /// Finds the room id and joins.
    ///
    /// Deliberately not done in the constructor. Channels are made from inside
    /// the channel manager's lookup, which holds its mutex, and finding the
    /// Twitch channel goes back through that same lookup — relocking a
    /// non-recursive mutex on one thread, which aborts rather than waits.
    void initialize();

private:
    /// Finds the room id from the Twitch channel, waiting for it when the
    /// channel does not know it yet.
    void resolveRoom();
    void addCompanionMessage(const CompanionMessage &message);
    void addSystemMessage(const QString &text);

    QString login_;
    /// Empty until the Twitch channel learns its own id.
    QString roomId_;
    /// Messages sent and not yet answered for, so a refusal can name the one
    /// it is about rather than being a bare complaint.
    QHash<QString, QString> pending_;

    pajlada::Signals::SignalHolder signalHolder_;
    /// The socket outlives this room, so its connections are held and cut by
    /// hand; a lambda capturing `this` past the destructor is a use after
    /// free waiting for the next frame to arrive.
    std::vector<QMetaObject::Connection> socketConnections_;
};

}  // namespace chatterino

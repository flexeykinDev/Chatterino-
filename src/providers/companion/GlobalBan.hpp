// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QDateTime>
#include <QHash>
#include <QString>

#include <optional>
#include <vector>

class QJsonObject;

namespace chatterino {

/// One of the offender's messages from just before a ban, kept so a moderator
/// can judge the ban rather than only count it.
struct GlobalBanContextLine {
    QString body;
    QDateTime sentAt;
};

/// A single ban, as the companion service records it.
///
/// A lifted ban is one Twitch no longer holds. It stays in the history rather
/// than disappearing, because the fact that it happened is part of the record
/// even once it stopped applying.
///
/// Vouching is deliberately *not* here: a vouch belongs to the channel that
/// granted it, not to any particular ban, so it lives on the summary.
struct GlobalBanRecord {
    QString channelId;
    QString channelLogin;
    QString reason;
    QDateTime bannedAt;
    std::optional<QDateTime> liftedAt;
    std::vector<GlobalBanContextLine> context;

    /// Parses one history entry. Returns nothing when the entry lacks the
    /// fields that make it meaningful — a channel and a time — so that one bad
    /// row cannot make a whole history unreadable.
    static std::optional<GlobalBanRecord> fromJson(const QJsonObject &root);

    [[nodiscard]] bool isLifted() const;
};

/// Everything the service knows about one offender, as the history window shows
/// it.
struct GlobalBanSummary {
    QString offenderId;
    /// Bans that should raise a marker, as seen from the channel that asked.
    int markerCount = 0;
    /// Whether the channel that asked has vouched for this person. When it
    /// has, `markerCount` is zero for that channel and the history below is
    /// still worth reading — a vouch hides the marker, it does not erase what
    /// happened.
    bool vouched = false;
    /// Newest first, including lifted and vouched-for bans.
    std::vector<GlobalBanRecord> history;
    /// Channels where the ban is still in force, for offering to relay it.
    std::vector<QString> activeChannels;

    static std::optional<GlobalBanSummary> fromJson(const QJsonObject &root);
};

/// Parses the batch marker response, `{"markers": {"<id>": <count>}}`.
///
/// Entries that are not positive counts are dropped rather than stored as
/// zero, because "no marker" and "no answer" must stay distinguishable: the
/// caller records the ids it asked about as known-zero itself.
QHash<QString, int> parseGlobalBanMarkers(const QJsonObject &root);

}  // namespace chatterino

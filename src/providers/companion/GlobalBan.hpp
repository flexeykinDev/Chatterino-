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
/// `liftedAt` and `clearedAt` are the two ways a ban stops counting towards the
/// marker, and they mean different things: lifted means Twitch no longer holds
/// the ban, cleared means a moderator of that channel vouched for the person.
/// The history shows both, because "forgiven" and "never really banned" are not
/// the same fact.
struct GlobalBanRecord {
    QString channelId;
    QString channelLogin;
    QString reason;
    QDateTime bannedAt;
    std::optional<QDateTime> liftedAt;
    std::optional<QDateTime> clearedAt;
    QString clearedBy;
    std::vector<GlobalBanContextLine> context;

    /// Parses one history entry. Returns nothing when the entry lacks the
    /// fields that make it meaningful — a channel and a time — so that one bad
    /// row cannot make a whole history unreadable.
    static std::optional<GlobalBanRecord> fromJson(const QJsonObject &root);

    [[nodiscard]] bool isLifted() const;
    [[nodiscard]] bool isCleared() const;
};

/// Everything the service knows about one offender, as the history window shows
/// it.
struct GlobalBanSummary {
    QString offenderId;
    /// Bans that should raise a marker, as seen from the channel that asked.
    int markerCount = 0;
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

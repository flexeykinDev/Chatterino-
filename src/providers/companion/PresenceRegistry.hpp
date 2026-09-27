// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>

class QJsonObject;

namespace chatterino {

/// Whether somebody is running this client.
enum class PresenceState {
    /// A session is live right now.
    Online,
    /// They have used the client before, but nothing is running.
    Offline,
    /// Never seen. Which is nearly everybody, so it draws nothing at all.
    Unknown,
};

/// The client's view of who is running the client.
///
/// Like the ban registry this holds no network code, because laying out a
/// message asks it questions and layout runs on every repaint.
///
/// Unlike a ban, presence changes on its own: somebody who was online a minute
/// ago may not be now, and nothing tells us. So answers go stale and are asked
/// again, rather than being believed forever.
class PresenceRegistry
{
public:
    /// The most ids the batch route accepts in one request.
    static constexpr int maxBatchSize = 200;

    /// How long an answer is believed. Long enough that a busy chat does not
    /// generate constant traffic, short enough that a dot is not badly wrong.
    static constexpr qint64 freshForMs = 60000;

    /// How many channels a chatter appears in does not matter here: presence
    /// is a property of the person, not of where they are being read.
    [[nodiscard]] std::optional<PresenceState> state(const QString &userId,
                                                     qint64 nowMs) const;

    /// Records that somebody is worth asking about. Returns whether this
    /// queued anything: a fresh answer queues nothing, a stale one does.
    bool note(const QString &userId, qint64 nowMs);

    /// Takes up to `limit` queued ids, marking them in flight.
    QStringList takeBatch(int limit = maxBatchSize);

    /// Records answers. Ids that were asked about but are absent from
    /// `states` are recorded as Unknown, which is what the service means by
    /// leaving them out.
    ///
    /// Returns whether anything visible changed, so a caller can skip a
    /// relayout for an answer that said what the last one did.
    bool applyStates(const QStringList &askedIds,
                     const QHash<QString, PresenceState> &states, qint64 nowMs);

    /// Puts a failed batch's ids back, so a dropped request does not leave
    /// somebody unknown until they speak again.
    void failBatch(const QStringList &askedIds);

    void clear();

    [[nodiscard]] int pendingCount() const;
    [[nodiscard]] bool isEmpty() const;

private:
    struct Entry {
        PresenceState state = PresenceState::Unknown;
        /// When this answer stops being believed.
        qint64 staleAtMs = 0;
    };

    QHash<QString, Entry> known_;
    QStringList pending_;
    QSet<QString> pendingSet_;
    QSet<QString> inFlight_;
};

/// Parses the batch response, `{"presence": {"<id>": "online"}}`.
///
/// Entries whose value is not a state this build knows are dropped rather than
/// guessed at.
QHash<QString, PresenceState> parsePresenceStates(const QJsonObject &root);

}  // namespace chatterino

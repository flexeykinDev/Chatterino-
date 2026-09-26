// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>

namespace chatterino {

/// The client's view of which chatters were banned elsewhere.
///
/// This holds no network code on purpose. Laying out a message asks it how many
/// bans a chatter has, and layout runs on every repaint of a resized window, so
/// that call has to be a hash lookup and nothing else. Whoever owns this fills
/// it by draining {@link takeBatch} and handing the answers back.
///
/// Counts are keyed by the channel they were asked from, not just by the
/// chatter. The service answers "banned *elsewhere*", so the same person can
/// legitimately carry a marker in one tab and none in another: a ban on the
/// channel you are reading is already visible there, and a moderator of that
/// channel may have vouched for them locally.
class GlobalBanRegistry
{
public:
    /// The most ids the batch route accepts in one request.
    static constexpr int maxBatchSize = 200;

    struct Batch {
        QString channelId;
        /// Ids in the order they were first asked about, so a busy chat's
        /// oldest unanswered questions are not starved by newer ones.
        QStringList userIds;
    };

    /// How many channels this chatter is banned on, as seen from `channelId`,
    /// or nothing when that has not been answered yet. Nothing means "unknown",
    /// never "none" — a marker must not flicker into existence on every join.
    [[nodiscard]] std::optional<int> markerCount(const QString &channelId,
                                                 const QString &userId) const;

    /// Records that a chatter is worth asking about. Returns whether this
    /// actually queued anything: an id that is already known, or already in
    /// flight, queues nothing.
    bool note(const QString &channelId, const QString &userId);

    /// Takes up to `limit` queued ids for one channel, marking them in flight
    /// so a second call does not ask about them again. Returns nothing when
    /// there is nothing queued.
    std::optional<Batch> takeBatch(int limit = maxBatchSize);

    /// Records a batch's answers. Every id that was asked about becomes known,
    /// including the ones absent from `markers`, which is what makes an
    /// unmarked chatter stop being re-queried forever.
    ///
    /// Returns whether any count actually changed, so the caller can skip a
    /// relayout when the answer was "still nobody".
    bool applyMarkers(const QString &channelId, const QStringList &askedIds,
                      const QHash<QString, int> &markers);

    /// Puts a failed batch's ids back in the queue. Without this a dropped
    /// request would leave those chatters unknown until the tab is reopened.
    void failBatch(const QString &channelId, const QStringList &askedIds);

    /// Queues a chatter to be asked about again, keeping the count already
    /// held. Used after vouching for someone, where the answer has changed for
    /// reasons only the service knows.
    ///
    /// What is held is deliberately kept rather than dropped, for two reasons:
    /// the marker stays put instead of blinking off and back on while the
    /// request is out, and the answer can still be compared against what is on
    /// screen — dropping it first would make a marker's disappearance look
    /// like no change at all.
    void refresh(const QString &channelId, const QString &userId);

    /// Drops a whole channel, for leaving it or reconnecting.
    void forgetChannel(const QString &channelId);

    void clear();

    [[nodiscard]] int pendingCount() const;

    /// Whether anything is held at all — no answers, no questions, nothing on
    /// screen that depends on this.
    [[nodiscard]] bool isEmpty() const;

private:
    struct ChannelState {
        QHash<QString, int> counts;
        QStringList pending;
        QSet<QString> pendingSet;
        QSet<QString> inFlight;
    };

    QHash<QString, ChannelState> channels_;
};

}  // namespace chatterino

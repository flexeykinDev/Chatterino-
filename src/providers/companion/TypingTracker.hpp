// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QCoreApplication>
#include <QHash>
#include <QString>
#include <QStringList>

namespace chatterino {

/// Who is currently typing, per channel.
///
/// Entries expire on their own. The server is told when somebody stops, but a
/// client that crashes, loses its connection or is closed mid-sentence never
/// sends that, and an indicator that stays up forever is worse than one that
/// is occasionally a second stale. So a typist is only believed for as long as
/// the repeat interval they are expected to keep sending on.
class TypingTracker
{
    Q_DECLARE_TR_FUNCTIONS(TypingTracker)

public:
    /// How long a typing frame is believed without being repeated. Comfortably
    /// more than the repeat interval, so an indicator does not flicker between
    /// one repeat and the next on a slow connection.
    static constexpr qint64 expiryMs = 9000;

    /// Records what a `typing` frame said. Returns whether the set of people
    /// shown for that channel actually changed, so a caller can skip redrawing
    /// for the repeats that make up most of the traffic.
    bool set(const QString &channelId, const QString &login, bool active,
             qint64 nowMs);

    /// Who to show for a channel, alphabetically so the line does not reshuffle
    /// itself as repeats arrive in whatever order they happen to.
    [[nodiscard]] QStringList typists(const QString &channelId,
                                      qint64 nowMs) const;

    /// Drops everyone whose entry has expired. Returns the channels whose line
    /// changed, so only those are redrawn.
    QStringList dropExpired(qint64 nowMs);

    /// Forgets a channel, for leaving it.
    void forget(const QString &channelId);

    /// Forgets everything, for a dropped connection: nothing known before it
    /// dropped is still trustworthy.
    void clear();

    [[nodiscard]] bool isEmpty() const;

    /// The sentence to show, or empty when nobody is typing.
    ///
    /// Naming one or two people is useful; naming five is a wall of text that
    /// nobody reads, so past a couple it becomes a count instead.
    static QString describe(const QStringList &typists);

private:
    /// channel -> login -> when the entry stops being believed.
    QHash<QString, QHash<QString, qint64>> channels_;
};

}  // namespace chatterino

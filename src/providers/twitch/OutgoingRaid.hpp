// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QDateTime>
#include <QString>

#include <chrono>

namespace chatterino {

/// A raid that has been started but has not gone out yet.
///
/// Twitch waits before sending the audience over, so there is a window in
/// which the broadcaster can still call it off. Nothing in the client said the
/// window existed: `/raid` returned silently and the next thing that happened
/// was everyone leaving.
struct OutgoingRaid {
    QString targetLogin;
    QString targetDisplayName;

    /// When Twitch accepted the raid. Taken from when the call returned rather
    /// than from Twitch's own `created_at`, which the Helix wrapper discards —
    /// a round trip is a fraction of a second against a minute and a half.
    QDateTime startedAt;

    /// The name to show, falling back to the login when Twitch had no display
    /// name for them.
    [[nodiscard]] QString name() const;
};

/// How long Twitch waits before sending the audience over.
inline constexpr std::chrono::seconds raidCountdown{90};

/// How long is left, never negative and never more than the full countdown.
///
/// Clamping the top end matters: a machine whose clock is behind the one that
/// stamped the raid would otherwise show a countdown longer than a raid can
/// possibly have, and it would tick upwards as the clocks converged.
std::chrono::seconds raidTimeLeft(const OutgoingRaid &raid,
                                  const QDateTime &now);

/// Whether there is nothing left to show: the raid has gone out, or the state
/// is malformed enough that a countdown would be a guess.
bool raidHasGone(const OutgoingRaid &raid, const QDateTime &now);

/// "1:29". A countdown reads better as minutes and seconds than as the
/// "1m 29s" used elsewhere for durations that are not counting down.
QString formatRaidCountdown(std::chrono::seconds left);

/// What the banner says.
QString raidBannerText(const OutgoingRaid &raid, const QDateTime &now);

}  // namespace chatterino

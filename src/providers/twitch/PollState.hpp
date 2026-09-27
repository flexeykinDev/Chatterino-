// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QDateTime>
#include <QString>

#include <optional>
#include <vector>

class QJsonObject;

namespace chatterino {

/// One option in a poll.
struct PollChoice {
    QString id;
    QString title;
    int votes = 0;
};

/// A poll running on a channel.
///
/// Deliberately independent of where it came from. Twitch has offered this
/// over three different mechanisms, each with its own payload and its own
/// rules about who may read it; none of that should reach the thing that draws
/// a bar chart.
struct Poll {
    enum class Status {
        /// Accepting votes.
        Active,
        /// Ran its course.
        Completed,
        /// Ended early by the broadcaster. The votes still stand.
        Terminated,
        /// Ended early and hidden. The result is not worth showing.
        Archived,
    };

    QString id;
    QString title;
    Status status = Status::Active;
    std::vector<PollChoice> choices;
    QDateTime startedAt;
    QDateTime endsAt;
    /// How many people voted, which is not the number of votes: a poll may
    /// allow more than one choice each.
    int totalVoters = 0;

    /// How many votes were cast in total, across every choice.
    [[nodiscard]] int totalVotes() const;

    /// Whether votes are still being taken.
    [[nodiscard]] bool isRunning() const;

    /// Whether the result is worth showing once it is over. An archived poll
    /// was withdrawn, and putting its result on screen would announce
    /// something the broadcaster chose to take down.
    [[nodiscard]] bool hasShowableResult() const;

    /// Seconds until voting closes, floored at zero. Zero for a poll that has
    /// already ended, whatever its clock says.
    [[nodiscard]] int secondsRemaining(const QDateTime &now) const;

    /// The choice with the most votes, or nothing when there are no votes at
    /// all or when the lead is tied — a tie has no winner to point at, and
    /// picking whichever came first would be inventing one.
    [[nodiscard]] const PollChoice *leader() const;
};

/// Parses a `polls.<channel id>` frame.
///
/// The payload is Twitch's own, captured from a live poll rather than guessed
/// at. Returns nothing for anything that is not a poll update, including frame
/// types a later Twitch may add.
std::optional<Poll> parsePollFrame(const QJsonObject &root);

/// Each choice's share of the vote, as a percentage rounded to whole numbers
/// that still add up to 100.
///
/// Rounding each share independently produces columns reading 33/33/33, which
/// looks like a rounding bug to anybody who adds them up. The remainder goes to
/// the choices that lost the most to rounding.
std::vector<int> pollPercentages(const std::vector<PollChoice> &choices);

}  // namespace chatterino

// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QDateTime>
#include <QString>

#include <chrono>
#include <cstddef>
#include <vector>

namespace chatterino {

struct HelixPrediction;

/// Which of the two sides an outcome is. Twitch colours them, and people refer
/// to a prediction by its colour rather than its wording, so showing the wrong
/// one reads as showing the wrong prediction.
enum class PredictionColor {
    Blue,
    Pink,
};

/// One side of a prediction.
struct PredictionOutcome {
    QString id;
    QString title;
    /// How many people backed it.
    int users = 0;
    /// How much they staked between them.
    qint64 channelPoints = 0;
    PredictionColor color = PredictionColor::Blue;
};

/// What stage a prediction is at.
enum class PredictionStatus {
    /// Betting is open.
    Active,
    /// Betting has closed; the broadcaster has not said who won.
    Locked,
    /// Settled, with a winning outcome.
    Resolved,
    /// Called off; everybody got their points back.
    Canceled,
};

/// A prediction, independent of whether it arrived over Helix or a socket.
struct Prediction {
    QString id;
    QString title;
    PredictionStatus status = PredictionStatus::Active;
    /// The outcome that won, once one has.
    QString winningOutcomeId;
    /// When betting opened, and how long it stays open for. Neither is any use
    /// without the other: together they are the countdown.
    QDateTime createdAt;
    std::chrono::seconds window{0};
    std::vector<PredictionOutcome> outcomes;

    [[nodiscard]] bool isRunning() const;
    /// Whether there is a settled result worth leaving on screen.
    [[nodiscard]] bool hasShowableResult() const;
    /// Seconds until betting closes, never negative. Zero once it has.
    [[nodiscard]] int secondsRemaining(const QDateTime &now) const;
    /// The outcome that won, or null while nothing has.
    [[nodiscard]] const PredictionOutcome *winner() const;
    /// The total staked across every outcome.
    [[nodiscard]] qint64 totalPoints() const;
};

/// Each outcome's share of the pot, as whole percentages adding up to 100.
std::vector<int> predictionShares(const std::vector<PredictionOutcome> &outcomes);

/// What one point staked on this outcome would come back as, which is the
/// number people actually decide on.
///
/// Zero when nothing is staked on it, or when it is the only side with
/// anything on it — there is nothing to win off nobody.
double predictionReturn(const std::vector<PredictionOutcome> &outcomes,
                        std::size_t index);

/// "1.8x", or empty when there is no meaningful return yet.
QString formatPredictionReturn(double multiplier);

/// Reads the status Twitch sends. Anything unrecognised reads as active, so a
/// prediction Twitch has invented a new state for is still shown rather than
/// silently dropped.
PredictionStatus parsePredictionStatus(const QString &status);

/// Reads an outcome's colour. Anything but "PINK" is blue, which is what
/// Twitch's first outcome always is.
PredictionColor parsePredictionColor(const QString &color);

/// Builds one from what the official API returns.
Prediction predictionFromHelix(const HelixPrediction &helix);

}  // namespace chatterino

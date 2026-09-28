// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/twitch/PredictionState.hpp"

#include "providers/twitch/api/Helix.hpp"
#include "util/PercentageShares.hpp"

#include <algorithm>
#include <numeric>

namespace chatterino {

bool Prediction::isRunning() const
{
    return this->status == PredictionStatus::Active;
}

bool Prediction::hasShowableResult() const
{
    // A cancelled prediction has no result to show; a locked one has no result
    // yet. Only a resolved one that actually names a winner does.
    return this->status == PredictionStatus::Resolved &&
           this->winner() != nullptr;
}

int Prediction::secondsRemaining(const QDateTime &now) const
{
    if (!this->createdAt.isValid() || !now.isValid() ||
        this->window <= std::chrono::seconds{0})
    {
        return 0;
    }

    auto elapsed = this->createdAt.secsTo(now);
    auto left = this->window.count() - elapsed;

    // Clamped at the top as well as the bottom: a clock behind the one that
    // stamped the prediction would otherwise show longer than the window.
    return static_cast<int>(
        std::clamp<qint64>(left, 0, this->window.count()));
}

const PredictionOutcome *Prediction::winner() const
{
    if (this->winningOutcomeId.isEmpty())
    {
        return nullptr;
    }

    auto found = std::ranges::find_if(this->outcomes, [this](const auto &o) {
        return o.id == this->winningOutcomeId;
    });

    return found == this->outcomes.end() ? nullptr : &*found;
}

qint64 Prediction::totalPoints() const
{
    return std::accumulate(this->outcomes.begin(), this->outcomes.end(),
                           qint64{0},
                           [](qint64 sum, const PredictionOutcome &outcome) {
                               return sum + std::max<qint64>(
                                                0, outcome.channelPoints);
                           });
}

std::vector<int> predictionShares(const std::vector<PredictionOutcome> &outcomes)
{
    std::vector<qint64> points;
    points.reserve(outcomes.size());
    for (const auto &outcome : outcomes)
    {
        points.push_back(outcome.channelPoints);
    }

    return percentageShares(points);
}

double predictionReturn(const std::vector<PredictionOutcome> &outcomes,
                        std::size_t index)
{
    if (index >= outcomes.size())
    {
        return 0.0;
    }

    auto mine = std::max<qint64>(0, outcomes[index].channelPoints);
    if (mine <= 0)
    {
        return 0.0;
    }

    qint64 total = 0;
    for (const auto &outcome : outcomes)
    {
        total += std::max<qint64>(0, outcome.channelPoints);
    }

    if (total <= mine)
    {
        // Everything staked is on this side, so there is nothing to win off
        // anybody. Twitch shows nothing here rather than 1x.
        return 0.0;
    }

    return static_cast<double>(total) / static_cast<double>(mine);
}

QString formatPredictionReturn(double multiplier)
{
    if (multiplier <= 1.0)
    {
        return {};
    }

    return QStringLiteral("%1x").arg(multiplier, 0, 'f', 2);
}

PredictionStatus parsePredictionStatus(const QString &status)
{
    if (status == QStringLiteral("LOCKED"))
    {
        return PredictionStatus::Locked;
    }
    if (status == QStringLiteral("RESOLVED"))
    {
        return PredictionStatus::Resolved;
    }
    if (status == QStringLiteral("CANCELED") ||
        status == QStringLiteral("CANCELLED"))
    {
        return PredictionStatus::Canceled;
    }

    // "ACTIVE", and anything Twitch has invented since. Showing a prediction
    // whose state is not recognised beats dropping it silently.
    return PredictionStatus::Active;
}

PredictionColor parsePredictionColor(const QString &color)
{
    return color.compare(QStringLiteral("PINK"), Qt::CaseInsensitive) == 0
               ? PredictionColor::Pink
               : PredictionColor::Blue;
}

Prediction predictionFromHelix(const HelixPrediction &helix)
{
    Prediction prediction;
    prediction.id = helix.id;
    prediction.title = helix.title;
    prediction.status = parsePredictionStatus(helix.status);
    prediction.winningOutcomeId = helix.winningOutcomeID;
    prediction.createdAt =
        QDateTime::fromString(helix.createdAt, Qt::ISODateWithMs);
    if (!prediction.createdAt.isValid())
    {
        prediction.createdAt =
            QDateTime::fromString(helix.createdAt, Qt::ISODate);
    }
    prediction.createdAt.setTimeSpec(Qt::UTC);
    prediction.window = std::chrono::seconds{helix.predictionWindow};

    prediction.outcomes.reserve(helix.outcomes.size());
    for (const auto &outcome : helix.outcomes)
    {
        prediction.outcomes.push_back({
            .id = outcome.id,
            .title = outcome.title,
            .users = outcome.users,
            .channelPoints = outcome.channelPoints,
            .color = parsePredictionColor(outcome.color),
        });
    }

    return prediction;
}

}  // namespace chatterino

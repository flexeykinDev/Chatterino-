// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/twitch/OutgoingRaid.hpp"

#include <QCoreApplication>

#include <algorithm>

namespace chatterino {

QString OutgoingRaid::name() const
{
    return this->targetDisplayName.isEmpty() ? this->targetLogin
                                             : this->targetDisplayName;
}

std::chrono::seconds raidTimeLeft(const OutgoingRaid &raid,
                                  const QDateTime &now)
{
    if (!raid.startedAt.isValid() || !now.isValid())
    {
        return std::chrono::seconds{0};
    }

    auto elapsed = raid.startedAt.secsTo(now);
    auto left = raidCountdown.count() - elapsed;

    return std::chrono::seconds{
        std::clamp<qint64>(left, 0, raidCountdown.count())};
}

bool raidHasGone(const OutgoingRaid &raid, const QDateTime &now)
{
    if (raid.targetLogin.isEmpty() || !raid.startedAt.isValid())
    {
        // Nothing worth counting down to. Hiding beats a banner stuck at 1:30.
        return true;
    }

    return raidTimeLeft(raid, now) == std::chrono::seconds{0};
}

QString formatRaidCountdown(std::chrono::seconds left)
{
    auto total = std::max<qint64>(left.count(), 0);

    return QStringLiteral("%1:%2")
        .arg(total / 60)
        .arg(total % 60, 2, 10, QLatin1Char('0'));
}

QString raidBannerText(const OutgoingRaid &raid, const QDateTime &now)
{
    auto left = raidTimeLeft(raid, now);
    if (left == std::chrono::seconds{0})
    {
        return QCoreApplication::translate("OutgoingRaid", "Raiding %1 now")
            .arg(raid.name());
    }

    return QCoreApplication::translate("OutgoingRaid", "Raiding %1 in %2")
        .arg(raid.name(), formatRaidCountdown(left));
}

}  // namespace chatterino

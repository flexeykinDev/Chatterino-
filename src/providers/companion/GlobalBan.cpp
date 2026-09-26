// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/companion/GlobalBan.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>

namespace {

using namespace chatterino;

/// Epoch milliseconds to a local timestamp. Anything that is not a positive
/// number reads as "not set", which is how the service spells both `null` and
/// an absent field.
std::optional<QDateTime> parseEpochMillis(const QJsonValue &value)
{
    if (!value.isDouble())
    {
        return std::nullopt;
    }

    auto millis = static_cast<qint64>(value.toDouble());
    if (millis <= 0)
    {
        return std::nullopt;
    }

    return QDateTime::fromMSecsSinceEpoch(millis);
}

std::vector<GlobalBanContextLine> parseContext(const QJsonValue &value)
{
    std::vector<GlobalBanContextLine> lines;
    if (!value.isArray())
    {
        return lines;
    }

    for (const auto &entry : value.toArray())
    {
        if (!entry.isObject())
        {
            continue;
        }

        auto object = entry.toObject();
        auto body = object.value("body").toString();
        if (body.isEmpty())
        {
            continue;
        }

        lines.push_back({
            .body = body,
            .sentAt = parseEpochMillis(object.value("sentAt"))
                          .value_or(QDateTime{}),
        });
    }

    return lines;
}

}  // namespace

namespace chatterino {

std::optional<GlobalBanRecord> GlobalBanRecord::fromJson(
    const QJsonObject &root)
{
    auto channelId = root.value("channelId").toString();
    auto bannedAt = parseEpochMillis(root.value("bannedAt"));
    if (channelId.isEmpty() || !bannedAt.has_value())
    {
        return std::nullopt;
    }

    auto channelLogin = root.value("channelLogin").toString();

    return GlobalBanRecord{
        .channelId = channelId,
        // A record with no login is still worth showing; the id is at least
        // something the viewer can look up.
        .channelLogin = channelLogin.isEmpty() ? channelId : channelLogin,
        .reason = root.value("reason").toString(),
        .bannedAt = *bannedAt,
        .liftedAt = parseEpochMillis(root.value("liftedAt")),
        .clearedAt = parseEpochMillis(root.value("clearedAt")),
        .clearedBy = root.value("clearedBy").toString(),
        .context = parseContext(root.value("context")),
    };
}

bool GlobalBanRecord::isLifted() const
{
    return this->liftedAt.has_value();
}

bool GlobalBanRecord::isCleared() const
{
    return this->clearedAt.has_value();
}

std::optional<GlobalBanSummary> GlobalBanSummary::fromJson(
    const QJsonObject &root)
{
    auto offenderId = root.value("offenderId").toString();
    if (offenderId.isEmpty())
    {
        return std::nullopt;
    }

    GlobalBanSummary summary;
    summary.offenderId = offenderId;
    summary.markerCount = std::max(0, root.value("markerCount").toInt());

    for (const auto &entry : root.value("history").toArray())
    {
        if (!entry.isObject())
        {
            continue;
        }

        if (auto record = GlobalBanRecord::fromJson(entry.toObject()))
        {
            summary.history.push_back(std::move(*record));
        }
    }

    for (const auto &entry : root.value("activeChannels").toArray())
    {
        auto channelId = entry.toString();
        if (!channelId.isEmpty())
        {
            summary.activeChannels.push_back(channelId);
        }
    }

    return summary;
}

QHash<QString, int> parseGlobalBanMarkers(const QJsonObject &root)
{
    QHash<QString, int> markers;

    auto object = root.value("markers").toObject();
    for (auto it = object.begin(); it != object.end(); ++it)
    {
        if (!it.value().isDouble())
        {
            continue;
        }

        auto count = it.value().toInt();
        if (count > 0)
        {
            markers.insert(it.key(), count);
        }
    }

    return markers;
}

}  // namespace chatterino

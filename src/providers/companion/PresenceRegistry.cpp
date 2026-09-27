// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/companion/PresenceRegistry.hpp"

#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>

namespace chatterino {

std::optional<PresenceState> PresenceRegistry::state(const QString &userId,
                                                     qint64 nowMs) const
{
    auto entry = this->known_.constFind(userId);
    if (entry == this->known_.constEnd())
    {
        return std::nullopt;
    }

    // A stale answer is still shown rather than blanked. Somebody's dot
    // flickering off while it is re-asked is worse than it being a minute out
    // of date, and the re-ask is already queued.
    return entry->state;
}

bool PresenceRegistry::note(const QString &userId, qint64 nowMs)
{
    if (userId.isEmpty() || this->pendingSet_.contains(userId) ||
        this->inFlight_.contains(userId))
    {
        return false;
    }

    auto entry = this->known_.constFind(userId);
    if (entry != this->known_.constEnd() && entry->staleAtMs > nowMs)
    {
        return false;
    }

    this->pending_.append(userId);
    this->pendingSet_.insert(userId);
    return true;
}

QStringList PresenceRegistry::takeBatch(int limit)
{
    if (limit <= 0 || this->pending_.isEmpty())
    {
        return {};
    }

    auto take = std::min<qsizetype>(limit, this->pending_.size());
    auto batch = this->pending_.mid(0, take);
    this->pending_.remove(0, take);

    for (const auto &userId : batch)
    {
        this->pendingSet_.remove(userId);
        this->inFlight_.insert(userId);
    }

    return batch;
}

bool PresenceRegistry::applyStates(
    const QStringList &askedIds, const QHash<QString, PresenceState> &states,
    qint64 nowMs)
{
    bool changed = false;

    auto record = [&](const QString &userId, PresenceState state) {
        auto existing = this->known_.constFind(userId);

        // Unknown draws nothing, and so does never having asked, so the two
        // are the same to look at.
        if (existing == this->known_.constEnd())
        {
            changed = changed || state != PresenceState::Unknown;
        }
        else
        {
            changed = changed || existing->state != state;
        }

        this->known_.insert(userId, {
                                        .state = state,
                                        .staleAtMs = nowMs + freshForMs,
                                    });
    };

    for (const auto &userId : askedIds)
    {
        this->inFlight_.remove(userId);
        record(userId, states.value(userId, PresenceState::Unknown));
    }

    // Anything volunteered that was not asked for is taken too, but does not
    // cancel a queued question: something was queued because what we hold is
    // older than this answer may be.
    for (auto it = states.constBegin(); it != states.constEnd(); ++it)
    {
        if (!askedIds.contains(it.key()))
        {
            record(it.key(), it.value());
        }
    }

    return changed;
}

void PresenceRegistry::failBatch(const QStringList &askedIds)
{
    for (const auto &userId : askedIds)
    {
        this->inFlight_.remove(userId);

        if (this->pendingSet_.contains(userId))
        {
            continue;
        }

        this->pending_.append(userId);
        this->pendingSet_.insert(userId);
    }
}

void PresenceRegistry::clear()
{
    this->known_.clear();
    this->pending_.clear();
    this->pendingSet_.clear();
    this->inFlight_.clear();
}

int PresenceRegistry::pendingCount() const
{
    return static_cast<int>(this->pending_.size());
}

bool PresenceRegistry::isEmpty() const
{
    return this->known_.isEmpty() && this->pending_.isEmpty() &&
           this->inFlight_.isEmpty();
}

QHash<QString, PresenceState> parsePresenceStates(const QJsonObject &root)
{
    QHash<QString, PresenceState> states;

    auto object = root.value("presence").toObject();
    for (auto it = object.begin(); it != object.end(); ++it)
    {
        auto value = it.value().toString();

        if (value == "online")
        {
            states.insert(it.key(), PresenceState::Online);
        }
        else if (value == "offline")
        {
            states.insert(it.key(), PresenceState::Offline);
        }
        else if (value == "unknown")
        {
            states.insert(it.key(), PresenceState::Unknown);
        }

        // Anything else is a state this build does not know. Guessing at it
        // would put a wrong dot beside a name, so it is dropped.
    }

    return states;
}

}  // namespace chatterino

// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/companion/GlobalBanRegistry.hpp"

#include <algorithm>

namespace chatterino {

std::optional<int> GlobalBanRegistry::markerCount(const QString &channelId,
                                                  const QString &userId) const
{
    auto channel = this->channels_.constFind(channelId);
    if (channel == this->channels_.constEnd())
    {
        return std::nullopt;
    }

    auto count = channel->counts.constFind(userId);
    if (count == channel->counts.constEnd())
    {
        return std::nullopt;
    }

    return *count;
}

bool GlobalBanRegistry::note(const QString &channelId, const QString &userId)
{
    if (channelId.isEmpty() || userId.isEmpty())
    {
        return false;
    }

    auto &channel = this->channels_[channelId];
    if (channel.counts.contains(userId) || channel.inFlight.contains(userId) ||
        channel.pendingSet.contains(userId))
    {
        return false;
    }

    channel.pending.append(userId);
    channel.pendingSet.insert(userId);
    return true;
}

std::optional<GlobalBanRegistry::Batch> GlobalBanRegistry::takeBatch(int limit)
{
    if (limit <= 0)
    {
        return std::nullopt;
    }

    for (auto it = this->channels_.begin(); it != this->channels_.end(); ++it)
    {
        auto &channel = it.value();
        if (channel.pending.isEmpty())
        {
            continue;
        }

        auto take = std::min<qsizetype>(limit, channel.pending.size());

        Batch batch;
        batch.channelId = it.key();
        batch.userIds = channel.pending.mid(0, take);
        channel.pending.remove(0, take);

        for (const auto &userId : batch.userIds)
        {
            channel.pendingSet.remove(userId);
            channel.inFlight.insert(userId);
        }

        return batch;
    }

    return std::nullopt;
}

bool GlobalBanRegistry::applyMarkers(const QString &channelId,
                                     const QStringList &askedIds,
                                     const QHash<QString, int> &markers)
{
    auto &channel = this->channels_[channelId];

    bool changed = false;

    // What was on screen before, for deciding whether a relayout is warranted.
    // An unknown chatter drew nothing, which is what a zero draws too, so the
    // two are the same to look at and the common case — a whole batch of
    // chatters nobody has banned — costs no relayout at all.
    auto wasDrawnAs = [&channel](const QString &userId) {
        return channel.counts.value(userId, 0);
    };

    for (const auto &userId : askedIds)
    {
        channel.inFlight.remove(userId);

        auto count = markers.value(userId, 0);
        changed = changed || wasDrawnAs(userId) != count;
        channel.counts.insert(userId, count);
    }

    // The service may volunteer a marker for someone we did not ask about in
    // this batch — a ban recorded while the request was in flight, say. Taking
    // it is free and saves a round trip.
    //
    // It does not cancel a queued question, though. Volunteered data is a
    // bonus, and a question queued by refresh() was queued precisely because
    // something happened that the answer in hand predates.
    for (auto it = markers.constBegin(); it != markers.constEnd(); ++it)
    {
        changed = changed || wasDrawnAs(it.key()) != it.value();
        channel.counts.insert(it.key(), it.value());
    }

    return changed;
}

void GlobalBanRegistry::failBatch(const QString &channelId,
                                  const QStringList &askedIds)
{
    auto channel = this->channels_.find(channelId);
    if (channel == this->channels_.end())
    {
        return;
    }

    for (const auto &userId : askedIds)
    {
        channel->inFlight.remove(userId);

        if (channel->counts.contains(userId) ||
            channel->pendingSet.contains(userId))
        {
            continue;
        }

        channel->pending.append(userId);
        channel->pendingSet.insert(userId);
    }
}

void GlobalBanRegistry::refresh(const QString &channelId,
                                const QString &userId)
{
    if (channelId.isEmpty() || userId.isEmpty())
    {
        return;
    }

    auto &channel = this->channels_[channelId];

    // Dropped from in flight so a reply to the older request cannot mark this
    // answered and leave the refresh unasked.
    channel.inFlight.remove(userId);

    if (!channel.pendingSet.contains(userId))
    {
        channel.pending.append(userId);
        channel.pendingSet.insert(userId);
    }
}

void GlobalBanRegistry::forgetChannel(const QString &channelId)
{
    this->channels_.remove(channelId);
}

void GlobalBanRegistry::clear()
{
    this->channels_.clear();
}

bool GlobalBanRegistry::isEmpty() const
{
    return this->channels_.isEmpty();
}

int GlobalBanRegistry::pendingCount() const
{
    int total = 0;
    for (const auto &channel : this->channels_)
    {
        total += static_cast<int>(channel.pending.size());
    }

    return total;
}

}  // namespace chatterino

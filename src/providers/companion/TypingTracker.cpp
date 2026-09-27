// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/companion/TypingTracker.hpp"

#include <algorithm>

namespace chatterino {

bool TypingTracker::set(const QString &channelId, const QString &login,
                        bool active, qint64 nowMs)
{
    if (channelId.isEmpty() || login.isEmpty())
    {
        return false;
    }

    auto &logins = this->channels_[channelId];

    if (!active)
    {
        auto wasShown = logins.contains(login) && logins.value(login) > nowMs;
        logins.remove(login);

        if (logins.isEmpty())
        {
            this->channels_.remove(channelId);
        }

        return wasShown;
    }

    // Already shown and merely being repeated. The line is unchanged, which is
    // the common case by a wide margin.
    auto wasShown = logins.value(login, 0) > nowMs;
    logins.insert(login, nowMs + expiryMs);

    return !wasShown;
}

QStringList TypingTracker::typists(const QString &channelId,
                                   qint64 nowMs) const
{
    auto channel = this->channels_.constFind(channelId);
    if (channel == this->channels_.constEnd())
    {
        return {};
    }

    QStringList names;
    for (auto it = channel->constBegin(); it != channel->constEnd(); ++it)
    {
        if (it.value() > nowMs)
        {
            names.append(it.key());
        }
    }

    names.sort(Qt::CaseInsensitive);
    return names;
}

QStringList TypingTracker::dropExpired(qint64 nowMs)
{
    QStringList changed;

    for (auto channel = this->channels_.begin();
         channel != this->channels_.end();)
    {
        auto &logins = channel.value();

        bool removedAny = false;
        for (auto it = logins.begin(); it != logins.end();)
        {
            if (it.value() <= nowMs)
            {
                it = logins.erase(it);
                removedAny = true;
            }
            else
            {
                ++it;
            }
        }

        if (removedAny)
        {
            changed.append(channel.key());
        }

        if (logins.isEmpty())
        {
            channel = this->channels_.erase(channel);
        }
        else
        {
            ++channel;
        }
    }

    return changed;
}

void TypingTracker::forget(const QString &channelId)
{
    this->channels_.remove(channelId);
}

void TypingTracker::clear()
{
    this->channels_.clear();
}

bool TypingTracker::isEmpty() const
{
    return this->channels_.isEmpty();
}

QString TypingTracker::describe(const QStringList &typists)
{
    switch (typists.size())
    {
        case 0:
            return {};

        case 1:
            //: %1 is a chatter's name.
            return TypingTracker::tr("%1 is typing…").arg(typists[0]);

        case 2:
            //: %1 and %2 are chatters' names.
            return TypingTracker::tr("%1 and %2 are typing…")
                .arg(typists[0], typists[1]);

        default:
            // Naming five people is a wall of text nobody reads, and the line
            // has to fit beside the message box.
            //: %n is how many people are typing, always three or more.
            return TypingTracker::tr("%n people are typing…", "",
                                     static_cast<int>(typists.size()));
    }
}

}  // namespace chatterino

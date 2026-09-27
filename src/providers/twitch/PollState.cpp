// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/twitch/PollState.hpp"

#include <algorithm>
#include <numeric>

namespace chatterino {

int Poll::totalVotes() const
{
    return std::accumulate(this->choices.begin(), this->choices.end(), 0,
                           [](int sum, const PollChoice &choice) {
                               return sum + std::max(0, choice.votes);
                           });
}

bool Poll::isRunning() const
{
    return this->status == Status::Active;
}

bool Poll::hasShowableResult() const
{
    return this->status == Status::Completed ||
           this->status == Status::Terminated;
}

int Poll::secondsRemaining(const QDateTime &now) const
{
    if (!this->isRunning() || !this->endsAt.isValid() || !now.isValid())
    {
        return 0;
    }

    auto seconds = now.secsTo(this->endsAt);
    return static_cast<int>(std::max<qint64>(0, seconds));
}

const PollChoice *Poll::leader() const
{
    const PollChoice *best = nullptr;
    bool tied = false;

    for (const auto &choice : this->choices)
    {
        if (choice.votes <= 0)
        {
            continue;
        }

        if (best == nullptr || choice.votes > best->votes)
        {
            best = &choice;
            tied = false;
        }
        else if (choice.votes == best->votes)
        {
            tied = true;
        }
    }

    return tied ? nullptr : best;
}

std::vector<int> pollPercentages(const std::vector<PollChoice> &choices)
{
    std::vector<int> shares(choices.size(), 0);

    auto total = std::accumulate(choices.begin(), choices.end(), 0,
                                 [](int sum, const PollChoice &choice) {
                                     return sum + std::max(0, choice.votes);
                                 });
    if (total <= 0)
    {
        return shares;
    }

    // Floor each share, then hand the leftover to whoever lost most to the
    // rounding. Rounding each independently gives columns that read 33/33/33
    // and look like a bug to anyone who adds them up.
    std::vector<std::pair<int, std::size_t>> remainders;
    int assigned = 0;

    for (std::size_t i = 0; i < choices.size(); i++)
    {
        auto votes = std::max(0, choices[i].votes);
        auto scaled = votes * 100;

        shares[i] = scaled / total;
        assigned += shares[i];
        remainders.emplace_back(scaled % total, i);
    }

    std::ranges::sort(remainders, [](const auto &a, const auto &b) {
        // Largest remainder first; ties go to the earlier choice so the same
        // votes always produce the same picture.
        if (a.first != b.first)
        {
            return a.first > b.first;
        }
        return a.second < b.second;
    });

    for (int i = 0; assigned < 100 && i < static_cast<int>(remainders.size());
         i++)
    {
        shares[remainders[static_cast<std::size_t>(i)].second]++;
        assigned++;
    }

    return shares;
}

}  // namespace chatterino

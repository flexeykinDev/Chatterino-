// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "util/PercentageShares.hpp"

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <utility>

namespace chatterino {

std::vector<int> percentageShares(const std::vector<qint64> &counts)
{
    std::vector<int> shares(counts.size(), 0);

    auto total = std::accumulate(counts.begin(), counts.end(), qint64{0},
                                 [](qint64 sum, qint64 count) {
                                     return sum + std::max<qint64>(0, count);
                                 });
    if (total <= 0)
    {
        return shares;
    }

    std::vector<std::pair<qint64, std::size_t>> remainders;
    remainders.reserve(counts.size());
    int assigned = 0;

    for (std::size_t i = 0; i < counts.size(); i++)
    {
        auto count = std::max<qint64>(0, counts[i]);
        auto scaled = count * 100;

        shares[i] = static_cast<int>(scaled / total);
        assigned += shares[i];
        remainders.emplace_back(scaled % total, i);
    }

    std::ranges::sort(remainders, [](const auto &a, const auto &b) {
        // Largest remainder first; ties go to the earlier entry so the same
        // counts always produce the same picture.
        if (a.first != b.first)
        {
            return a.first > b.first;
        }
        return a.second < b.second;
    });

    for (std::size_t i = 0;
         assigned < 100 && i < remainders.size(); i++)
    {
        shares[remainders[i].second]++;
        assigned++;
    }

    return shares;
}

}  // namespace chatterino

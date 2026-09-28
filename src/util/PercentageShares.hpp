// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QtGlobal>

#include <vector>

namespace chatterino {

/// Each count's share of the total, as whole percentages that still add up to
/// 100.
///
/// Rounding each share on its own produces columns reading 33/33/33, which
/// looks like a rounding bug to anybody who adds them up. The leftover goes to
/// whichever counts lost the most to the rounding, earlier ones first so the
/// same numbers always draw the same picture.
///
/// Everything is zero when nothing has been counted yet: a poll nobody has
/// voted in has no shares, not equal ones.
std::vector<int> percentageShares(const std::vector<qint64> &counts);

}  // namespace chatterino

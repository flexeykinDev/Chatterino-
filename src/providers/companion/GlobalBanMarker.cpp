// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/companion/GlobalBanMarker.hpp"

#include <algorithm>

namespace chatterino {

QSizeF GlobalBanMarkerMetrics::size(qreal lineHeight, qreal digitsWidth)
{
    auto height = std::max<qreal>(0, lineHeight) * heightRatio;
    auto padding = height * horizontalPaddingRatio;
    auto width = std::max(height, std::max<qreal>(0, digitsWidth) + padding * 2);

    return {width, height};
}

qreal GlobalBanMarkerMetrics::cornerRadius(qreal markerHeight)
{
    return std::max<qreal>(0, markerHeight) * cornerRadiusRatio;
}

QRectF GlobalBanMarkerMetrics::textRect(QRectF bounds)
{
    auto padding = bounds.height() * horizontalPaddingRatio;

    // Never let the padding eat the whole tag: a very small line height would
    // otherwise produce a negative width and Qt would draw nothing at all.
    if (padding * 2 >= bounds.width())
    {
        return bounds;
    }

    return bounds.adjusted(padding, 0, -padding, 0);
}

}  // namespace chatterino

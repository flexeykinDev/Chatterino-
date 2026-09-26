// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QRectF>
#include <QSizeF>

namespace chatterino {

/// The shape of the cross-channel ban marker.
///
/// The marker is a rounded tag carrying the number of channels a chatter is
/// banned on. Its size is arithmetic on the line height and the width of the
/// digits, kept here so it can be checked without a window: a badge that is a
/// pixel too wide crowds the name, and one too narrow clips the number, and
/// neither shows up in a test that only asks whether it was drawn.
struct GlobalBanMarkerMetrics {
    /// Padding either side of the digits, as a fraction of the line height.
    static constexpr qreal horizontalPaddingRatio = 0.34;
    /// Corner radius, as a fraction of the marker height.
    static constexpr qreal cornerRadiusRatio = 0.3;
    /// The marker is slightly shorter than the line so it does not touch the
    /// message above and below.
    static constexpr qreal heightRatio = 0.86;

    /// `lineHeight` is the height a badge on this line may occupy, and
    /// `digitsWidth` the rendered width of the count. A marker is never
    /// narrower than it is tall, so a single digit stays a round tag rather
    /// than a sliver.
    static QSizeF size(qreal lineHeight, qreal digitsWidth);

    static qreal cornerRadius(qreal markerHeight);

    /// Where the digits go inside a marker placed at `bounds`: centred, since
    /// the padding is symmetric.
    static QRectF textRect(QRectF bounds);
};

}  // namespace chatterino

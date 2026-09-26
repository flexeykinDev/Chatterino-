// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/helper/TabAvatarLayout.hpp"

#include <algorithm>

namespace {

/// Breathing room between the avatar and the tab's edge, as a fraction of the
/// tab. Without it the circles would touch each other in a row of tabs.
constexpr qreal PADDING_RATIO = 0.16;

/// Ring thickness and badge size in unscaled pixels.
constexpr qreal RING_WIDTH = 2.0;
constexpr qreal BADGE_DIAMETER = 8.0;

}  // namespace

namespace chatterino {

TabAvatarLayout computeTabAvatarLayout(QRectF tab, qreal scale)
{
    TabAvatarLayout layout;

    if (scale <= 0)
    {
        scale = 1;
    }

    // The avatar is a circle, so it is sized by whichever side is shorter and
    // centred in the other. A tab is square in avatar mode, but this keeps the
    // layout sane if it is ever given a rectangle.
    const auto side = std::min(tab.width(), tab.height());
    const auto padding = side * PADDING_RATIO;
    const auto diameter = std::max(side - (padding * 2), 1.0);

    const auto centre = tab.center();
    layout.avatar = QRectF(centre.x() - (diameter / 2),
                           centre.y() - (diameter / 2), diameter, diameter);

    layout.ringWidth = RING_WIDTH * scale;

    // The ring is stroked on the circle's edge, so half of it falls outside.
    // Pulling the circle in by that half keeps it inside the tab.
    const auto inset = layout.ringWidth / 2;
    layout.ring = layout.avatar.adjusted(-inset, -inset, inset, inset);

    const auto badgeSize = std::min(BADGE_DIAMETER * scale, diameter / 2);
    // Sat on the upper right of the circle, far enough in that it overlaps the
    // avatar rather than floating away from it.
    layout.badge =
        QRectF(layout.avatar.right() - (badgeSize * 0.75),
               layout.avatar.top() - (badgeSize * 0.25), badgeSize, badgeSize);

    return layout;
}

}  // namespace chatterino

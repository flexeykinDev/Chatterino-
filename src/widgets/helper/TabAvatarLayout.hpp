// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QRectF>

namespace chatterino {

/// Where the parts of an avatar tab sit.
///
/// A tab showing an avatar is square, and everything inside it is derived from
/// that square so the result holds together at any scale or zoom.
struct TabAvatarLayout {
    /// The circle the avatar is drawn in.
    QRectF avatar;
    /// The ring drawn around the avatar, inset so it is not clipped by the
    /// tab's edge. Same as `avatar` when there is no ring to draw.
    QRectF ring;
    /// How thick that ring is.
    qreal ringWidth = 0;
    /// The dot marking unread messages, in the upper trailing corner.
    QRectF badge;
};

/// Lays out an avatar inside `tab`.
///
/// `scale` is the interface scale, so the ring and badge keep their weight
/// relative to the rest of the interface rather than growing with the avatar.
TabAvatarLayout computeTabAvatarLayout(QRectF tab, qreal scale);

}  // namespace chatterino

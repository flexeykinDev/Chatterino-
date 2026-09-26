// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "providers/emoteeffects/EmoteEffect.hpp"

#include <QRectF>
#include <QSizeF>
#include <QTransform>

namespace chatterino {

/// How an emote is drawn once its geometry effects are taken into account.
///
/// The two sizes differ for a quarter turn: the emote still occupies its own
/// width and height when drawn, but the space it takes up in the line has those
/// swapped.
struct EmoteEffectGeometry {
    /// The size the emote is drawn at, before any rotation.
    QSizeF drawnSize;
    /// The space the emote occupies in the line, after rotation.
    QSizeF occupiedSize;
    /// Degrees clockwise, one of 0, 90 or 270.
    qreal rotation = 0;
    bool flipHorizontally = false;
    bool flipVertically = false;

    /// Whether the emote sits flush against the one before it.
    bool collapseLeadingSpace = false;
};

/// Works out how `base` is stretched, turned and mirrored by `effects`.
///
/// `base` is the size the emote would be drawn at with no effects, already
/// scaled for the font and the user's emote scale.
EmoteEffectGeometry computeEmoteEffectGeometry(QSizeF base,
                                               const EmoteEffectSet &effects);

/// The transform that maps an emote drawn at `geometry.drawnSize` into `target`.
///
/// `target` is the rectangle the emote occupies in the line, so its size is
/// `geometry.occupiedSize`. The transform rotates and mirrors about the
/// rectangle's centre; the caller draws the emote into
/// {@link drawnRectFor} under this transform.
QTransform emoteEffectTransform(const EmoteEffectGeometry &geometry,
                                QRectF target);

/// The rectangle to draw the emote into, centred in `target`, before the
/// transform turns it.
QRectF drawnRectFor(const EmoteEffectGeometry &geometry, QRectF target);

}  // namespace chatterino

// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "providers/emoteeffects/EmoteEffect.hpp"

#include <QColor>
#include <QPixmap>
#include <QPointF>

namespace chatterino {

/// How an emote is displaced and tinted at one instant.
///
/// Everything here is derived from the clock rather than from a frame counter,
/// so the same emote is at the same point of its cycle for everyone watching,
/// and a repaint that is late or skipped does not shift the animation.
struct EmoteEffectAnimation {
    /// Displacement in pixels, applied on top of the emote's position.
    QPointF offset;
    /// Degrees clockwise, added to any quarter turn from the geometry.
    qreal rotation = 0;
    /// Uniform scale about the centre.
    qreal scale = 1;

    /// A colour laid over the emote, keeping its shape. Invalid when the emote
    /// is drawn in its own colours.
    QColor tint;
    /// How strongly the tint covers the emote, 0 to 1.
    qreal tintStrength = 0;

    /// Whether anything here changes over time, which is what decides if the
    /// emote has to be repainted every frame.
    [[nodiscard]] bool isAnimated() const;
};

/// Whether `kind` changes appearance from one moment to the next.
bool isTimeVaryingEffect(EmoteEffectKind kind);

/// Whether any effect in `effects` needs repainting over time.
bool hasTimeVaryingEffect(const EmoteEffectSet &effects);

/// Works out the displacement and tint for `effects` at `seconds`.
///
/// `seconds` is wall-clock time, not time since the message arrived: the
/// specification ties an effect's phase to the real clock so that everyone sees
/// it in step.
///
/// `emoteHeight` scales the displacements, so an effect looks the same at any
/// emote size or zoom level.
EmoteEffectAnimation computeEmoteEffectAnimation(const EmoteEffectSet &effects,
                                                 qreal seconds,
                                                 qreal emoteHeight);

/// Lays `tint` over `source` at `strength`, keeping the source's shape.
///
/// The colour has to stop at the emote's outline. Compositing it straight onto
/// the view would paint the whole rectangle, background included, so it is
/// composed onto a transparent layer where the emote's own alpha masks it.
QPixmap tintedPixmap(const QPixmap &source, const QColor &tint, qreal strength);

}  // namespace chatterino

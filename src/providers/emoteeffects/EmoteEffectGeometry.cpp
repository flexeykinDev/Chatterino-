// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/emoteeffects/EmoteEffectGeometry.hpp"

namespace {

/// How much wider each stretching effect makes an emote.
constexpr qreal WIDE_FACTOR = 4.0;
constexpr qreal GROW_X_FACTOR = 2.0;

}  // namespace

namespace chatterino {

EmoteEffectGeometry computeEmoteEffectGeometry(QSizeF base,
                                               const EmoteEffectSet &effects)
{
    EmoteEffectGeometry geometry{
        .drawnSize = base,
        .occupiedSize = base,
        .rotation = 0,
        .flipHorizontally = false,
        .flipVertically = false,
        .collapseLeadingSpace = effects.has(EmoteEffectKind::NoSpace),
    };

    // Only one geometry effect is ever in force, so this is a choice rather
    // than an accumulation.
    if (auto kind = effects.geometry())
    {
        switch (*kind)
        {
            case EmoteEffectKind::Wide:
                geometry.drawnSize.setWidth(base.width() * WIDE_FACTOR);
                break;

            case EmoteEffectKind::GrowX:
                geometry.drawnSize.setWidth(base.width() * GROW_X_FACTOR);
                break;

            case EmoteEffectKind::FlipX:
                geometry.flipHorizontally = true;
                break;

            case EmoteEffectKind::FlipY:
                geometry.flipVertically = true;
                break;

            case EmoteEffectKind::RotateLeft:
                geometry.rotation = 270;
                break;

            case EmoteEffectKind::RotateRight:
                geometry.rotation = 90;
                break;

            default:
                break;
        }
    }

    geometry.occupiedSize = geometry.drawnSize;
    if (geometry.rotation != 0)
    {
        // A quarter turn puts the emote's height along the line and its width
        // across it.
        geometry.occupiedSize = QSizeF(geometry.drawnSize.height(),
                                       geometry.drawnSize.width());
    }

    return geometry;
}

QRectF drawnRectFor(const EmoteEffectGeometry &geometry, QRectF target)
{
    // Centred, so a rotation about the centre lands inside the occupied space.
    const auto centre = target.center();
    return QRectF(centre.x() - (geometry.drawnSize.width() / 2),
                  centre.y() - (geometry.drawnSize.height() / 2),
                  geometry.drawnSize.width(), geometry.drawnSize.height());
}

QTransform emoteEffectTransform(const EmoteEffectGeometry &geometry,
                                QRectF target)
{
    QTransform transform;

    if (geometry.rotation == 0 && !geometry.flipHorizontally &&
        !geometry.flipVertically)
    {
        return transform;
    }

    const auto centre = target.center();

    // Turn and mirror about the centre of the space the emote occupies, so the
    // result stays within it rather than sliding off toward the origin.
    transform.translate(centre.x(), centre.y());

    if (geometry.rotation != 0)
    {
        transform.rotate(geometry.rotation);
    }

    if (geometry.flipHorizontally || geometry.flipVertically)
    {
        transform.scale(geometry.flipHorizontally ? -1.0 : 1.0,
                        geometry.flipVertically ? -1.0 : 1.0);
    }

    transform.translate(-centre.x(), -centre.y());

    return transform;
}

}  // namespace chatterino

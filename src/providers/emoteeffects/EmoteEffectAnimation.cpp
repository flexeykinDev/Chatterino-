// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/emoteeffects/EmoteEffectAnimation.hpp"

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace {

constexpr qreal TAU = 6.283185307179586;
constexpr qreal PI = 3.141592653589793;

/// Seconds per cycle for each moving effect.
constexpr qreal SPIN_PERIOD = 3.0;
constexpr qreal JAM_PERIOD = 0.6;
constexpr qreal BOUNCE_PERIOD = 0.5;
constexpr qreal SLIDE_PERIOD = 2.0;
constexpr qreal SHAKE_PERIOD = 0.12;
constexpr qreal HYPER_SHAKE_PERIOD = 0.06;
constexpr qreal HUE_PERIOD = 2.0;

/// Displacements as a fraction of the emote's height, so they hold their
/// proportions at any size.
constexpr qreal JAM_DEGREES = 12.0;
constexpr qreal BOUNCE_HEIGHT = 0.18;
constexpr qreal SLIDE_WIDTH = 0.35;
constexpr qreal SHAKE_AMOUNT = 0.07;
constexpr qreal HYPER_SHAKE_AMOUNT = 0.05;

/// How strongly the colour effects cover the emote.
constexpr qreal PARTY_STRENGTH = 0.55;
constexpr qreal RAINBOW_STRENGTH = 0.55;
constexpr qreal HYPER_STRENGTH = 0.4;
constexpr qreal CURSED_STRENGTH = 0.65;

/// The position within a cycle, 0 to 1, taken from the clock so that every
/// viewer is at the same point of the cycle at the same moment.
qreal phase(qreal seconds, qreal period)
{
    if (period <= 0)
    {
        return 0;
    }

    auto cycles = seconds / period;
    return cycles - std::floor(cycles);
}

}  // namespace

namespace chatterino {

bool EmoteEffectAnimation::isAnimated() const
{
    return this->offset != QPointF{} || this->rotation != 0 ||
           this->scale != 1 || this->tintStrength != 0;
}

bool isTimeVaryingEffect(EmoteEffectKind kind)
{
    switch (kind)
    {
        case EmoteEffectKind::Party:
        case EmoteEffectKind::Rainbow:
        case EmoteEffectKind::Hyper:
        case EmoteEffectKind::Shake:
        case EmoteEffectKind::Spin:
        case EmoteEffectKind::Jam:
        case EmoteEffectKind::Bounce:
        case EmoteEffectKind::Slide:
            return true;

        // Cursed only darkens, so it is drawn once and left alone.
        //
        // Arrive and leave describe an emote entering or departing, which needs
        // to know when the message appeared. A layout element does not, and may
        // be rebuilt at any time, so they are accepted and ignored rather than
        // played as an endless loop, which is not what they mean.
        default:
            return false;
    }
}

bool hasTimeVaryingEffect(const EmoteEffectSet &effects)
{
    for (auto kind : effects.kinds())
    {
        if (isTimeVaryingEffect(kind))
        {
            return true;
        }
    }
    return false;
}

EmoteEffectAnimation computeEmoteEffectAnimation(const EmoteEffectSet &effects,
                                                 qreal seconds,
                                                 qreal emoteHeight)
{
    EmoteEffectAnimation animation;

    for (auto kind : effects.kinds())
    {
        switch (kind)
        {
            case EmoteEffectKind::Spin:
                animation.rotation += phase(seconds, SPIN_PERIOD) * 360.0;
                break;

            case EmoteEffectKind::Jam:
                // Rocks back and forth rather than turning all the way round.
                animation.rotation +=
                    std::sin(phase(seconds, JAM_PERIOD) * TAU) * JAM_DEGREES;
                break;

            case EmoteEffectKind::Bounce: {
                // Half a sine over the cycle: one arc that leaves the ground at
                // the start, peaks in the middle and lands at the end. A full
                // sine would bounce twice per cycle, and taking its magnitude
                // would put a kink at the top rather than at the bottom.
                auto height = std::sin(phase(seconds, BOUNCE_PERIOD) * PI);
                animation.offset.ry() -= height * emoteHeight * BOUNCE_HEIGHT;
                break;
            }

            case EmoteEffectKind::Slide:
                animation.offset.rx() +=
                    std::sin(phase(seconds, SLIDE_PERIOD) * TAU) * emoteHeight *
                    SLIDE_WIDTH;
                break;

            case EmoteEffectKind::Shake:
                // Two different rates, so the motion does not read as a neat
                // circle.
                animation.offset.rx() +=
                    std::sin(phase(seconds, SHAKE_PERIOD) * TAU) * emoteHeight *
                    SHAKE_AMOUNT;
                animation.offset.ry() +=
                    std::sin(phase(seconds, SHAKE_PERIOD * 1.7) * TAU) *
                    emoteHeight * SHAKE_AMOUNT;
                break;

            case EmoteEffectKind::Hyper:
                animation.offset.rx() +=
                    std::sin(phase(seconds, HYPER_SHAKE_PERIOD) * TAU) *
                    emoteHeight * HYPER_SHAKE_AMOUNT;
                animation.offset.ry() +=
                    std::sin(phase(seconds, HYPER_SHAKE_PERIOD * 1.3) * TAU) *
                    emoteHeight * HYPER_SHAKE_AMOUNT;
                animation.tint = QColor::fromRgb(255, 40, 40);
                animation.tintStrength = HYPER_STRENGTH;
                break;

            case EmoteEffectKind::Party:
                animation.tint = QColor::fromHsvF(phase(seconds, HUE_PERIOD),
                                                  0.85, 1.0);
                animation.tintStrength = PARTY_STRENGTH;
                break;

            case EmoteEffectKind::Rainbow:
                animation.tint = QColor::fromHsvF(phase(seconds, HUE_PERIOD),
                                                  0.95, 1.0);
                animation.tintStrength = RAINBOW_STRENGTH;
                break;

            case EmoteEffectKind::Cursed:
                // Approximated by laying a dark, desaturated colour over the
                // emote. A true desaturation would mean rewriting every pixel
                // each time the emote is drawn.
                animation.tint = QColor::fromRgb(40, 40, 48);
                animation.tintStrength = CURSED_STRENGTH;
                break;

            default:
                break;
        }
    }

    return animation;
}

QPixmap tintedPixmap(const QPixmap &source, const QColor &tint, qreal strength)
{
    if (source.isNull() || !tint.isValid() || strength <= 0)
    {
        return source;
    }

    QPixmap layer(source.size());
    // Transparent to begin with, so that wherever the emote is transparent the
    // tint has nothing to sit on and the background shows through untouched.
    layer.fill(Qt::transparent);

    QPainter painter(&layer);
    painter.drawPixmap(0, 0, source);

    // SourceAtop keeps the destination's alpha, which here is the emote's own
    // shape, so the colour is confined to it.
    painter.setCompositionMode(QPainter::CompositionMode_SourceAtop);

    auto colour = tint;
    colour.setAlphaF(static_cast<float>(std::min(strength, 1.0)));
    painter.fillRect(layer.rect(), colour);

    return layer;
}

}  // namespace chatterino

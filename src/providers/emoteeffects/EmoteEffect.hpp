// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

namespace chatterino {

/// What an effect does to the emote it is attached to.
enum class EmoteEffectKind : std::uint8_t {
    /// Geometry. At most one of these applies to a given emote.
    Wide,         ///< Four times as wide.
    GrowX,        ///< Twice as wide.
    FlipX,        ///< Mirrored horizontally.
    FlipY,        ///< Mirrored vertically.
    RotateLeft,   ///< Quarter turn anticlockwise.
    RotateRight,  ///< Quarter turn clockwise.

    /// Spacing.
    NoSpace,  ///< Butts against the preceding emote, with no gap.

    /// Colour.
    Cursed,   ///< Desaturated and darkened.
    Party,    ///< Cycles through colours.
    Rainbow,  ///< Hue runs across the emote.
    Hyper,    ///< Red tint with a fast shake.

    /// Motion.
    Shake,
    Spin,
    Jam,
    Bounce,
    Slide,
    Arrive,
    Leave,
};

/// Who defines an effect. Codes from different providers can coexist, and a
/// message may mix them.
enum class EmoteEffectProvider : std::uint8_t {
    Bttv,
    Ffz,
    /// Defined by the Chatterino+ service rather than an emote provider.
    Plus,
};

/// Where the code sits relative to the emote it modifies.
enum class EmoteEffectPosition : std::uint8_t {
    Before,
    After,
};

/// Which emotes an effect may be applied to.
enum class EmoteEffectScope : std::uint8_t {
    /// Only 7TV, BTTV and FFZ emotes; Twitch's own emotes are left alone.
    ThirdPartyOnly,
    Any,
};

/// What the message's author must be for an effect to apply. An effect the
/// author is not entitled to is ignored, and its code stays as plain text.
enum class EmoteEffectRequirement : std::uint8_t {
    None,
    PlusSubscriber,
    FfzSupporter,
};

/// One modifier code and what it does.
struct EmoteEffectDefinition {
    QString code;
    EmoteEffectProvider provider = EmoteEffectProvider::Bttv;
    EmoteEffectPosition position = EmoteEffectPosition::Before;
    EmoteEffectScope scope = EmoteEffectScope::ThirdPartyOnly;
    bool caseSensitive = true;
    EmoteEffectRequirement requirement = EmoteEffectRequirement::None;
    EmoteEffectKind kind = EmoteEffectKind::Wide;
};

/// True when `kind` changes the emote's geometry. Only one such effect applies
/// to an emote: two rotations or a rotation and a flip do not compose.
bool isGeometryEffect(EmoteEffectKind kind);

/// What the author of a message is entitled to use.
struct EmoteEffectEntitlements {
    bool plusSubscriber = false;
    bool ffzSupporter = false;

    /// Whether `requirement` is satisfied.
    [[nodiscard]] bool satisfies(EmoteEffectRequirement requirement) const;
};

/// The effects gathered on a single emote.
///
/// Effects accumulate, except that the first geometry effect wins: the
/// specification is explicit that rotations and flips do not stack.
class EmoteEffectSet
{
public:
    /// Adds `kind`, and reports whether it was taken. A geometry effect is
    /// refused when one is already present; a duplicate of any kind is refused.
    bool add(EmoteEffectKind kind);

    [[nodiscard]] bool has(EmoteEffectKind kind) const;
    [[nodiscard]] bool empty() const;
    [[nodiscard]] std::size_t size() const;

    /// The geometry effect in force, if any.
    [[nodiscard]] std::optional<EmoteEffectKind> geometry() const;

    /// The effects in the order they were accepted.
    [[nodiscard]] const std::vector<EmoteEffectKind> &kinds() const;

private:
    std::vector<EmoteEffectKind> kinds_;
};

}  // namespace chatterino

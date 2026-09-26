// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "providers/emoteeffects/EmoteEffect.hpp"
#include "providers/emoteeffects/EmoteEffectRegistry.hpp"

#include <QString>

#include <vector>

namespace chatterino {

/// One word of a message, already classified as an emote or as plain text.
struct EffectToken {
    bool isEmote = false;
    /// The word, or the emote's name.
    QString text;
    /// Whether the emote came from 7TV, BTTV or FFZ rather than Twitch. Only
    /// meaningful when `isEmote` is set.
    bool thirdParty = false;
    /// Opaque to resolution and carried through to the result, so a caller can
    /// find what a resolved token came from. Resolution drops consumed
    /// modifiers and moves restored ones, so position alone cannot be relied on.
    std::size_t sourceIndex = 0;
};

/// A word after modifier codes have been resolved.
struct ResolvedEffectToken {
    bool isEmote = false;
    QString text;
    /// Carried through from the input, because a modifier written after an
    /// emote still has to know whether its provider covers that emote.
    bool thirdParty = false;
    /// The index of the input token this came from.
    std::size_t sourceIndex = 0;
    /// The effects that applied, for an emote. Empty for plain text.
    EmoteEffectSet effects;
};

/// Attaches modifier codes to the emotes they decorate.
///
/// A code that cannot apply stays in the message as ordinary text, which is the
/// only safe default: a word that merely looks like a modifier must still be
/// readable. That covers a code with no emote beside it, one used on an emote
/// its provider does not cover, and one the author is not entitled to.
///
/// Codes written before an emote accumulate until the emote arrives, so several
/// can decorate one emote. Effects otherwise stack, except that the first
/// geometry effect wins.
std::vector<ResolvedEffectToken> applyEmoteEffects(
    const std::vector<EffectToken> &tokens,
    const EmoteEffectRegistry &registry,
    const EmoteEffectEntitlements &entitlements);

}  // namespace chatterino

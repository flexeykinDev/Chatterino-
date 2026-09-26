// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/emoteeffects/EmoteEffectParser.hpp"

namespace {

using namespace chatterino;

/// A modifier written before an emote, waiting for the emote to arrive.
struct PendingModifier {
    EmoteEffectDefinition definition;
    /// Kept verbatim so the word can be restored if it turns out not to modify
    /// anything.
    QString text;
};

bool appliesTo(const EmoteEffectDefinition &definition, bool thirdPartyEmote)
{
    if (definition.scope == EmoteEffectScope::Any)
    {
        return true;
    }
    return thirdPartyEmote;
}

ResolvedEffectToken asText(QString text)
{
    return {
        .isEmote = false,
        .text = std::move(text),
        .thirdParty = false,
        .effects = {},
    };
}

}  // namespace

namespace chatterino {

std::vector<ResolvedEffectToken> applyEmoteEffects(
    const std::vector<EffectToken> &tokens,
    const EmoteEffectRegistry &registry,
    const EmoteEffectEntitlements &entitlements)
{
    std::vector<ResolvedEffectToken> out;
    out.reserve(tokens.size());

    std::vector<PendingModifier> pending;

    // Modifiers that never found an emote are ordinary words again.
    auto releasePending = [&] {
        for (auto &modifier : pending)
        {
            out.push_back(asText(std::move(modifier.text)));
        }
        pending.clear();
    };

    for (const auto &token : tokens)
    {
        if (token.isEmote)
        {
            ResolvedEffectToken resolved{
                .isEmote = true,
                .text = token.text,
                .thirdParty = token.thirdParty,
                .effects = {},
            };

            // A modifier whose provider does not cover this emote is not a
            // modifier here, so it goes back to being a word, and must stay
            // ahead of the emote it was written before.
            std::vector<PendingModifier> rejected;
            for (auto &modifier : pending)
            {
                if (appliesTo(modifier.definition, token.thirdParty))
                {
                    resolved.effects.add(modifier.definition.kind);
                }
                else
                {
                    rejected.push_back(std::move(modifier));
                }
            }
            pending.clear();

            for (auto &modifier : rejected)
            {
                out.push_back(asText(std::move(modifier.text)));
            }

            out.push_back(std::move(resolved));
            continue;
        }

        // A word that trails an emote binds to it, which takes precedence over
        // the same word starting a new run of modifiers.
        if (pending.empty() && !out.empty() && out.back().isEmote)
        {
            auto &target = out.back();
            auto after =
                registry.lookup(token.text, EmoteEffectPosition::After);
            if (after && entitlements.satisfies(after->requirement) &&
                appliesTo(*after, target.thirdParty))
            {
                // Consume the word whether or not the effect was new: a repeat
                // is still a modifier, it simply adds nothing.
                target.effects.add(after->kind);
                continue;
            }
        }

        auto before = registry.lookup(token.text, EmoteEffectPosition::Before);
        if (before && entitlements.satisfies(before->requirement))
        {
            pending.push_back({
                .definition = *before,
                .text = token.text,
            });
            continue;
        }

        // An ordinary word ends any run of pending modifiers.
        releasePending();
        out.push_back(asText(token.text));
    }

    releasePending();

    return out;
}

}  // namespace chatterino

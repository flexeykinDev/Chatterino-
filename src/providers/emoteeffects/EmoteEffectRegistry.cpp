// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/emoteeffects/EmoteEffectRegistry.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

namespace {

using namespace chatterino;

std::optional<EmoteEffectProvider> providerFromString(const QString &name)
{
    if (name == "bttv")
    {
        return EmoteEffectProvider::Bttv;
    }
    if (name == "ffz")
    {
        return EmoteEffectProvider::Ffz;
    }
    if (name == "plus")
    {
        return EmoteEffectProvider::Plus;
    }
    return std::nullopt;
}

std::optional<EmoteEffectPosition> positionFromString(const QString &name)
{
    if (name == "before")
    {
        return EmoteEffectPosition::Before;
    }
    if (name == "after")
    {
        return EmoteEffectPosition::After;
    }
    return std::nullopt;
}

std::optional<EmoteEffectScope> scopeFromString(const QString &name)
{
    if (name == "third_party")
    {
        return EmoteEffectScope::ThirdPartyOnly;
    }
    if (name == "any")
    {
        return EmoteEffectScope::Any;
    }
    return std::nullopt;
}

/// An absent requirement means the effect is open to everyone.
std::optional<EmoteEffectRequirement> requirementFromJson(
    const QJsonValue &value)
{
    if (value.isUndefined() || value.isNull())
    {
        return EmoteEffectRequirement::None;
    }

    const auto name = value.toString();
    if (name == "plus_subscriber")
    {
        return EmoteEffectRequirement::PlusSubscriber;
    }
    if (name == "ffz_supporter")
    {
        return EmoteEffectRequirement::FfzSupporter;
    }
    return std::nullopt;
}

}  // namespace

namespace chatterino {

std::optional<EmoteEffectKind> emoteEffectKindFromString(const QString &name)
{
    static const QHash<QString, EmoteEffectKind> byName{
        {"wide", EmoteEffectKind::Wide},
        {"grow_x", EmoteEffectKind::GrowX},
        {"flip_x", EmoteEffectKind::FlipX},
        {"flip_y", EmoteEffectKind::FlipY},
        {"rotate_left", EmoteEffectKind::RotateLeft},
        {"rotate_right", EmoteEffectKind::RotateRight},
        {"no_space", EmoteEffectKind::NoSpace},
        {"cursed", EmoteEffectKind::Cursed},
        {"party", EmoteEffectKind::Party},
        {"rainbow", EmoteEffectKind::Rainbow},
        {"hyper", EmoteEffectKind::Hyper},
        {"shake", EmoteEffectKind::Shake},
        {"spin", EmoteEffectKind::Spin},
        {"jam", EmoteEffectKind::Jam},
        {"bounce", EmoteEffectKind::Bounce},
        {"slide", EmoteEffectKind::Slide},
        {"arrive", EmoteEffectKind::Arrive},
        {"leave", EmoteEffectKind::Leave},
    };

    auto it = byName.find(name);
    if (it == byName.end())
    {
        return std::nullopt;
    }
    return it.value();
}

QString emoteEffectKindToString(EmoteEffectKind kind)
{
    switch (kind)
    {
        case EmoteEffectKind::Wide:
            return "wide";
        case EmoteEffectKind::GrowX:
            return "grow_x";
        case EmoteEffectKind::FlipX:
            return "flip_x";
        case EmoteEffectKind::FlipY:
            return "flip_y";
        case EmoteEffectKind::RotateLeft:
            return "rotate_left";
        case EmoteEffectKind::RotateRight:
            return "rotate_right";
        case EmoteEffectKind::NoSpace:
            return "no_space";
        case EmoteEffectKind::Cursed:
            return "cursed";
        case EmoteEffectKind::Party:
            return "party";
        case EmoteEffectKind::Rainbow:
            return "rainbow";
        case EmoteEffectKind::Hyper:
            return "hyper";
        case EmoteEffectKind::Shake:
            return "shake";
        case EmoteEffectKind::Spin:
            return "spin";
        case EmoteEffectKind::Jam:
            return "jam";
        case EmoteEffectKind::Bounce:
            return "bounce";
        case EmoteEffectKind::Slide:
            return "slide";
        case EmoteEffectKind::Arrive:
            return "arrive";
        case EmoteEffectKind::Leave:
            return "leave";
    }

    return {};
}

EmoteEffectRegistry EmoteEffectRegistry::builtin()
{
    EmoteEffectRegistry registry;

    // BetterTTV's modifiers are written before the emote and cover third-party
    // emotes only.
    struct BttvCode {
        const char *code;
        EmoteEffectKind kind;
    };
    static constexpr BttvCode BTTV_CODES[]{
        {"w!", EmoteEffectKind::Wide},
        {"h!", EmoteEffectKind::FlipX},
        {"v!", EmoteEffectKind::FlipY},
        {"z!", EmoteEffectKind::NoSpace},
        {"c!", EmoteEffectKind::Cursed},
        {"l!", EmoteEffectKind::RotateLeft},
        {"r!", EmoteEffectKind::RotateRight},
        {"p!", EmoteEffectKind::Party},
        {"s!", EmoteEffectKind::Shake},
    };

    for (const auto &entry : BTTV_CODES)
    {
        registry.add({
            .code = entry.code,
            .provider = EmoteEffectProvider::Bttv,
            .position = EmoteEffectPosition::Before,
            .scope = EmoteEffectScope::ThirdPartyOnly,
            .caseSensitive = true,
            .requirement = EmoteEffectRequirement::None,
            .kind = entry.kind,
        });
    }

    // FrankerFaceZ's are written after the emote and apply to any emote. Most
    // of them are reserved for people who support FFZ.
    struct FfzCode {
        const char *code;
        EmoteEffectKind kind;
        EmoteEffectRequirement requirement;
    };
    static constexpr FfzCode FFZ_CODES[]{
        {"ffzW", EmoteEffectKind::GrowX, EmoteEffectRequirement::None},
        {"ffzX", EmoteEffectKind::FlipX, EmoteEffectRequirement::None},
        {"ffzY", EmoteEffectKind::FlipY, EmoteEffectRequirement::None},
        {"ffzCursed", EmoteEffectKind::Cursed, EmoteEffectRequirement::None},
        {"ffzSpin", EmoteEffectKind::Spin,
         EmoteEffectRequirement::FfzSupporter},
        {"ffzRainbow", EmoteEffectKind::Rainbow,
         EmoteEffectRequirement::FfzSupporter},
        {"ffzHyper", EmoteEffectKind::Hyper,
         EmoteEffectRequirement::FfzSupporter},
        {"ffzJam", EmoteEffectKind::Jam, EmoteEffectRequirement::FfzSupporter},
        {"ffzBounce", EmoteEffectKind::Bounce,
         EmoteEffectRequirement::FfzSupporter},
        {"ffzSlide", EmoteEffectKind::Slide,
         EmoteEffectRequirement::FfzSupporter},
        {"ffzArrive", EmoteEffectKind::Arrive,
         EmoteEffectRequirement::FfzSupporter},
        {"ffzLeave", EmoteEffectKind::Leave,
         EmoteEffectRequirement::FfzSupporter},
    };

    for (const auto &entry : FFZ_CODES)
    {
        registry.add({
            .code = entry.code,
            .provider = EmoteEffectProvider::Ffz,
            .position = EmoteEffectPosition::After,
            .scope = EmoteEffectScope::Any,
            .caseSensitive = true,
            .requirement = entry.requirement,
            .kind = entry.kind,
        });
    }

    return registry;
}

EmoteEffectRegistry EmoteEffectRegistry::fromJson(const QJsonObject &root)
{
    EmoteEffectRegistry registry;

    const auto entries = root.value("effects").toArray();
    for (const auto &entry : entries)
    {
        const auto object = entry.toObject();

        const auto code = object.value("code").toString();
        if (code.isEmpty())
        {
            continue;
        }

        const auto provider = providerFromString(object.value("provider").toString());
        const auto position = positionFromString(object.value("position").toString());
        const auto scope = scopeFromString(object.value("applies_to").toString());
        const auto requirement = requirementFromJson(object.value("requires"));
        const auto kind = emoteEffectKindFromString(object.value("effect").toString());

        // A single unrecognised entry must not cost us the rest of the
        // catalogue: newer servers may describe effects this build predates.
        if (!provider || !position || !scope || !requirement || !kind)
        {
            continue;
        }

        registry.add({
            .code = code,
            .provider = *provider,
            .position = *position,
            .scope = *scope,
            // The field is optional; codes are case-sensitive unless told
            // otherwise, which is the stricter reading.
            .caseSensitive = object.value("case_sensitive").toBool(true),
            .requirement = *requirement,
            .kind = *kind,
        });
    }

    return registry;
}

void EmoteEffectRegistry::add(EmoteEffectDefinition definition)
{
    const auto lowered = definition.code.toLower();
    this->definitions_.push_back(std::move(definition));
    this->byLoweredCode_[lowered].push_back(this->definitions_.size() - 1);
}

std::optional<EmoteEffectDefinition> EmoteEffectRegistry::lookup(
    const QString &word, EmoteEffectPosition position) const
{
    auto it = this->byLoweredCode_.find(word.toLower());
    if (it == this->byLoweredCode_.end())
    {
        return std::nullopt;
    }

    std::optional<EmoteEffectDefinition> caseInsensitiveMatch;

    for (auto index : it.value())
    {
        const auto &definition = this->definitions_[index];
        if (definition.position != position)
        {
            continue;
        }

        if (definition.caseSensitive)
        {
            if (definition.code == word)
            {
                // An exact match is unambiguous, so stop looking.
                return definition;
            }
            continue;
        }

        if (!caseInsensitiveMatch)
        {
            caseInsensitiveMatch = definition;
        }
    }

    return caseInsensitiveMatch;
}

const std::vector<EmoteEffectDefinition> &EmoteEffectRegistry::definitions()
    const
{
    return this->definitions_;
}

bool EmoteEffectRegistry::empty() const
{
    return this->definitions_.empty();
}

std::size_t EmoteEffectRegistry::size() const
{
    return this->definitions_.size();
}

}  // namespace chatterino

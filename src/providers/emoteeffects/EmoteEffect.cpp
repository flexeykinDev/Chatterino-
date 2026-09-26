// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/emoteeffects/EmoteEffect.hpp"

#include <algorithm>

namespace chatterino {

bool isGeometryEffect(EmoteEffectKind kind)
{
    switch (kind)
    {
        case EmoteEffectKind::Wide:
        case EmoteEffectKind::GrowX:
        case EmoteEffectKind::FlipX:
        case EmoteEffectKind::FlipY:
        case EmoteEffectKind::RotateLeft:
        case EmoteEffectKind::RotateRight:
            return true;

        default:
            return false;
    }
}

bool EmoteEffectEntitlements::satisfies(
    EmoteEffectRequirement requirement) const
{
    switch (requirement)
    {
        case EmoteEffectRequirement::None:
            return true;
        case EmoteEffectRequirement::PlusSubscriber:
            return this->plusSubscriber;
        case EmoteEffectRequirement::FfzSupporter:
            return this->ffzSupporter;
    }

    return false;
}

bool EmoteEffectSet::add(EmoteEffectKind kind)
{
    if (this->has(kind))
    {
        return false;
    }

    // Geometry effects do not compose: a second rotation or flip is dropped
    // rather than applied on top of the first.
    if (isGeometryEffect(kind) && this->geometry().has_value())
    {
        return false;
    }

    this->kinds_.push_back(kind);
    return true;
}

bool EmoteEffectSet::has(EmoteEffectKind kind) const
{
    return std::ranges::find(this->kinds_, kind) != this->kinds_.end();
}

bool EmoteEffectSet::empty() const
{
    return this->kinds_.empty();
}

std::size_t EmoteEffectSet::size() const
{
    return this->kinds_.size();
}

std::optional<EmoteEffectKind> EmoteEffectSet::geometry() const
{
    auto it = std::ranges::find_if(this->kinds_, isGeometryEffect);
    if (it == this->kinds_.end())
    {
        return std::nullopt;
    }

    return *it;
}

const std::vector<EmoteEffectKind> &EmoteEffectSet::kinds() const
{
    return this->kinds_;
}

}  // namespace chatterino

// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/emoteeffects/EmoteEffectController.hpp"

#include <utility>

namespace chatterino {

const EmoteEffectRegistry &EmoteEffectController::registry() const
{
    return this->registry_;
}

void EmoteEffectController::setRegistry(EmoteEffectRegistry registry)
{
    this->registry_ = std::move(registry);
}

EmoteEffectEntitlements EmoteEffectController::entitlements() const
{
    return this->entitlements_;
}

void EmoteEffectController::setEntitlements(
    EmoteEffectEntitlements entitlements)
{
    this->entitlements_ = entitlements;
}

}  // namespace chatterino

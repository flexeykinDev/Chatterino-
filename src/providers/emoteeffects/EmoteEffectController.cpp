// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/emoteeffects/EmoteEffectController.hpp"

#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"

#include <utility>

namespace chatterino {

void EmoteEffectController::load(const QString &url)
{
    if (url.isEmpty())
    {
        return;
    }

    NetworkRequest(url)
        .timeout(20000)
        .onSuccess([this](const NetworkResult &result) {
            auto registry = EmoteEffectRegistry::fromJson(result.parseJson());

            // A response that parsed to nothing usable is treated as a failed
            // fetch rather than as an instruction to forget every code we have.
            if (registry.empty())
            {
                qCWarning(chatterinoApp)
                    << "Emote effect catalogue had no usable entries; keeping "
                       "the built-in codes";
                return;
            }

            this->setRegistry(std::move(registry));
        })
        .onError([](const NetworkResult &result) {
            qCWarning(chatterinoApp)
                << "Failed to fetch the emote effect catalogue:"
                << result.formatError();
        })
        .execute();
}

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

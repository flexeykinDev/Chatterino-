// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "providers/emoteeffects/EmoteEffect.hpp"
#include "providers/emoteeffects/EmoteEffectRegistry.hpp"

namespace chatterino {

/// Holds the modifier catalogue the message builder resolves against.
///
/// The catalogue starts as the codes compiled in and is replaced once the
/// effects endpoint has been read, so a running client picks up new codes
/// without a restart.
class EmoteEffectController
{
public:
    EmoteEffectController() = default;
    virtual ~EmoteEffectController() = default;

    EmoteEffectController(const EmoteEffectController &) = delete;
    EmoteEffectController &operator=(const EmoteEffectController &) = delete;
    EmoteEffectController(EmoteEffectController &&) = delete;
    EmoteEffectController &operator=(EmoteEffectController &&) = delete;

    /// Fetches the catalogue from `url` and replaces the current one.
    ///
    /// An empty url does nothing, leaving the compiled-in codes in place, and
    /// so does a failed or unparseable response: the client stays usable when
    /// the endpoint is unreachable, just without whatever codes it adds.
    virtual void load(const QString &url);

    [[nodiscard]] virtual const EmoteEffectRegistry &registry() const;

    /// Replaces the catalogue. An empty one disables effect resolution
    /// entirely, which is what makes the endpoint being unreachable harmless.
    virtual void setRegistry(EmoteEffectRegistry registry);

    /// What the local user may use. Effects the author is not entitled to are
    /// left as plain text.
    [[nodiscard]] virtual EmoteEffectEntitlements entitlements() const;
    virtual void setEntitlements(EmoteEffectEntitlements entitlements);

private:
    EmoteEffectRegistry registry_ = EmoteEffectRegistry::builtin();
    EmoteEffectEntitlements entitlements_;
};

}  // namespace chatterino

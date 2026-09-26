// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "providers/emoteeffects/EmoteEffect.hpp"

#include <QHash>
#include <QString>

#include <optional>
#include <vector>

class QJsonObject;

namespace chatterino {

/// The set of modifier codes the client understands.
///
/// The catalogue is data, not code: it is fetched from the effects endpoint so
/// that new codes do not need a new build. Only the handful of codes the
/// specification itself documents are compiled in, as a fallback for when the
/// endpoint cannot be reached.
class EmoteEffectRegistry
{
public:
    /// The codes documented in the specification. Deliberately small: guessing
    /// at a wider table risks silently eating words that are not modifiers.
    static EmoteEffectRegistry builtin();

    /// Parses the effects endpoint's response. Entries that are malformed, or
    /// that name an effect this build does not know, are skipped rather than
    /// failing the whole catalogue, so one unknown code cannot break the rest.
    static EmoteEffectRegistry fromJson(const QJsonObject &root);

    void add(EmoteEffectDefinition definition);

    /// Finds the definition for `word` at `position`, or nothing when the word
    /// is not a modifier there. Case-sensitive definitions are preferred over
    /// case-insensitive ones when both could match.
    [[nodiscard]] std::optional<EmoteEffectDefinition> lookup(
        const QString &word, EmoteEffectPosition position) const;

    [[nodiscard]] const std::vector<EmoteEffectDefinition> &definitions() const;
    [[nodiscard]] bool empty() const;
    [[nodiscard]] std::size_t size() const;

private:
    std::vector<EmoteEffectDefinition> definitions_;
    /// Lower-cased code to indices, so lookup does not scan the whole table.
    QHash<QString, std::vector<std::size_t>> byLoweredCode_;
};

/// Maps the specification's effect name to a kind, or nothing when this build
/// does not implement it.
std::optional<EmoteEffectKind> emoteEffectKindFromString(const QString &name);

/// The inverse, for round-tripping and diagnostics.
QString emoteEffectKindToString(EmoteEffectKind kind);

}  // namespace chatterino

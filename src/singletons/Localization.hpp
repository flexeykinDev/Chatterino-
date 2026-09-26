// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>

#include <vector>

namespace chatterino {

/// A language the interface can be displayed in.
struct LanguageDescriptor {
    /// An ISO 639-1 code, or an empty string meaning "follow the system".
    QString code;
    /// The language's own name for itself, which is what a picker should show:
    /// someone looking for their language will not recognise its English name.
    QString nativeName;
};

/// Loads and installs the translators that localise the interface.
///
/// Source strings are English, so English needs no translation file; every other
/// language loads a compiled catalogue from the embedded resources. Qt's own
/// catalogue is loaded alongside it where available, so standard dialog buttons
/// are translated too.
class Localization
{
public:
    /// The languages this build ships, "follow the system" first.
    static const std::vector<LanguageDescriptor> &languages();

    /// Whether `code` names a language this build ships. An empty code, meaning
    /// "follow the system", counts as known.
    static bool isKnown(const QString &code);

    /// Installs the translators for `code`, replacing any already installed.
    ///
    /// An empty code follows the system locale, falling back to English when the
    /// system language is not one we ship. Returns the code actually applied,
    /// which lets a caller record what the user ended up with.
    ///
    /// Strings already shown keep their old language: Qt only retranslates
    /// widgets that are rebuilt, so callers changing language at runtime should
    /// tell the user a restart is needed.
    static QString apply(const QString &code);

    /// The code currently applied, or an empty string before the first apply().
    static QString current();
};

}  // namespace chatterino

// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "singletons/Localization.hpp"

#include "common/QLogging.hpp"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

#include <memory>

namespace {

using namespace chatterino;

/// The code used when no catalogue is loaded, because the source strings are
/// already in this language.
const QString SOURCE_LANGUAGE = QStringLiteral("en");

/// Installed translators, kept alive because Qt does not take ownership.
struct InstalledTranslators {
    std::unique_ptr<QTranslator> application;
    std::unique_ptr<QTranslator> qt;
    QString code;
};

InstalledTranslators &installed()
{
    // Function-local so there is no dependency on static initialisation order.
    static InstalledTranslators state;
    return state;
}

void removeIfInstalled(std::unique_ptr<QTranslator> &slot)
{
    if (slot)
    {
        QCoreApplication::removeTranslator(slot.get());
        slot.reset();
    }
}

/// Reduces a locale name such as "ru_RU" to the language part we ship files for.
QString languagePartOf(const QString &localeName)
{
    return localeName.left(localeName.indexOf('_'));
}

/// Loads the application catalogue for `code` from the embedded resources.
std::unique_ptr<QTranslator> loadApplicationCatalogue(const QString &code)
{
    auto translator = std::make_unique<QTranslator>();
    if (translator->load(QStringLiteral(":/translations/chatterino_%1.qm")
                             .arg(code)))
    {
        return translator;
    }

    return nullptr;
}

/// Loads Qt's own catalogue so standard dialogs are translated as well. This is
/// best-effort: a build or package that ships no Qt catalogues is still usable,
/// it just leaves those few strings in English.
std::unique_ptr<QTranslator> loadQtCatalogue(const QString &code)
{
    // Qt's own strings — the buttons on standard dialogs, chiefly — live in a
    // catalogue this application does not ship. Where it sits depends on how
    // the build was put together:
    //
    //  - a development build finds it in the Qt installation it was built
    //    against, as qtbase_<code>;
    //  - a deployed build has whatever windeployqt copied next to the
    //    executable, which is the combined qt_<code> rather than qtbase_<code>;
    //  - and on a machine with no Qt installed, the first path does not exist
    //    at all.
    //
    // Missing it is not loud. Everything this fork writes stays translated and
    // a stray "Cancel" sits in the corner of an otherwise Russian dialog.
    const QString directories[] = {
        QLibraryInfo::path(QLibraryInfo::TranslationsPath),
        QCoreApplication::applicationDirPath() + QStringLiteral("/translations"),
    };

    for (const auto &directory : directories)
    {
        for (const auto *prefix : {"qtbase_", "qt_"})
        {
            auto translator = std::make_unique<QTranslator>();
            if (translator->load(QLatin1String(prefix) + code, directory))
            {
                return translator;
            }
        }
    }

    return nullptr;
}

}  // namespace

namespace chatterino {

const std::vector<LanguageDescriptor> &Localization::languages()
{
    static const std::vector<LanguageDescriptor> all{
        {QString(), QStringLiteral("System default")},
        {QStringLiteral("en"), QStringLiteral("English")},
        {QStringLiteral("ru"), QStringLiteral("Русский")},
    };
    return all;
}

bool Localization::isKnown(const QString &code)
{
    for (const auto &language : languages())
    {
        if (language.code == code)
        {
            return true;
        }
    }
    return false;
}

QString Localization::apply(const QString &code)
{
    auto &state = installed();

    QString target = code;
    if (target.isEmpty())
    {
        target = languagePartOf(QLocale::system().name());
        if (!isKnown(target) || target.isEmpty())
        {
            target = SOURCE_LANGUAGE;
        }
    }

    removeIfInstalled(state.application);
    removeIfInstalled(state.qt);

    if (target == SOURCE_LANGUAGE)
    {
        state.code = target;
        return target;
    }

    auto application = loadApplicationCatalogue(target);
    if (!application)
    {
        qCWarning(chatterinoApp).noquote()
            << "No translation catalogue for" << target
            << "- falling back to" << SOURCE_LANGUAGE;
        state.code = SOURCE_LANGUAGE;
        return SOURCE_LANGUAGE;
    }

    QCoreApplication::installTranslator(application.get());
    state.application = std::move(application);

    auto qt = loadQtCatalogue(target);
    if (qt)
    {
        QCoreApplication::installTranslator(qt.get());
        state.qt = std::move(qt);
    }

    state.code = target;
    return target;
}

QString Localization::current()
{
    return installed().code;
}

}  // namespace chatterino

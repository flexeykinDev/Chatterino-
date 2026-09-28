// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "singletons/Localization.hpp"

#include "messages/MessageElement.hpp"
#include "widgets/dialogs/CreatePollDialog.hpp"
#include "widgets/dialogs/GlobalBanHistoryPopup.hpp"
#include "widgets/dialogs/MentionsPopup.hpp"
#include "widgets/splits/ChatModesPopup.hpp"
#include "widgets/splits/RaidBannerWidget.hpp"
#include "widgets/dialogs/SettingsDialog.hpp"
#include "widgets/settingspages/HighlightingPage.hpp"
#include "widgets/settingspages/NotificationPage.hpp"

#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QMetaObject>
#include <QString>

using namespace chatterino;

namespace {

/// A string that exists in the Russian catalogue, used to prove a catalogue was
/// really loaded rather than merely compiled. If the settings dialog's tab
/// titles are ever reworded, this needs updating alongside them.
QString translatedSettingsTab()
{
    return QCoreApplication::translate("chatterino::SettingsDialog", "General");
}

/// Leaves the process back on the source language, so one test's choice of
/// language cannot leak into another's expectations.
class LocalizationTest : public ::testing::Test
{
protected:
    void TearDown() override
    {
        Localization::apply("en");
    }
};

}  // namespace

TEST_F(LocalizationTest, ListsSystemDefaultFirst)
{
    const auto &languages = Localization::languages();

    ASSERT_FALSE(languages.empty());
    // An empty code means "follow the system", and it should be the first
    // option a picker offers.
    EXPECT_EQ(languages.front().code, QString());
    EXPECT_FALSE(languages.front().nativeName.isEmpty());
}

TEST_F(LocalizationTest, EveryLanguageHasAName)
{
    for (const auto &language : Localization::languages())
    {
        EXPECT_FALSE(language.nativeName.isEmpty())
            << "language " << language.code.toStdString() << " has no name";
    }
}

TEST_F(LocalizationTest, RecognisesShippedLanguages)
{
    EXPECT_TRUE(Localization::isKnown(""));
    EXPECT_TRUE(Localization::isKnown("en"));
    EXPECT_TRUE(Localization::isKnown("ru"));
}

TEST_F(LocalizationTest, RejectsLanguagesNotShipped)
{
    EXPECT_FALSE(Localization::isKnown("de"));
    EXPECT_FALSE(Localization::isKnown("en_US"));
    EXPECT_FALSE(Localization::isKnown("nonsense"));
}

TEST_F(LocalizationTest, EnglishLeavesSourceStringsAlone)
{
    EXPECT_EQ(Localization::apply("en"), "en");
    EXPECT_EQ(translatedSettingsTab(), "General");
}

TEST_F(LocalizationTest, RussianTranslatesStrings)
{
    EXPECT_EQ(Localization::apply("ru"), "ru");

    // The point of the whole pipeline: a catalogue was compiled, embedded and
    // loaded, so a source string comes back translated.
    EXPECT_NE(translatedSettingsTab(), "General");
    EXPECT_EQ(translatedSettingsTab(), QString::fromUtf8("Общие"));
}

TEST_F(LocalizationTest, SwitchingBackRestoresSourceStrings)
{
    Localization::apply("ru");
    ASSERT_NE(translatedSettingsTab(), "General");

    Localization::apply("en");
    EXPECT_EQ(translatedSettingsTab(), "General");
}

TEST_F(LocalizationTest, ApplyingTwiceIsStable)
{
    Localization::apply("ru");
    const auto first = translatedSettingsTab();

    // Re-applying must replace the installed translator rather than stack
    // another one on top of it.
    Localization::apply("ru");
    EXPECT_EQ(translatedSettingsTab(), first);
}

TEST_F(LocalizationTest, UnshippedLanguageFallsBackToEnglish)
{
    EXPECT_EQ(Localization::apply("de"), "en");
    EXPECT_EQ(translatedSettingsTab(), "General");
}

TEST_F(LocalizationTest, FallingBackClearsAPreviousTranslation)
{
    Localization::apply("ru");
    ASSERT_NE(translatedSettingsTab(), "General");

    // Asking for a language with no catalogue must not leave the previous
    // language installed.
    EXPECT_EQ(Localization::apply("de"), "en");
    EXPECT_EQ(translatedSettingsTab(), "General");
}

/// A class whose strings are translated must carry Q_OBJECT, so that tr()
/// resolves against its own name. Without it, tr() silently falls back to the
/// base class's context, finds nothing, and the window stays in English while
/// the catalogue itself looks perfectly healthy.
///
/// These assertions are what catch that: translating through a hardcoded
/// context string cannot, because it bypasses the very lookup that breaks.
TEST_F(LocalizationTest, TranslatedClassesDeclareTheirOwnContext)
{
    EXPECT_STREQ(SettingsDialog::staticMetaObject.className(),
                 "chatterino::SettingsDialog");
    EXPECT_STREQ(HighlightingPage::staticMetaObject.className(),
                 "chatterino::HighlightingPage");
    EXPECT_STREQ(NotificationPage::staticMetaObject.className(),
                 "chatterino::NotificationPage");
    EXPECT_STREQ(GlobalBanHistoryPopup::staticMetaObject.className(),
                 "chatterino::GlobalBanHistoryPopup");
    EXPECT_STREQ(MentionsPopup::staticMetaObject.className(),
                 "chatterino::MentionsPopup");
    EXPECT_STREQ(ChatModesPopup::staticMetaObject.className(),
                 "chatterino::ChatModesPopup");
    EXPECT_STREQ(CreatePollDialog::staticMetaObject.className(),
                 "chatterino::CreatePollDialog");
    EXPECT_STREQ(RaidBannerWidget::staticMetaObject.className(),
                 "chatterino::RaidBannerWidget");
}

/// A class that is not a QObject gets its context from
/// Q_DECLARE_TR_FUNCTIONS instead, and the same trap applies: without it,
/// tr() resolves somewhere else and quietly returns English.
///
/// This asks for the string the way the message layout does, so a missing or
/// misspelled context shows up as an English tooltip here too.
TEST_F(LocalizationTest, NonQObjectClassesTranslateThroughTheirOwnContext)
{
    Localization::apply("ru");

    EXPECT_EQ(GlobalBanMarkerElement::tooltipFor(1),
              QString::fromUtf8(
                  "Забанен ещё на 1 канале. Нажмите, чтобы увидеть где и за "
                  "что."));
}

/// Russian has three plural forms, and %n picks between them. A catalogue with
/// only one filled in would pass the test above and still read wrongly for
/// every count but one.
TEST_F(LocalizationTest, PluralFormsAgreeWithTheCount)
{
    Localization::apply("ru");

    auto one = GlobalBanMarkerElement::tooltipFor(1);
    auto few = GlobalBanMarkerElement::tooltipFor(3);
    auto many = GlobalBanMarkerElement::tooltipFor(11);

    EXPECT_TRUE(one.contains(QString::fromUtf8("1 канале")));
    EXPECT_TRUE(few.contains(QString::fromUtf8("3 каналах")));
    EXPECT_TRUE(many.contains(QString::fromUtf8("11 каналах")));
}

/// Every context the catalogue carries must correspond to a real class name, or
/// its entries can never be found at runtime.
TEST_F(LocalizationTest, CatalogueContextsMatchRealClasses)
{
    Localization::apply("ru");

    // If SettingsDialog's context were wrong, this would return the source
    // string rather than the translation.
    EXPECT_EQ(QCoreApplication::translate(
                  SettingsDialog::staticMetaObject.className(), "General"),
              QString::fromUtf8("Общие"));
}

TEST_F(LocalizationTest, CurrentReportsWhatWasApplied)
{
    Localization::apply("ru");
    EXPECT_EQ(Localization::current(), "ru");

    Localization::apply("en");
    EXPECT_EQ(Localization::current(), "en");
}

TEST_F(LocalizationTest, SystemDefaultResolvesToAShippedLanguage)
{
    // Whatever the host locale is, following it must land on a language we
    // actually ship, never on an empty or unusable code.
    const auto applied = Localization::apply("");

    EXPECT_FALSE(applied.isEmpty());
    EXPECT_TRUE(Localization::isKnown(applied));
}

/// Qt's own strings come from a catalogue this application does not ship, and
/// where it lives depends on how the build was assembled. Missing it is quiet:
/// everything the fork writes stays translated, and a lone English "Cancel"
/// sits on an otherwise Russian dialog.
TEST_F(LocalizationTest, QtsOwnDialogButtonsAreTranslatedToo)
{
    Localization::apply("ru");

    auto cancel = QCoreApplication::translate("QPlatformTheme", "Cancel");

    EXPECT_NE(cancel, QStringLiteral("Cancel"))
        << "Qt's catalogue did not load, so standard dialog buttons stay in "
           "English";
}

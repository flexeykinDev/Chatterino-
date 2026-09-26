// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "singletons/Localization.hpp"

#include <gtest/gtest.h>
#include <QCoreApplication>
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

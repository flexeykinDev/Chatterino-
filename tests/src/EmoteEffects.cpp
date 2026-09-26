// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/emoteeffects/EmoteEffectParser.hpp"

#include "providers/emoteeffects/EmoteEffect.hpp"
#include "providers/emoteeffects/EmoteEffectRegistry.hpp"

#include <gtest/gtest.h>
#include <QJsonDocument>
#include <QJsonObject>

using namespace chatterino;

namespace {

EmoteEffectDefinition define(const QString &code, EmoteEffectPosition position,
                             EmoteEffectKind kind,
                             EmoteEffectScope scope = EmoteEffectScope::ThirdPartyOnly,
                             bool caseSensitive = true,
                             EmoteEffectRequirement requirement =
                                 EmoteEffectRequirement::None)
{
    return {
        .code = code,
        .provider = EmoteEffectProvider::Bttv,
        .position = position,
        .scope = scope,
        .caseSensitive = caseSensitive,
        .requirement = requirement,
        .kind = kind,
    };
}

/// A registry covering the cases the parser tests need.
EmoteEffectRegistry testRegistry()
{
    EmoteEffectRegistry registry;
    registry.add(define("w!", EmoteEffectPosition::Before, EmoteEffectKind::Wide));
    registry.add(
        define("shake!", EmoteEffectPosition::Before, EmoteEffectKind::Shake));
    registry.add(define("l!", EmoteEffectPosition::Before,
                        EmoteEffectKind::RotateLeft));
    registry.add(define("r!", EmoteEffectPosition::Before,
                        EmoteEffectKind::RotateRight));
    registry.add(define("anywhere!", EmoteEffectPosition::Before,
                        EmoteEffectKind::Party, EmoteEffectScope::Any));
    registry.add(define("ffzSpin", EmoteEffectPosition::After,
                        EmoteEffectKind::Spin, EmoteEffectScope::Any));
    registry.add(define("plusOnly", EmoteEffectPosition::Before,
                        EmoteEffectKind::Rainbow,
                        EmoteEffectScope::ThirdPartyOnly, true,
                        EmoteEffectRequirement::PlusSubscriber));
    return registry;
}

EffectToken word(const QString &text)
{
    return {.isEmote = false, .text = text, .thirdParty = false};
}

EffectToken emote(const QString &name, bool thirdParty = true)
{
    return {.isEmote = true, .text = name, .thirdParty = thirdParty};
}

/// Renders a resolved stream as "word" / "emote[effect,effect]" for readable
/// assertions.
QString describe(const std::vector<ResolvedEffectToken> &tokens)
{
    QStringList parts;
    for (const auto &token : tokens)
    {
        if (!token.isEmote)
        {
            parts.append(token.text);
            continue;
        }

        QStringList effects;
        for (auto kind : token.effects.kinds())
        {
            effects.append(emoteEffectKindToString(kind));
        }

        parts.append(effects.isEmpty()
                         ? token.text
                         : token.text + "[" + effects.join(",") + "]");
    }
    return parts.join(" ");
}

std::vector<ResolvedEffectToken> run(
    const std::vector<EffectToken> &tokens,
    EmoteEffectEntitlements entitlements = {})
{
    return applyEmoteEffects(tokens, testRegistry(), entitlements);
}

}  // namespace

TEST(EmoteEffectSet, StacksDistinctEffects)
{
    EmoteEffectSet set;

    EXPECT_TRUE(set.add(EmoteEffectKind::Shake));
    EXPECT_TRUE(set.add(EmoteEffectKind::Rainbow));
    EXPECT_EQ(set.size(), 2u);
}

TEST(EmoteEffectSet, IgnoresDuplicates)
{
    EmoteEffectSet set;

    EXPECT_TRUE(set.add(EmoteEffectKind::Shake));
    EXPECT_FALSE(set.add(EmoteEffectKind::Shake));
    EXPECT_EQ(set.size(), 1u);
}

TEST(EmoteEffectSet, KeepsOnlyTheFirstGeometryEffect)
{
    EmoteEffectSet set;

    // Rotations and flips do not compose: the first one wins.
    EXPECT_TRUE(set.add(EmoteEffectKind::RotateLeft));
    EXPECT_FALSE(set.add(EmoteEffectKind::RotateRight));
    EXPECT_FALSE(set.add(EmoteEffectKind::FlipX));
    EXPECT_FALSE(set.add(EmoteEffectKind::Wide));

    EXPECT_EQ(set.geometry(), EmoteEffectKind::RotateLeft);
    EXPECT_EQ(set.size(), 1u);
}

TEST(EmoteEffectSet, GeometryDoesNotBlockOtherEffects)
{
    EmoteEffectSet set;

    EXPECT_TRUE(set.add(EmoteEffectKind::Wide));
    EXPECT_TRUE(set.add(EmoteEffectKind::Shake));
    EXPECT_TRUE(set.add(EmoteEffectKind::Rainbow));

    EXPECT_EQ(set.size(), 3u);
    EXPECT_EQ(set.geometry(), EmoteEffectKind::Wide);
}

TEST(EmoteEffectSet, ReportsNoGeometryWhenNoneApplied)
{
    EmoteEffectSet set;
    set.add(EmoteEffectKind::Shake);

    EXPECT_FALSE(set.geometry().has_value());
}

TEST(EmoteEffectRegistry, LooksUpByPosition)
{
    auto registry = testRegistry();

    EXPECT_TRUE(registry.lookup("w!", EmoteEffectPosition::Before).has_value());
    // The same word is not a modifier on the other side of the emote.
    EXPECT_FALSE(registry.lookup("w!", EmoteEffectPosition::After).has_value());
}

TEST(EmoteEffectRegistry, RespectsCaseSensitivity)
{
    EmoteEffectRegistry registry;
    registry.add(define("w!", EmoteEffectPosition::Before, EmoteEffectKind::Wide));
    registry.add(define("+spin", EmoteEffectPosition::Before,
                        EmoteEffectKind::Spin, EmoteEffectScope::ThirdPartyOnly,
                        false));

    EXPECT_TRUE(registry.lookup("w!", EmoteEffectPosition::Before).has_value());
    EXPECT_FALSE(registry.lookup("W!", EmoteEffectPosition::Before).has_value());

    // The case-insensitive entry matches however it is written.
    EXPECT_TRUE(registry.lookup("+spin", EmoteEffectPosition::Before).has_value());
    EXPECT_TRUE(registry.lookup("+SPIN", EmoteEffectPosition::Before).has_value());
}

TEST(EmoteEffectRegistry, UnknownWordIsNotAModifier)
{
    auto registry = testRegistry();
    EXPECT_FALSE(
        registry.lookup("hello", EmoteEffectPosition::Before).has_value());
}

TEST(EmoteEffectRegistry, ParsesTheDocumentedShape)
{
    auto json = QJsonDocument::fromJson(R"({"effects": [
        {"code": "w!", "provider": "bttv", "position": "before",
         "applies_to": "third_party", "case_sensitive": true, "effect": "wide"},
        {"code": "ffzSpin", "provider": "ffz", "position": "after",
         "applies_to": "any", "requires": "ffz_supporter", "effect": "spin"},
        {"code": "+spin", "provider": "plus", "position": "before",
         "applies_to": "third_party", "case_sensitive": false,
         "requires": "plus_subscriber", "effect": "spin"}
    ]})")
                    .object();

    auto registry = EmoteEffectRegistry::fromJson(json);
    ASSERT_EQ(registry.size(), 3u);

    auto wide = registry.lookup("w!", EmoteEffectPosition::Before);
    ASSERT_TRUE(wide.has_value());
    EXPECT_EQ(wide->kind, EmoteEffectKind::Wide);
    EXPECT_EQ(wide->provider, EmoteEffectProvider::Bttv);
    EXPECT_EQ(wide->scope, EmoteEffectScope::ThirdPartyOnly);
    EXPECT_EQ(wide->requirement, EmoteEffectRequirement::None);

    auto spin = registry.lookup("ffzSpin", EmoteEffectPosition::After);
    ASSERT_TRUE(spin.has_value());
    EXPECT_EQ(spin->requirement, EmoteEffectRequirement::FfzSupporter);
    EXPECT_EQ(spin->scope, EmoteEffectScope::Any);

    auto plus = registry.lookup("+SPIN", EmoteEffectPosition::Before);
    ASSERT_TRUE(plus.has_value());
    EXPECT_EQ(plus->requirement, EmoteEffectRequirement::PlusSubscriber);
}

TEST(EmoteEffectRegistry, SkipsEntriesItCannotUnderstand)
{
    // A newer server may describe effects this build predates. One unusable
    // entry must not cost us the rest of the catalogue.
    auto json = QJsonDocument::fromJson(R"({"effects": [
        {"code": "good", "provider": "bttv", "position": "before",
         "applies_to": "any", "effect": "shake"},
        {"code": "futureEffect", "provider": "bttv", "position": "before",
         "applies_to": "any", "effect": "teleport"},
        {"code": "badProvider", "provider": "nobody", "position": "before",
         "applies_to": "any", "effect": "shake"},
        {"code": "", "provider": "bttv", "position": "before",
         "applies_to": "any", "effect": "shake"}
    ]})")
                    .object();

    auto registry = EmoteEffectRegistry::fromJson(json);

    EXPECT_EQ(registry.size(), 1u);
    EXPECT_TRUE(registry.lookup("good", EmoteEffectPosition::Before).has_value());
}

TEST(EmoteEffectRegistry, DefaultsToCaseSensitiveWhenUnstated)
{
    auto json = QJsonDocument::fromJson(R"({"effects": [
        {"code": "Strict", "provider": "bttv", "position": "before",
         "applies_to": "any", "effect": "shake"}
    ]})")
                    .object();

    auto registry = EmoteEffectRegistry::fromJson(json);

    EXPECT_TRUE(registry.lookup("Strict", EmoteEffectPosition::Before).has_value());
    EXPECT_FALSE(registry.lookup("strict", EmoteEffectPosition::Before).has_value());
}

TEST(EmoteEffectRegistry, HandlesAnEmptyCatalogue)
{
    auto registry = EmoteEffectRegistry::fromJson(QJsonObject{});
    EXPECT_TRUE(registry.empty());
}

TEST(ApplyEmoteEffects, LeavesAPlainMessageAlone)
{
    EXPECT_EQ(describe(run({word("hello"), word("world")})), "hello world");
}

TEST(ApplyEmoteEffects, AppliesAModifierWrittenBeforeAnEmote)
{
    EXPECT_EQ(describe(run({word("w!"), emote("Kappa")})), "Kappa[wide]");
}

TEST(ApplyEmoteEffects, AppliesAModifierWrittenAfterAnEmote)
{
    EXPECT_EQ(describe(run({emote("Kappa"), word("ffzSpin")})), "Kappa[spin]");
}

TEST(ApplyEmoteEffects, StacksSeveralModifiersOnOneEmote)
{
    EXPECT_EQ(describe(run({word("w!"), word("shake!"), emote("Kappa")})),
              "Kappa[wide,shake]");
}

TEST(ApplyEmoteEffects, KeepsOnlyTheFirstGeometryModifier)
{
    EXPECT_EQ(describe(run({word("l!"), word("r!"), emote("Kappa")})),
              "Kappa[rotate_left]");
}

TEST(ApplyEmoteEffects, CombinesModifiersFromBothSides)
{
    EXPECT_EQ(describe(run({word("w!"), emote("Kappa"), word("ffzSpin")})),
              "Kappa[wide,spin]");
}

TEST(ApplyEmoteEffects, RestoresAModifierWithNoEmoteAfterIt)
{
    // The word must stay readable rather than vanishing.
    EXPECT_EQ(describe(run({word("w!")})), "w!");
    EXPECT_EQ(describe(run({word("w!"), word("hello")})), "w! hello");
}

TEST(ApplyEmoteEffects, RestoresModifiersInTheOrderTheyWereWritten)
{
    EXPECT_EQ(describe(run({word("w!"), word("shake!"), word("hi")})),
              "w! shake! hi");
}

TEST(ApplyEmoteEffects, RestoresATrailingModifierWithNoEmoteBeforeIt)
{
    EXPECT_EQ(describe(run({word("hello"), word("ffzSpin")})), "hello ffzSpin");
}

TEST(ApplyEmoteEffects, DoesNotApplyAThirdPartyModifierToATwitchEmote)
{
    // w! covers third-party emotes only, so on a Twitch emote it stays a word.
    EXPECT_EQ(describe(run({word("w!"), emote("Kappa", false)})), "w! Kappa");
}

TEST(ApplyEmoteEffects, KeepsARestoredModifierAheadOfItsEmote)
{
    EXPECT_EQ(
        describe(run({word("w!"), word("anywhere!"), emote("Kappa", false)})),
        "w! Kappa[party]");
}

TEST(ApplyEmoteEffects, AppliesAnUnscopedModifierToATwitchEmote)
{
    EXPECT_EQ(describe(run({word("anywhere!"), emote("Kappa", false)})),
              "Kappa[party]");
}

TEST(ApplyEmoteEffects, IgnoresAModifierTheAuthorIsNotEntitledTo)
{
    EXPECT_EQ(describe(run({word("plusOnly"), emote("Kappa")})),
              "plusOnly Kappa");
}

TEST(ApplyEmoteEffects, AppliesAModifierTheAuthorIsEntitledTo)
{
    EmoteEffectEntitlements entitled{.plusSubscriber = true,
                                     .ffzSupporter = false};

    EXPECT_EQ(describe(run({word("plusOnly"), emote("Kappa")}, entitled)),
              "Kappa[rainbow]");
}

TEST(ApplyEmoteEffects, ModifiesOnlyTheAdjacentEmote)
{
    EXPECT_EQ(describe(run({word("w!"), emote("Kappa"), emote("PogChamp")})),
              "Kappa[wide] PogChamp");
}

TEST(ApplyEmoteEffects, HandlesSeveralDecoratedEmotesInOneMessage)
{
    EXPECT_EQ(describe(run({word("w!"), emote("Kappa"), word("shake!"),
                            emote("PogChamp")})),
              "Kappa[wide] PogChamp[shake]");
}

TEST(ApplyEmoteEffects, TreatsARepeatedModifierAsOne)
{
    EXPECT_EQ(describe(run({word("w!"), word("w!"), emote("Kappa")})),
              "Kappa[wide]");
}

TEST(ApplyEmoteEffects, BindsATrailingModifierToTheEmoteBeforeIt)
{
    // ffzSpin follows an emote, so it decorates that one rather than waiting
    // for the next.
    EXPECT_EQ(describe(run({emote("Kappa"), word("ffzSpin"), emote("PogChamp")})),
              "Kappa[spin] PogChamp");
}

TEST(ApplyEmoteEffects, PreservesSurroundingText)
{
    EXPECT_EQ(describe(run({word("look"), word("w!"), emote("Kappa"),
                            word("at"), word("this")})),
              "look Kappa[wide] at this");
}

TEST(ApplyEmoteEffects, HandlesAnEmptyMessage)
{
    EXPECT_TRUE(run({}).empty());
}

TEST(ApplyEmoteEffects, CarriesTheProviderFlagThrough)
{
    auto resolved = run({emote("Kappa", true), emote("Kappa2", false)});

    ASSERT_EQ(resolved.size(), 2u);
    EXPECT_TRUE(resolved[0].thirdParty);
    EXPECT_FALSE(resolved[1].thirdParty);
}

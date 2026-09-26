// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/emoteeffects/EmoteEffectParser.hpp"

#include "providers/emoteeffects/EmoteEffect.hpp"
#include "providers/emoteeffects/EmoteEffectGeometry.hpp"
#include "providers/emoteeffects/EmoteEffectRegistry.hpp"

#include <gtest/gtest.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointF>

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

TEST(EmoteEffectBuiltin, CarriesBetterTtvCodes)
{
    auto registry = EmoteEffectRegistry::builtin();

    auto wide = registry.lookup("w!", EmoteEffectPosition::Before);
    ASSERT_TRUE(wide.has_value());
    EXPECT_EQ(wide->kind, EmoteEffectKind::Wide);
    EXPECT_EQ(wide->scope, EmoteEffectScope::ThirdPartyOnly);
    EXPECT_EQ(wide->requirement, EmoteEffectRequirement::None);

    // The whole set, so a missing one is noticed.
    for (const auto *code : {"w!", "h!", "v!", "z!", "c!", "l!", "r!", "p!",
                             "s!"})
    {
        EXPECT_TRUE(registry.lookup(code, EmoteEffectPosition::Before)
                        .has_value())
            << "missing BetterTTV code " << code;
    }
}

TEST(EmoteEffectBuiltin, CarriesFrankerFaceZCodes)
{
    auto registry = EmoteEffectRegistry::builtin();

    auto grow = registry.lookup("ffzW", EmoteEffectPosition::After);
    ASSERT_TRUE(grow.has_value());
    EXPECT_EQ(grow->kind, EmoteEffectKind::GrowX);
    EXPECT_EQ(grow->scope, EmoteEffectScope::Any);

    // Most of FFZ's are reserved for supporters, but the plain transforms are
    // open to everyone.
    EXPECT_EQ(registry.lookup("ffzX", EmoteEffectPosition::After)->requirement,
              EmoteEffectRequirement::None);
    EXPECT_EQ(
        registry.lookup("ffzSpin", EmoteEffectPosition::After)->requirement,
        EmoteEffectRequirement::FfzSupporter);
}

TEST(EmoteEffectBuiltin, OmitsServiceSpecificCodes)
{
    // Codes belonging to a particular service are gated behind that service's
    // subscription, so they are not compiled in; they arrive only if its
    // address is configured.
    auto registry = EmoteEffectRegistry::builtin();

    EXPECT_FALSE(registry.lookup("+spin", EmoteEffectPosition::Before)
                     .has_value());
    EXPECT_FALSE(registry.lookup("+wide", EmoteEffectPosition::Before)
                     .has_value());
}

TEST(EmoteEffectBuiltin, DistinguishesCodesByPosition)
{
    auto registry = EmoteEffectRegistry::builtin();

    // BetterTTV writes its codes before the emote and FFZ after it, so the
    // same lookup on the wrong side must miss.
    EXPECT_TRUE(
        registry.lookup("w!", EmoteEffectPosition::Before).has_value());
    EXPECT_FALSE(registry.lookup("w!", EmoteEffectPosition::After).has_value());
    EXPECT_TRUE(
        registry.lookup("ffzX", EmoteEffectPosition::After).has_value());
    EXPECT_FALSE(
        registry.lookup("ffzX", EmoteEffectPosition::Before).has_value());
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

namespace {

EmoteEffectSet setOf(std::initializer_list<EmoteEffectKind> kinds)
{
    EmoteEffectSet set;
    for (auto kind : kinds)
    {
        set.add(kind);
    }
    return set;
}

/// A 28x28 emote in a line, which is the shape the maths has to get right.
const QSizeF BASE{28, 28};

}  // namespace

TEST(EmoteEffectGeometry, LeavesAnUndecoratedEmoteAlone)
{
    auto geometry = computeEmoteEffectGeometry(BASE, {});

    EXPECT_EQ(geometry.drawnSize, BASE);
    EXPECT_EQ(geometry.occupiedSize, BASE);
    EXPECT_EQ(geometry.rotation, 0);
    EXPECT_FALSE(geometry.flipHorizontally);
    EXPECT_FALSE(geometry.flipVertically);
}

TEST(EmoteEffectGeometry, WideStretchesWidthOnly)
{
    auto geometry =
        computeEmoteEffectGeometry(BASE, setOf({EmoteEffectKind::Wide}));

    EXPECT_EQ(geometry.drawnSize, QSizeF(112, 28));
    EXPECT_EQ(geometry.occupiedSize, QSizeF(112, 28));
}

TEST(EmoteEffectGeometry, GrowXIsHalfOfWide)
{
    auto geometry =
        computeEmoteEffectGeometry(BASE, setOf({EmoteEffectKind::GrowX}));

    EXPECT_EQ(geometry.drawnSize, QSizeF(56, 28));
}

TEST(EmoteEffectGeometry, FlippingDoesNotChangeSize)
{
    auto horizontal =
        computeEmoteEffectGeometry(BASE, setOf({EmoteEffectKind::FlipX}));
    EXPECT_EQ(horizontal.occupiedSize, BASE);
    EXPECT_TRUE(horizontal.flipHorizontally);
    EXPECT_FALSE(horizontal.flipVertically);

    auto vertical =
        computeEmoteEffectGeometry(BASE, setOf({EmoteEffectKind::FlipY}));
    EXPECT_EQ(vertical.occupiedSize, BASE);
    EXPECT_TRUE(vertical.flipVertically);
}

TEST(EmoteEffectGeometry, RotationSwapsTheSpaceTaken)
{
    // A non-square emote makes the swap observable.
    auto geometry = computeEmoteEffectGeometry(
        QSizeF(112, 28), setOf({EmoteEffectKind::RotateRight}));

    EXPECT_EQ(geometry.drawnSize, QSizeF(112, 28));
    EXPECT_EQ(geometry.occupiedSize, QSizeF(28, 112));
    EXPECT_EQ(geometry.rotation, 90);
}

TEST(EmoteEffectGeometry, RotatesTheOtherWayForRotateLeft)
{
    auto geometry =
        computeEmoteEffectGeometry(BASE, setOf({EmoteEffectKind::RotateLeft}));

    EXPECT_EQ(geometry.rotation, 270);
}

TEST(EmoteEffectGeometry, ReportsCollapsedSpacing)
{
    EXPECT_FALSE(computeEmoteEffectGeometry(BASE, {}).collapseLeadingSpace);
    EXPECT_TRUE(
        computeEmoteEffectGeometry(BASE, setOf({EmoteEffectKind::NoSpace}))
            .collapseLeadingSpace);
}

TEST(EmoteEffectGeometry, NonGeometryEffectsDoNotResize)
{
    auto geometry = computeEmoteEffectGeometry(
        BASE, setOf({EmoteEffectKind::Rainbow, EmoteEffectKind::Shake}));

    EXPECT_EQ(geometry.occupiedSize, BASE);
    EXPECT_EQ(geometry.rotation, 0);
}

TEST(EmoteEffectGeometry, StretchingCombinesWithNonGeometryEffects)
{
    auto geometry = computeEmoteEffectGeometry(
        BASE, setOf({EmoteEffectKind::Wide, EmoteEffectKind::Shake}));

    EXPECT_EQ(geometry.drawnSize, QSizeF(112, 28));
}

TEST(EmoteEffectGeometry, DrawnRectIsCentredInTheOccupiedSpace)
{
    auto geometry = computeEmoteEffectGeometry(
        QSizeF(112, 28), setOf({EmoteEffectKind::RotateRight}));

    // The line gave the emote a 28x112 slot; the emote is drawn 112x28 inside
    // it and then turned.
    QRectF target(10, 20, 28, 112);
    auto drawn = drawnRectFor(geometry, target);

    EXPECT_EQ(drawn.size(), QSizeF(112, 28));
    EXPECT_EQ(drawn.center(), target.center());
}

TEST(EmoteEffectTransform, IsIdentityWithoutTurnOrMirror)
{
    auto geometry = computeEmoteEffectGeometry(BASE, {});
    auto transform = emoteEffectTransform(geometry, QRectF(0, 0, 28, 28));

    EXPECT_TRUE(transform.isIdentity());
}

TEST(EmoteEffectTransform, MirroringKeepsTheEmoteInPlace)
{
    auto geometry =
        computeEmoteEffectGeometry(BASE, setOf({EmoteEffectKind::FlipX}));
    QRectF target(10, 20, 28, 28);
    auto transform = emoteEffectTransform(geometry, target);

    // A mirror about the centre swaps the edges rather than moving the emote
    // somewhere else entirely.
    EXPECT_EQ(transform.map(target.topLeft()), target.topRight());
    EXPECT_EQ(transform.map(target.center()), target.center());
}

TEST(EmoteEffectTransform, MirroringVerticallySwapsTopAndBottom)
{
    auto geometry =
        computeEmoteEffectGeometry(BASE, setOf({EmoteEffectKind::FlipY}));
    QRectF target(10, 20, 28, 28);
    auto transform = emoteEffectTransform(geometry, target);

    EXPECT_EQ(transform.map(target.topLeft()), target.bottomLeft());
}

TEST(EmoteEffectTransform, TurningKeepsTheCentreFixed)
{
    auto geometry = computeEmoteEffectGeometry(
        QSizeF(112, 28), setOf({EmoteEffectKind::RotateRight}));
    QRectF target(10, 20, 28, 112);
    auto transform = emoteEffectTransform(geometry, target);

    EXPECT_EQ(transform.map(target.center()), target.center());
}

TEST(EmoteEffectTransform, TurnedEmoteLandsInsideItsSlot)
{
    auto geometry = computeEmoteEffectGeometry(
        QSizeF(112, 28), setOf({EmoteEffectKind::RotateRight}));
    QRectF target(10, 20, 28, 112);

    auto drawn = drawnRectFor(geometry, target);
    auto mapped = emoteEffectTransform(geometry, target).mapRect(drawn);

    // Turning the drawn rectangle should reproduce the slot the line reserved.
    EXPECT_NEAR(mapped.x(), target.x(), 0.001);
    EXPECT_NEAR(mapped.y(), target.y(), 0.001);
    EXPECT_NEAR(mapped.width(), target.width(), 0.001);
    EXPECT_NEAR(mapped.height(), target.height(), 0.001);
}

TEST(ApplyEmoteEffects, CarriesTheProviderFlagThrough)
{
    auto resolved = run({emote("Kappa", true), emote("Kappa2", false)});

    ASSERT_EQ(resolved.size(), 2u);
    EXPECT_TRUE(resolved[0].thirdParty);
    EXPECT_FALSE(resolved[1].thirdParty);
}

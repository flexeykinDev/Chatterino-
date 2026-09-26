# Building this fork

This fork builds exactly like upstream Chatterino. Follow the guide for your
platform — [Windows](BUILDING_ON_WINDOWS.md),
[Linux](BUILDING_ON_LINUX.md), [macOS](BUILDING_ON_MAC.md) — and read this page
for what differs.

## Get the branch

```sh
git clone https://github.com/flexeykinDev/Chatterino-.git
cd Chatterino-
git checkout claude/inspiring-faraday-4ux26k
git submodule update --init --recursive
```

Forgetting the submodules is the most common cause of a confusing first build
failure.

## Qt

**Qt 6.7 or later.** The build needs Qt's Linguist tools (`lrelease`) to compile
the translation catalogues; they ship with the standard Qt kit, so there is no
extra component to tick in the online installer beyond what the platform guide
already lists.

Qt 6.4 does not work: its `moc` cannot parse this codebase when built against a
GCC 13 or newer standard library, and fails with a parse error inside
`<concepts>`. That is a Qt bug, not a problem with the source. Distribution
packages of Qt 6.4 — Ubuntu 24.04's, for instance — will hit it.

## Translations

The interface language is chosen in **Settings → General → Language**, and takes
effect on restart. English needs no catalogue, since it is the source language.

Two build targets manage the catalogues:

| Target | What it does |
| --- | --- |
| `update_translations` | Rescans the sources for `tr()` calls and updates `resources/translations/*.ts`. |
| `release_translations` | Compiles the `.ts` files to `.qm`. Runs as part of a normal build. |

After adding or changing a translatable string:

```sh
cmake --build build --target update_translations
# edit resources/translations/chatterino_ru.ts, then build as usual
```

### Adding a language

1. Copy an existing `.ts` file to `resources/translations/chatterino_<code>.ts`.
2. Add it to `CHATTERINO_TS_FILES` in the top-level `CMakeLists.txt`.
3. Add the code and its native name to `Localization::languages()` in
   `src/singletons/Localization.cpp`.

### A trap worth knowing about

`tr()` resolves its context from the class's own meta-object. A class that
translates strings **must declare `Q_OBJECT`** — without it `tr()` silently
resolves against a base class, finds nothing and returns the English string,
while the catalogue itself looks perfectly healthy. `lupdate` warns about this
("lacks Q_OBJECT macro"); the warning is worth believing.

`tests/src/Localization.cpp` asserts that each translated class reports its own
name, which is what catches this.

## Building headless (CI, containers)

The test binary needs a display. Without one, CTest's discovery step aborts and
the build reports failure even though both binaries linked:

```sh
export QT_QPA_PLATFORM=offscreen
./build/bin/chatterino-test
```

Some tests reach the network (websocket test servers, outbound HTTP) and fail in
a sandbox that blocks it. They are not indicative of a broken build.

## Server components

The features that are shared state between users — shadow chat, the
cross-channel ban registry, presence, typing indicators and the paint library —
need the companion service in [`server/`](server/README.md). The client works
without it; those features are simply unavailable. See that README for running
it and for the API it exposes.

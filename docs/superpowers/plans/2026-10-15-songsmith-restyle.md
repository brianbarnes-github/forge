# Songsmith Restyle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restyle Songsmith in the mock-up's visual language (charcoal surfaces, copper accent, rounded glowing notes, glossy keyboard, glowing copper playhead with ruler cap, outlined Play button, anvil logo) without changing layout or behaviour.

**Architecture:** A retuned `constexpr` palette (`SongsmithColours.h`), an app-wide `SongsmithLookAndFeel` (stock JUCE widgets), and stateless `ThemePaint` helpers (panel gradient, note body, logo) called by the custom-painted components.

**Tech Stack:** C++20, JUCE (`juce_gui_basics`), Catch2, CMake + Ninja.

**Spec:** `docs/superpowers/specs/2026-10-15-songsmith-restyle-design.md`

## Global Constraints

- Restyle only: no layout, menu, shortcut, interaction or `Source/Core/` change.
- `SongsmithColours.h` stays header-only and `juce_core`-only.
- No new dependencies, font files, or build-time image assets; exe `ICON_BIG` deferred.
- Track `colorArgb` saved in songs is never migrated or rewritten.
- New UI `.cpp` files are added to **both** `CMakeLists.txt` (`forge_ui` `target_sources`) and `Tests/CMakeLists.txt` (`forge_tests`, `${CMAKE_SOURCE_DIR}/Source/UI/...`).
- No test is deleted; existing pixel tests change only to use tokens/names instead of literals, or a tolerance where a body is now a gradient. Any other failure is a regression to investigate.
- Every commit: `cmake --build build && ctest --test-dir build --output-on-failure` green. Conventional-commit messages ending with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Never launch `song-smith` from a subagent.
- Palette values (verbatim from spec): `background #17191C`, `panelHeader #24272B`, `surfaceTop #2C2F34`, `surfaceBottom #1C1E22`, `edgeHighlight 0x0FFFFFFF`, `border #0E0F11`, `text #DADCE0`, `textMuted #8B8F96`, `accentCopper #E8955A`, `previewNoteNormal/conductorMeter #5CC3DE`, `previewNoteBorder #8FD8EC`, `selectedRow #2E3238`, `markerIvory #E6DFD2`.

## Review Focus

1. **Extreme zoom-out notes** (1–2 px wide, < 6 px tall): must still draw a visible body, no glow, no negative-radius artefacts. → Task 2 test "tiny note".
2. **Partial repaint covering only a note's glow** (viewport scroll exposing a strip beside a note): the note must not be culled, or the glow is cut. → Task 4 test "glow-only clip".
3. **Playhead at x = 0 and at the right edge**: glow clipped cleanly, dirty strips cover the whole glow so nothing trails. → Task 5 tests.
4. **Old song with legacy track colours**: loads and paints with its stored `colorArgb`, untouched. → Task 4 test "stored colour wins".
5. **Selected note at small height**: the selection edge stays visible when there's no glow. → Task 2 test "selected tiny note".

---

## File Structure

| File | Responsibility |
|---|---|
| `Source/UI/SongsmithColours.h` (modify) | Palette tokens (retuned + new) |
| `Source/UI/Theme/PianoRollPalette.h` (create) | Piano-roll row-band and keyboard colours (moved out of `PianoRollComponent.cpp`) |
| `Source/UI/Theme/ThemePaint.{h,cpp}` (create) | `paintPanelGradient`, `paintNoteBody`, `anvilPath`, `paintLogo`, `renderAppIcon` |
| `Source/UI/Theme/SongsmithLookAndFeel.{h,cpp}` (create) | App-wide LookAndFeel |
| `Tests/ThemePaint_tests.cpp`, `Tests/SongsmithLookAndFeel_tests.cpp` (create) | New tests |
| `Source/UI/UiMain.cpp` | Install/uninstall the LookAndFeel |
| `Source/UI/PianoRollComponent.{h,cpp}` | Notes via `paintNoteBody`, row bands, keyboard, `onPlayheadMoved` |
| `Source/UI/TrackNotePreview.cpp` | Row notes via `paintNoteBody` |
| `Source/UI/Playback/PlayheadOverlay.{h,cpp}` | Glow, `paintHalfWidth`, ivory marker/loop |
| `Source/UI/Playback/TimelineRuler.{h,cpp}` | Ivory marker/loop, playhead cap |
| `Source/UI/Playback/TransportStrip.{h,cpp}` | Gradient, icon colours, `PlayButton` |
| `Source/UI/Playback/TransportControls.{h,cpp}` | Remove `KnobLook` |
| `Source/UI/Dock/DockPanel.cpp`, `Source/UI/AboutBox.cpp`, `Source/UI/MainWindow.cpp` | Header gradient, logo, window icon |

---

### Task 1: Palette retune, `accentCopper` rename, `PianoRollPalette.h`

**Files:**
- Modify: `Source/UI/SongsmithColours.h`
- Create: `Source/UI/Theme/PianoRollPalette.h`
- Modify: `Source/UI/PianoRollComponent.cpp:15-35` (remove anon-namespace colours, include the new header)
- Modify: every `accentAmber` call site (`grep -rn accentAmber Source Tests`), `Source/UI/SongsmithMainComponent.cpp:111`
- Test: `Tests/SongsmithColours_tests.cpp` (create, or append if it exists — check `ls Tests | grep -i colour`)

**Interfaces:**
- Produces: `SongsmithColours::{accentCopper, surfaceTop, surfaceBottom, edgeHighlight, markerIvory, sourceHeaderLabel}`; `PianoRollPalette::{rowBandLight, rowBandDark, activeRowTint, whiteKey, whiteKeyShade, blackKeyTop, blackKeyBottom, blackKeyHighlight, whiteKeyDivider, whiteKeyLabel, whiteKeyHover, blackKeyHover, hoverLabel}` (all `constexpr juce::uint32`). `accentAmber` no longer exists.

- [ ] **Step 1: Write the failing test** — `Tests/SongsmithColours_tests.cpp`:

```cpp
#include "UI/SongsmithColours.h"
#include "UI/Theme/PianoRollPalette.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

TEST_CASE ("SongsmithColours: the restyle palette", "[theme]")
{
    CHECK (SongsmithColours::background    == 0xFF17191Cu);
    CHECK (SongsmithColours::panelHeader   == 0xFF24272Bu);
    CHECK (SongsmithColours::surfaceTop    == 0xFF2C2F34u);
    CHECK (SongsmithColours::surfaceBottom == 0xFF1C1E22u);
    CHECK (SongsmithColours::edgeHighlight == 0x0FFFFFFFu);
    CHECK (SongsmithColours::border        == 0xFF0E0F11u);
    CHECK (SongsmithColours::text          == 0xFFDADCE0u);
    CHECK (SongsmithColours::textMuted     == 0xFF8B8F96u);
    CHECK (SongsmithColours::accentCopper  == 0xFFE8955Au);
    CHECK (SongsmithColours::conductorTempo == SongsmithColours::accentCopper);
    CHECK (SongsmithColours::previewNoteNormal == 0xFF5CC3DEu);
    CHECK (SongsmithColours::conductorMeter == SongsmithColours::previewNoteNormal);
    CHECK (SongsmithColours::previewNoteBorder == 0xFF8FD8ECu);
    CHECK (SongsmithColours::selectedRow   == 0xFF2E3238u);
    CHECK (SongsmithColours::markerIvory   == 0xFFE6DFD2u);
    CHECK (SongsmithColours::sourceHeaderLabel == SongsmithColours::previewNoteNormal);
}

TEST_CASE ("SongsmithColours: the playhead copper differs from the marker ivory", "[theme]")
{
    CHECK (SongsmithColours::accentCopper != SongsmithColours::markerIvory);
}

TEST_CASE ("PianoRollPalette: keys keep light/dark contrast", "[theme]")
{
    const auto brightness = [] (juce::uint32 argb)
    {
        return (((argb >> 16) & 0xFF) + ((argb >> 8) & 0xFF) + (argb & 0xFF)) / 3;
    };
    CHECK (brightness (PianoRollPalette::whiteKey) > 200);
    CHECK (brightness (PianoRollPalette::whiteKeyShade) < brightness (PianoRollPalette::whiteKey));
    CHECK (brightness (PianoRollPalette::blackKeyTop) < 50);
    CHECK (brightness (PianoRollPalette::blackKeyBottom) > brightness (PianoRollPalette::blackKeyTop));
}
```

Add `SongsmithColours_tests.cpp` to `Tests/CMakeLists.txt`'s `add_executable(forge_tests ...)` list.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build 2>&1 | tail -5`
Expected: compile error — `accentCopper` / `PianoRollPalette.h` not found.

- [ ] **Step 3: Implement the palette.** In `SongsmithColours.h` replace the first block and the changed tokens:

```cpp
    constexpr juce::uint32 background    = 0xFF17191C;
    constexpr juce::uint32 panelHeader   = 0xFF24272B;
    // Header / transport-strip / button surfaces: a subtle top-to-bottom
    // gradient with a 1 px highlight on top (ThemePaint::paintPanelGradient).
    constexpr juce::uint32 surfaceTop    = 0xFF2C2F34;
    constexpr juce::uint32 surfaceBottom = 0xFF1C1E22;
    constexpr juce::uint32 edgeHighlight = 0x0FFFFFFF;
    constexpr juce::uint32 selectedRow   = 0xFF2E3238;
    constexpr juce::uint32 border        = 0xFF0E0F11;
    constexpr juce::uint32 text          = 0xFFDADCE0;
    constexpr juce::uint32 textMuted     = 0xFF8B8F96;
    constexpr juce::uint32 accentCopper  = 0xFFE8955A;

    // The start marker and loop range: ivory, so the copper playhead stays distinct.
    constexpr juce::uint32 markerIvory   = 0xFFE6DFD2;

    constexpr juce::uint32 trackDivider   = 0xFF2E3136;
    constexpr juce::uint32 columnDivider  = 0xFF4A4E55;
    constexpr juce::uint32 instrumentBand = 0xFF25282C;
```

Then: `conductorTempo = accentCopper;` `conductorMeter = 0xFF5CC3DE;` `previewNoteNormal = 0xFF5CC3DE;` `previewNoteBorder = 0xFF8FD8EC;` `rangeBandFill = 0x0F5CC3DE;` `rangeBandBorder = 0xFF3F8AA0;` and add `constexpr juce::uint32 sourceHeaderLabel = previewNoteNormal;`. Update comments that say "white playhead"/"amber" to "copper playhead"/"ivory marker".

Retune `familyBaseColour` (same hues, ~+10 % saturation/value):

```cpp
        0xFFF0E0B0, // Piano               — ivory
        0xFFF0A8CC, // ChromaticPercussion — pink
        0xFFCC9450, // Organ               — brown
        0xFFF8A860, // Guitar              — orange
        0xFFCC6448, // Bass                — rust
        0xFF84DC78, // Strings             — green
        0xFF5CCCC2, // Ensemble            — teal
        0xFFF8D840, // Brass               — gold
        0xFF6898F4, // Reed                — blue
        0xFF70E8F8, // Pipe                — cyan
        0xFFC484FF, // SynthLead           — violet
        0xFF948CDC, // SynthPad            — slate lavender
        0xFFEC74E4, // SynthEffects        — magenta
        0xFFB4DC50, // Ethnic              — olive
        0xFFF88484, // Percussive          — salmon
        0xFFA4A4A4, // SoundEffects        — grey
        0xFFF44C4C, // Drums               — red
```

Create `Source/UI/Theme/PianoRollPalette.h`:

```cpp
#pragma once

#include <juce_core/juce_core.h>

// The piano roll's own row-band and keyboard colours, in a header so the
// keyboard tests can name them. Not part of the general palette.
namespace lotro::PianoRollPalette
{
    constexpr juce::uint32 rowBandLight  = 0xFF1B1D21;
    constexpr juce::uint32 rowBandDark   = 0xFF17191C;
    // Drawn over the band for the active row (the semitone the last click landed on).
    constexpr juce::uint32 activeRowTint = 0x14E8955A;

    constexpr juce::uint32 whiteKey          = 0xFFECE8E0;
    constexpr juce::uint32 whiteKeyShade     = 0xFFD4D0C8;   // right edge of a white key
    constexpr juce::uint32 blackKeyTop       = 0xFF1A1B1E;
    constexpr juce::uint32 blackKeyBottom    = 0xFF2A2C30;
    constexpr juce::uint32 blackKeyHighlight = 0x26FFFFFF;   // glossy 1 px top edge
    constexpr juce::uint32 whiteKeyDivider   = 0xFF9A968E;
    constexpr juce::uint32 whiteKeyLabel     = 0xFF5A5650;
    constexpr juce::uint32 whiteKeyHover     = 0xFFD8D3C9;
    constexpr juce::uint32 blackKeyHover     = 0xFF34363B;
    constexpr juce::uint32 hoverLabel        = 0xFF8C8880;
}
```

In `PianoRollComponent.cpp` delete the anon-namespace colour constants (`rowBandLight` … `hoverLabel`, keep `blackKeyWidthRatio`, `middleC`, `fullMidiPitchRange`), add `#include "UI/Theme/PianoRollPalette.h"` and `using namespace PianoRollPalette;` inside that anon namespace's enclosing scope, replace `activeRowBand` usage with: fill the band (`rowBandDark`/`rowBandLight`), then if `pitch == activePitch` fill `activeRowTint` over it. Replace `blackKey` with `blackKeyTop` (Task 4 adds the gradient).

Rename `accentAmber` → `accentCopper` everywhere: `grep -rln accentAmber Source Tests | xargs sed -i 's/accentAmber/accentCopper/g'`. Replace `juce::Colour (0xFF7FA8D0)` at `SongsmithMainComponent.cpp:111` with `juce::Colour (SongsmithColours::sourceHeaderLabel)` and drop the "mockup's blue label" comment.

- [ ] **Step 4: Run tests**

Run: `cmake --build build && ctest --test-dir build --output-on-failure 2>&1 | tail -30`
Expected: new `[theme]` tests PASS. Fix any existing test that compares to an old literal **that means a theme colour** by switching it to the token (e.g. `0xFF2B2B2B` → `SongsmithColours::background`). Any other failure: investigate, don't loosen.

- [ ] **Step 5: Commit**

```bash
git add -A Source/UI Tests
git commit -m "feat(ui): restyle palette — charcoal surfaces, copper accent, ivory marker

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: `ThemePaint` helpers

**Files:**
- Create: `Source/UI/Theme/ThemePaint.h`, `Source/UI/Theme/ThemePaint.cpp`
- Create: `Tests/ThemePaint_tests.cpp`
- Modify: `CMakeLists.txt` (forge_ui sources), `Tests/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 tokens.
- Produces (namespace `lotro::ThemePaint`):
  - `constexpr int noteOverhang = 2;` `constexpr int glowMinHeight = 6;`
  - `struct NoteStyle { bool selected = false; std::optional<juce::Colour> border; };`
  - `void paintPanelGradient (juce::Graphics&, juce::Rectangle<int> bounds);`
  - `void paintNoteBody (juce::Graphics&, juce::Rectangle<int> rect, juce::Colour fill, const NoteStyle& style = {});`
  - `juce::Path anvilPath();` (unit space, x 0..1, y 0.10..0.70)
  - `void paintLogo (juce::Graphics&, juce::Rectangle<float> area, bool withWordmark);`
  - `juce::Image renderAppIcon (int size);`

- [ ] **Step 1: Write the failing tests** — `Tests/ThemePaint_tests.cpp`:

```cpp
#include "UI/Theme/ThemePaint.h"
#include "UI/SongsmithColours.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace
{
    juce::Image blank (int w, int h)
    {
        juce::Image image (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
        juce::Graphics (image).fillAll (juce::Colours::black);
        return image;
    }

    bool isNear (juce::Colour a, juce::Colour b, int tolerance)
    {
        return std::abs (a.getRed() - b.getRed()) <= tolerance
            && std::abs (a.getGreen() - b.getGreen()) <= tolerance
            && std::abs (a.getBlue() - b.getBlue()) <= tolerance;
    }

    const juce::Colour fill (0xFF8060A0);
}

TEST_CASE ("ThemePaint: a note body's centre is close to its fill", "[theme]")
{
    auto image = blank (60, 30);
    { juce::Graphics g (image); ThemePaint::paintNoteBody (g, { 10, 10, 40, 12 }, fill); }
    CHECK (isNear (image.getPixelAt (30, 16), fill, 16));
}

TEST_CASE ("ThemePaint: a tall note has rounded corners", "[theme]")
{
    auto image = blank (60, 30);
    { juce::Graphics g (image); ThemePaint::paintNoteBody (g, { 10, 10, 40, 12 }, fill); }
    // The corner pixel carries at most glow, so it is darker than the body.
    CHECK (image.getPixelAt (10, 10).getBrightness() < image.getPixelAt (30, 16).getBrightness());
}

TEST_CASE ("ThemePaint: a tall note glows, fading with distance", "[theme]")
{
    auto image = blank (60, 30);
    { juce::Graphics g (image); ThemePaint::paintNoteBody (g, { 10, 10, 40, 12 }, fill); }
    const auto near = image.getPixelAt (30, 9);   // 1 px above
    const auto far  = image.getPixelAt (30, 8);   // 2 px above
    const auto out  = image.getPixelAt (30, 6);   // beyond the overhang
    CHECK (near.getBrightness() > far.getBrightness());
    CHECK (far.getBrightness() > 0.0f);
    CHECK (out == juce::Colours::black);
}

TEST_CASE ("ThemePaint: a short note has no glow", "[theme]")
{
    auto image = blank (60, 30);
    { juce::Graphics g (image); ThemePaint::paintNoteBody (g, { 10, 10, 40, ThemePaint::glowMinHeight - 1 }, fill); }
    CHECK (image.getPixelAt (30, 9) == juce::Colours::black);
    CHECK (image.getPixelAt (30, 8) == juce::Colours::black);
}

TEST_CASE ("ThemePaint: a tiny note (1 px wide, 2 px tall) still draws a body", "[theme]")
{
    auto image = blank (20, 20);
    { juce::Graphics g (image); ThemePaint::paintNoteBody (g, { 5, 5, 1, 2 }, fill); }
    CHECK (image.getPixelAt (5, 5) != juce::Colours::black);
    CHECK (image.getPixelAt (5, 3) == juce::Colours::black);
}

TEST_CASE ("ThemePaint: a selected note has a white edge", "[theme]")
{
    auto image = blank (60, 30);
    { juce::Graphics g (image); ThemePaint::paintNoteBody (g, { 10, 10, 40, 12 }, fill, { true, std::nullopt }); }
    CHECK (image.getPixelAt (30, 11).getBrightness() > 0.9f);
}

TEST_CASE ("ThemePaint: a selected tiny note keeps a visible edge", "[theme]")
{
    auto image = blank (60, 30);
    { juce::Graphics g (image); ThemePaint::paintNoteBody (g, { 10, 10, 40, 4 }, fill, { true, std::nullopt }); }
    CHECK (image.getPixelAt (30, 10).getBrightness() > 0.9f);
}

TEST_CASE ("ThemePaint: a border override colours the edge", "[theme]")
{
    auto image = blank (60, 30);
    { juce::Graphics g (image); ThemePaint::paintNoteBody (g, { 10, 10, 40, 12 }, fill, { false, juce::Colours::red }); }
    const auto edge = image.getPixelAt (30, 10);
    CHECK (edge.getRed() > 200);
    CHECK (edge.getGreen() < 80);
}

TEST_CASE ("ThemePaint: the panel gradient is lighter at the top, with a highlight line", "[theme]")
{
    auto image = blank (40, 30);
    { juce::Graphics g (image); ThemePaint::paintPanelGradient (g, { 0, 0, 40, 30 }); }
    const auto highlight = image.getPixelAt (20, 0);
    const auto top = image.getPixelAt (20, 2);
    const auto bottom = image.getPixelAt (20, 27);
    CHECK (top.getBrightness() > bottom.getBrightness());
    CHECK (highlight.getBrightness() > top.getBrightness());
    CHECK (image.getPixelAt (20, 29) == juce::Colour (SongsmithColours::border));
}

TEST_CASE ("ThemePaint: the anvil path fits the unit square and is wider than tall", "[theme]")
{
    const auto bounds = ThemePaint::anvilPath().getBounds();
    CHECK (! bounds.isEmpty());
    CHECK (bounds.getX() >= 0.0f);
    CHECK (bounds.getY() >= 0.0f);
    CHECK (bounds.getRight() <= 1.0f);
    CHECK (bounds.getBottom() <= 1.0f);
    CHECK (bounds.getWidth() > bounds.getHeight());
}

TEST_CASE ("ThemePaint: the app icon has copper pixels on a transparent ground", "[theme]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto icon = ThemePaint::renderAppIcon (64);
    CHECK (icon.getWidth() == 64);
    CHECK (icon.getPixelAt (0, 0).getAlpha() == 0);
    CHECK (isNear (icon.getPixelAt (32, 30), juce::Colour (SongsmithColours::accentCopper), 40));
}
```

Add `ThemePaint_tests.cpp` and `${CMAKE_SOURCE_DIR}/Source/UI/Theme/ThemePaint.cpp` to `Tests/CMakeLists.txt`; add `Source/UI/Theme/ThemePaint.cpp` to `forge_ui`'s `target_sources`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build 2>&1 | tail -5`
Expected: `ThemePaint.h: No such file`.

- [ ] **Step 3: Implement.** `Source/UI/Theme/ThemePaint.h`:

```cpp
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

// Stateless painting helpers shared by Songsmith's custom-painted components,
// so notes, headers and the logo look the same everywhere.
namespace lotro::ThemePaint
{
    // How far a note's glow and selection edge reach outside its bounds. Cull
    // and dirty rects for notes must be inflated by this.
    constexpr int noteOverhang = 2;
    // Notes shorter than this get no glow (track-row mini notes, zoomed-out rolls).
    constexpr int glowMinHeight = 6;

    struct NoteStyle
    {
        bool selected = false;
        std::optional<juce::Colour> border;   // replaces the default fill.brighter (0.4f) edge
    };

    // Gradient surfaceTop -> surfaceBottom, 1 px edgeHighlight on top, 1 px border on the bottom.
    void paintPanelGradient (juce::Graphics& g, juce::Rectangle<int> bounds);

    // A rounded, gradient-filled note with an edge and (if tall enough) a soft glow.
    void paintNoteBody (juce::Graphics& g, juce::Rectangle<int> rect, juce::Colour fill, const NoteStyle& style = {});

    // The anvil mark in unit space (x 0..1, y 0.10..0.70).
    juce::Path anvilPath();

    // The anvil in copper, fitted into `area`; with the "Songsmith" / "MIDI EDITOR" wordmark to its right.
    void paintLogo (juce::Graphics& g, juce::Rectangle<float> area, bool withWordmark);

    // A square ARGB image of the anvil on a transparent ground, for window icons.
    juce::Image renderAppIcon (int size);
}
```

`Source/UI/Theme/ThemePaint.cpp`:

```cpp
#include "UI/Theme/ThemePaint.h"

#include "UI/SongsmithColours.h"

namespace lotro::ThemePaint
{

void paintPanelGradient (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    if (bounds.isEmpty())
        return;
    const auto b = bounds.toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colour (SongsmithColours::surfaceTop), 0.0f, b.getY(),
                                             juce::Colour (SongsmithColours::surfaceBottom), 0.0f, b.getBottom(), false));
    g.fillRect (bounds);
    g.setColour (juce::Colour (SongsmithColours::edgeHighlight));
    g.fillRect (bounds.getX(), bounds.getY(), bounds.getWidth(), 1);
    g.setColour (juce::Colour (SongsmithColours::border));
    g.fillRect (bounds.getX(), bounds.getBottom() - 1, bounds.getWidth(), 1);
}

void paintNoteBody (juce::Graphics& g, juce::Rectangle<int> rect, juce::Colour fill, const NoteStyle& style)
{
    if (rect.isEmpty())
        return;
    const auto r = rect.toFloat();
    const float radius = juce::jmin (3.0f, r.getHeight() / 3.0f, r.getWidth() / 2.0f);

    if (rect.getHeight() >= glowMinHeight)
    {
        // Outermost ring first, fainter; the inner ring adds on top, so alpha falls with distance.
        for (int ring = noteOverhang; ring >= 1; --ring)
        {
            g.setColour (fill.withAlpha (0.25f / (float) ring));
            g.fillRoundedRectangle (r.expanded ((float) ring), radius + (float) ring);
        }
    }

    g.setGradientFill (juce::ColourGradient (fill.brighter (0.15f), 0.0f, r.getY(),
                                             fill.darker (0.15f), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);

    if (rect.getWidth() >= 3 && rect.getHeight() >= 3)
    {
        g.setColour (style.border.value_or (fill.brighter (0.4f)));
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    }

    if (style.selected)
    {
        g.setColour (juce::Colours::white);
        const float thickness = juce::jmin (2.0f, r.getHeight() / 2.0f);
        g.drawRoundedRectangle (r.reduced (thickness / 2.0f), radius, thickness);
    }
}

juce::Path anvilPath()
{
    juce::Path p;
    p.startNewSubPath (0.0f, 0.18f);                 // horn tip
    p.quadraticTo (0.18f, 0.18f, 0.30f, 0.10f);      // horn's upper curve
    p.lineTo (1.0f, 0.10f);                          // face
    p.lineTo (1.0f, 0.30f);                          // heel
    p.lineTo (0.78f, 0.36f);
    p.quadraticTo (0.66f, 0.42f, 0.66f, 0.55f);      // waist, right
    p.lineTo (0.80f, 0.70f);                         // foot, right
    p.lineTo (0.30f, 0.70f);                         // foot, left
    p.lineTo (0.44f, 0.55f);
    p.quadraticTo (0.44f, 0.42f, 0.30f, 0.34f);      // waist, left
    p.quadraticTo (0.14f, 0.30f, 0.0f, 0.18f);       // horn's underside
    p.closeSubPath();
    return p;
}

void paintLogo (juce::Graphics& g, juce::Rectangle<float> area, bool withWordmark)
{
    const auto copper = juce::Colour (SongsmithColours::accentCopper);
    auto markArea = withWordmark ? area.removeFromLeft (area.getHeight() * 1.4f) : area;
    auto mark = anvilPath();
    mark.applyTransform (mark.getTransformToScaleToFit (markArea.reduced (2.0f), true));
    g.setColour (copper.withAlpha (0.35f));
    g.fillPath (mark);
    g.setColour (copper);
    g.strokePath (mark, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (! withWordmark)
        return;
    area.removeFromLeft (8.0f);
    auto title = area.removeFromTop (area.getHeight() * 0.62f);
    g.setColour (copper);
    g.setFont (juce::FontOptions (title.getHeight() * 0.8f));
    g.drawText ("Songsmith", title, juce::Justification::bottomLeft, false);
    g.setColour (juce::Colour (SongsmithColours::textMuted));
    juce::Font caption (juce::FontOptions (juce::jmax (8.0f, area.getHeight() * 0.6f)));
    caption.setExtraKerningFactor (0.25f);
    g.setFont (caption);
    g.drawText ("MIDI EDITOR", area, juce::Justification::topLeft, false);
}

juce::Image renderAppIcon (int size)
{
    juce::Image image (juce::Image::ARGB, size, size, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    const float inset = (float) size * 0.08f;
    auto mark = anvilPath();
    mark.applyTransform (mark.getTransformToScaleToFit (juce::Rectangle<float> (inset, inset, (float) size - 2 * inset,
                                                                                (float) size - 2 * inset), true));
    g.setColour (juce::Colour (SongsmithColours::accentCopper));
    g.fillPath (mark);
    return image;
}

}
```

- [ ] **Step 4: Run tests**

Run: `cmake --build build && ctest --test-dir build -R "ThemePaint" --output-on-failure`
Expected: all PASS. If the icon centre sample (32, 30) falls in the anvil's waist gap, sample the face instead (`(48, 16)` for size 64: face spans y 0.10..0.30 of the fitted box) — the assertion's intent is "copper on the mark".

- [ ] **Step 5: Commit**

```bash
git add Source/UI/Theme/ThemePaint.* Tests/ThemePaint_tests.cpp CMakeLists.txt Tests/CMakeLists.txt
git commit -m "feat(ui): ThemePaint — panel gradient, glowing note body, anvil logo

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: `SongsmithLookAndFeel`, installed app-wide

**Files:**
- Create: `Source/UI/Theme/SongsmithLookAndFeel.{h,cpp}`, `Tests/SongsmithLookAndFeel_tests.cpp`
- Modify: `Source/UI/UiMain.cpp`, `Source/UI/Playback/TransportControls.{h,cpp}` (delete `KnobLook`), `CMakeLists.txt`, `Tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `ThemePaint::paintPanelGradient`, Task 1 tokens.
- Produces: `class lotro::SongsmithLookAndFeel : public juce::LookAndFeel_V4` (default-constructible); `struct lotro::ScopedSongsmithLookAndFeel` (RAII: installs as default on construction, restores `nullptr` on destruction) declared in the same header.

- [ ] **Step 1: Write the failing tests** — `Tests/SongsmithLookAndFeel_tests.cpp`:

```cpp
#include "UI/Theme/SongsmithLookAndFeel.h"
#include "UI/SongsmithColours.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace
{
    juce::Image paintComponent (juce::Component& c)
    {
        juce::Image image (juce::Image::ARGB, c.getWidth(), c.getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g (image);
        c.paintEntireComponent (g, true);
        return image;
    }

    bool hasCopperPixel (const juce::Image& image)
    {
        const juce::Colour copper (SongsmithColours::accentCopper);
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
            {
                const auto p = image.getPixelAt (x, y);
                if (std::abs (p.getRed() - copper.getRed()) < 24 && std::abs (p.getGreen() - copper.getGreen()) < 24
                    && std::abs (p.getBlue() - copper.getBlue()) < 24)
                    return true;
            }
        return false;
    }
}

TEST_CASE ("SongsmithLookAndFeel: colour IDs come from the palette", "[theme]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongsmithLookAndFeel lf;
    CHECK (lf.findColour (juce::ResizableWindow::backgroundColourId) == juce::Colour (SongsmithColours::background));
    CHECK (lf.findColour (juce::Label::textColourId) == juce::Colour (SongsmithColours::text));
    CHECK (lf.findColour (juce::PopupMenu::highlightedBackgroundColourId).getHue()
           == juce::Colour (SongsmithColours::accentCopper).getHue());
    CHECK (lf.findColour (juce::Slider::trackColourId) == juce::Colour (SongsmithColours::accentCopper));
    CHECK (lf.findColour (juce::TextEditor::focusedOutlineColourId) == juce::Colour (SongsmithColours::accentCopper));
}

TEST_CASE ("SongsmithLookAndFeel: stock widgets paint at several sizes", "[theme]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    ScopedSongsmithLookAndFeel installed;
    for (const auto size : { juce::Point<int> (8, 8), juce::Point<int> (24, 24), juce::Point<int> (160, 32) })
    {
        juce::TextButton button ("OK");
        juce::Slider linear (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
        juce::Slider rotary (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
        juce::ToggleButton toggle ("On");
        for (juce::Component* c : std::initializer_list<juce::Component*> { &button, &linear, &rotary, &toggle })
        {
            c->setSize (size.x, size.y);
            CHECK (paintComponent (*c).getWidth() == size.x);
        }
    }
}

TEST_CASE ("SongsmithLookAndFeel: a ticked toggle shows copper", "[theme]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    ScopedSongsmithLookAndFeel installed;
    juce::ToggleButton toggle ("On");
    toggle.setSize (120, 24);
    CHECK (! hasCopperPixel (paintComponent (toggle)));
    toggle.setToggleState (true, juce::dontSendNotification);
    CHECK (hasCopperPixel (paintComponent (toggle)));
}

TEST_CASE ("SongsmithLookAndFeel: a linear slider fills copper up to its thumb", "[theme]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    ScopedSongsmithLookAndFeel installed;
    juce::Slider slider (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    slider.setRange (0.0, 1.0);
    slider.setValue (0.5);
    slider.setSize (200, 24);
    const auto image = paintComponent (slider);
    CHECK (hasCopperPixel (image.getClippedImage ({ 0, 0, 90, 24 })));
    CHECK (! hasCopperPixel (image.getClippedImage ({ 120, 0, 80, 24 })));
}
```

Add the test file and `${CMAKE_SOURCE_DIR}/Source/UI/Theme/SongsmithLookAndFeel.cpp` to `Tests/CMakeLists.txt`; add `Source/UI/Theme/SongsmithLookAndFeel.cpp` to `forge_ui`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build 2>&1 | tail -5`
Expected: `SongsmithLookAndFeel.h: No such file`.

- [ ] **Step 3: Implement.** `SongsmithLookAndFeel.h`:

```cpp
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace lotro
{

// Songsmith's app-wide look: the palette mapped onto JUCE's colour IDs, plus
// drawing overrides for the stock widgets the app uses. Installed as the
// default LookAndFeel in UiMain; custom-painted components use ThemePaint.
class SongsmithLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SongsmithLookAndFeel();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override;
    void drawTickBox (juce::Graphics&, juce::Component&, float x, float y, float w, float h,
                      bool ticked, bool isEnabled, bool highlighted, bool down) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
                        int thumbStart, int thumbSize, bool mouseOver, bool mouseDown) override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawMenuBarBackground (juce::Graphics&, int width, int height, bool isMouseOverBar,
                                juce::MenuBarComponent&) override;
};

// Installs a SongsmithLookAndFeel as the default for its lifetime (tests).
struct ScopedSongsmithLookAndFeel
{
    ScopedSongsmithLookAndFeel()  { juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel); }
    ~ScopedSongsmithLookAndFeel() { juce::LookAndFeel::setDefaultLookAndFeel (nullptr); }
    SongsmithLookAndFeel lookAndFeel;
};

} // namespace lotro
```

`SongsmithLookAndFeel.cpp`:

```cpp
#include "UI/Theme/SongsmithLookAndFeel.h"

#include "UI/SongsmithColours.h"
#include "UI/Theme/ThemePaint.h"

namespace lotro
{

namespace
{
    juce::Colour c (juce::uint32 argb) { return juce::Colour (argb); }

    juce::LookAndFeel_V4::ColourScheme songsmithScheme()
    {
        using namespace SongsmithColours;
        // windowBackground, widgetBackground, menuBackground, outline, defaultText,
        // defaultFill, highlightedText, highlightedFill, menuText
        return { c (background), c (panelHeader), c (panelHeader), c (border), c (text),
                 c (surfaceTop), c (background), c (accentCopper), c (text) };
    }
}

SongsmithLookAndFeel::SongsmithLookAndFeel() : juce::LookAndFeel_V4 (songsmithScheme())
{
    using namespace SongsmithColours;
    const auto copper = c (accentCopper);

    setColour (juce::ResizableWindow::backgroundColourId, c (background));
    setColour (juce::DocumentWindow::textColourId, c (textMuted));
    setColour (juce::Label::textColourId, c (text));
    setColour (juce::TextEditor::backgroundColourId, c (surfaceBottom));
    setColour (juce::TextEditor::textColourId, c (text));
    setColour (juce::TextEditor::outlineColourId, c (border));
    setColour (juce::TextEditor::focusedOutlineColourId, copper);
    setColour (juce::TextEditor::highlightColourId, copper.withAlpha (0.35f));
    setColour (juce::CaretComponent::caretColourId, copper);
    setColour (juce::ComboBox::backgroundColourId, c (surfaceBottom));
    setColour (juce::ComboBox::textColourId, c (text));
    setColour (juce::ComboBox::outlineColourId, c (border));
    setColour (juce::ComboBox::arrowColourId, c (textMuted));
    setColour (juce::ComboBox::focusedOutlineColourId, copper);
    setColour (juce::PopupMenu::backgroundColourId, c (panelHeader));
    setColour (juce::PopupMenu::textColourId, c (text));
    setColour (juce::PopupMenu::headerTextColourId, c (textMuted));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, copper.withAlpha (0.85f));
    setColour (juce::PopupMenu::highlightedTextColourId, c (background));
    setColour (juce::ListBox::backgroundColourId, c (background));
    setColour (juce::ListBox::textColourId, c (text));
    setColour (juce::TreeView::backgroundColourId, c (background));
    setColour (juce::TreeView::selectedItemBackgroundColourId, c (selectedRow));
    setColour (juce::ScrollBar::thumbColourId, c (textMuted));
    setColour (juce::Slider::trackColourId, copper);
    setColour (juce::Slider::backgroundColourId, c (surfaceBottom));
    setColour (juce::Slider::thumbColourId, c (text));
    setColour (juce::Slider::rotarySliderFillColourId, copper);
    setColour (juce::Slider::rotarySliderOutlineColourId, c (columnDivider));
    setColour (juce::Slider::textBoxTextColourId, c (text));
    setColour (juce::Slider::textBoxOutlineColourId, c (border));
    setColour (juce::ToggleButton::textColourId, c (text));
    setColour (juce::ToggleButton::tickColourId, c (background));
    setColour (juce::ToggleButton::tickDisabledColourId, c (textMuted));
    setColour (juce::TextButton::buttonColourId, c (surfaceTop));
    setColour (juce::TextButton::buttonOnColourId, copper);
    setColour (juce::TextButton::textColourOffId, c (text));
    setColour (juce::TextButton::textColourOnId, c (background));
    setColour (juce::AlertWindow::backgroundColourId, c (panelHeader));
    setColour (juce::AlertWindow::textColourId, c (text));
    setColour (juce::AlertWindow::outlineColourId, c (border));
}

void SongsmithLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                                 bool highlighted, bool down)
{
    using namespace SongsmithColours;
    const auto r = button.getLocalBounds().toFloat().reduced (0.5f);
    const float radius = juce::jmin (4.0f, r.getHeight() / 2.0f);
    auto top = c (surfaceTop), bottom = c (surfaceBottom);
    if (highlighted) { top = top.brighter (0.08f); bottom = bottom.brighter (0.08f); }
    if (down)        { top = top.darker (0.15f);   bottom = bottom.darker (0.15f); }
    g.setGradientFill (juce::ColourGradient (top, 0.0f, r.getY(), bottom, 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    if (button.getToggleState())
    {
        g.setColour (c (accentCopper).withAlpha (0.18f));
        g.fillRoundedRectangle (r, radius);
    }
    g.setColour (c (edgeHighlight));
    g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + radius, r.getRight() - radius);
    g.setColour (button.getToggleState() ? c (accentCopper) : c (border));
    g.drawRoundedRectangle (r, radius, 1.0f);
}

void SongsmithLookAndFeel::drawTickBox (juce::Graphics& g, juce::Component& component, float x, float y, float w, float h,
                                        bool ticked, bool isEnabled, bool highlighted, bool)
{
    using namespace SongsmithColours;
    const juce::Rectangle<float> box (x + 0.5f, y + 0.5f, w - 1.0f, h - 1.0f);
    g.setColour (ticked ? c (accentCopper) : c (surfaceBottom).brighter (highlighted ? 0.1f : 0.0f));
    g.fillRoundedRectangle (box, 3.0f);
    g.setColour (ticked ? c (accentCopper).brighter (0.2f) : c (columnDivider));
    g.drawRoundedRectangle (box, 3.0f, 1.0f);
    if (ticked)
    {
        auto tick = getTickShape (0.75f);
        g.setColour (component.findColour (isEnabled ? juce::ToggleButton::tickColourId
                                                     : juce::ToggleButton::tickDisabledColourId));
        g.fillPath (tick, tick.getTransformToScaleToFit (box.reduced (box.getWidth() * 0.22f), false));
    }
}

void SongsmithLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                             float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style,
                                             juce::Slider& slider)
{
    if (! slider.isBar() && (style == juce::Slider::LinearHorizontal || style == juce::Slider::LinearVertical))
    {
        using namespace SongsmithColours;
        const bool horizontal = style == juce::Slider::LinearHorizontal;
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
        const float trackThickness = 4.0f;
        const auto track = horizontal
            ? juce::Rectangle<float> (bounds.getX(), bounds.getCentreY() - trackThickness / 2, bounds.getWidth(), trackThickness)
            : juce::Rectangle<float> (bounds.getCentreX() - trackThickness / 2, bounds.getY(), trackThickness, bounds.getHeight());
        g.setColour (c (border));
        g.fillRoundedRectangle (track, trackThickness / 2);

        const auto filled = horizontal ? track.withRight (sliderPos) : track.withTop (sliderPos);
        g.setColour (slider.findColour (juce::Slider::trackColourId));
        g.fillRoundedRectangle (filled, trackThickness / 2);

        const float thumb = juce::jmin (14.0f, (horizontal ? bounds.getHeight() : bounds.getWidth()) - 2.0f);
        const auto centre = horizontal ? juce::Point<float> (sliderPos, bounds.getCentreY())
                                       : juce::Point<float> (bounds.getCentreX(), sliderPos);
        const auto knob = juce::Rectangle<float> (thumb, thumb).withCentre (centre);
        g.setGradientFill (juce::ColourGradient (c (text), knob.getX(), knob.getY(),
                                                 c (textMuted).darker (0.3f), knob.getRight(), knob.getBottom(), false));
        g.fillEllipse (knob);
        g.setColour (c (accentCopper));
        g.drawEllipse (knob.reduced (0.75f), 1.5f);
        juce::ignoreUnused (minSliderPos, maxSliderPos);
        return;
    }
    juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
}

// No 10 px inset (LookAndFeel_V4's would leave almost nothing of a 24 px knob).
void SongsmithLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                             float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    const auto radius = std::min (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const auto angle = startAngle + sliderPos * (endAngle - startAngle);
    const auto arcRadius = radius - 1.5f;
    if (arcRadius <= 0.0f)
        return;

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (slider.findColour (juce::Slider::rotarySliderOutlineColourId));
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path fill;
    fill.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);
    g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId));
    g.strokePath (fill, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path pointer;
    pointer.addLineSegment (juce::Line<float> (0.0f, -2.0f, 0.0f, -(arcRadius - 3.0f)), 0.0f);
    g.setColour (slider.findColour (juce::Slider::thumbColourId));
    g.strokePath (pointer, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                  juce::AffineTransform::rotation (angle).translated (centre));
}

void SongsmithLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar& bar, int x, int y, int width, int height,
                                          bool vertical, int thumbStart, int thumbSize, bool mouseOver, bool mouseDown)
{
    const auto thumb = vertical ? juce::Rectangle<int> (x, thumbStart, width, thumbSize)
                                : juce::Rectangle<int> (thumbStart, y, thumbSize, height);
    const auto alpha = mouseDown ? 0.8f : mouseOver ? 0.6f : 0.35f;
    g.setColour (bar.findColour (juce::ScrollBar::thumbColourId).withAlpha (alpha));
    const auto r = thumb.toFloat().reduced (vertical ? 3.0f : 1.0f, vertical ? 1.0f : 3.0f);
    g.fillRoundedRectangle (r, juce::jmin (r.getWidth(), r.getHeight()) / 2.0f);
}

void SongsmithLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
    g.setColour (juce::Colour (SongsmithColours::border));
    g.drawRect (0, 0, width, height, 1);
}

void SongsmithLookAndFeel::drawMenuBarBackground (juce::Graphics& g, int width, int height, bool, juce::MenuBarComponent&)
{
    ThemePaint::paintPanelGradient (g, { 0, 0, width, height });
}

} // namespace lotro
```

`juce::Button` has no "default button" flag, so only toggled-on buttons get the copper outline.

`UiMain.cpp`: add `#include "Theme/SongsmithLookAndFeel.h"`; a member `std::unique_ptr<SongsmithLookAndFeel> lookAndFeel;` declared **before** `window`; in `initialise` first line:

```cpp
        lookAndFeel = std::make_unique<SongsmithLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());
```

and `shutdown()`:

```cpp
    void shutdown() override
    {
        window.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        lookAndFeel.reset();
    }
```

`TransportControls`: delete `struct KnobLook`, `knobLook`, `KnobLook::drawRotarySlider`, `rateSlider.setLookAndFeel (&knobLook)` and `rateSlider.setLookAndFeel (nullptr)`. Keep the three `rateSlider.setColour` lines. The knob's draw now comes from the default LookAndFeel; in tests (no default installed) it falls back to V4's rotary — `TransportControls_tests` check geometry/values, not knob pixels.

- [ ] **Step 4: Run tests**

Run: `cmake --build build && ctest --test-dir build --output-on-failure 2>&1 | tail -20`
Expected: all PASS.

- [ ] **Step 5: Commit**

```bash
git add Source/UI/Theme/SongsmithLookAndFeel.* Tests/SongsmithLookAndFeel_tests.cpp Source/UI/UiMain.cpp Source/UI/Playback/TransportControls.* CMakeLists.txt Tests/CMakeLists.txt
git commit -m "feat(ui): app-wide SongsmithLookAndFeel; rate knob uses it

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Notes, row bands, keyboard and panel headers

**Files:**
- Modify: `Source/UI/PianoRollComponent.cpp` (`drawNotes` ~854-930, row bands ~686-700, `paintGutter` ~729-800)
- Modify: `Source/UI/TrackNotePreview.cpp` (note fill loop — find with `grep -n "colourArgb\|fillRect" Source/UI/TrackNotePreview.cpp`)
- Modify: `Source/UI/Dock/DockPanel.cpp:88-95`, `Source/UI/SongsmithMainComponent.cpp` (`sourceHeader` background), `Source/UI/TrackRowComponent.cpp` (instrument band fill, if it fills `instrumentBand`)
- Test: `Tests/PianoRollComponent_tests.cpp`, `Tests/TrackNotePreview_tests.cpp`, `Tests/DockPanel_tests.cpp` (if present; else add to an existing dock test file)

**Interfaces:**
- Consumes: `ThemePaint::{paintNoteBody, NoteStyle, noteOverhang, paintPanelGradient}`, `PianoRollPalette::*`.

- [ ] **Step 1: Write failing tests.** Append to `Tests/PianoRollComponent_tests.cpp` (it already has `Access` = `PianoRollComponentTestAccess`, `SourceRollFixture`, `paintKeyboard`, `keyTopY`; add `#include "UI/Theme/ThemePaint.h"` at the top):

```cpp
namespace
{
    // Paints the roll's canvas under `clip` into a canvas-sized image (black ground).
    juce::Image paintCanvasClipped (PianoRollComponent& roll, juce::Rectangle<int> clip)
    {
        auto& canvas = Access::canvas (roll);
        juce::Image image (juce::Image::ARGB, canvas.getWidth(), canvas.getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g (image);
        g.fillAll (juce::Colours::black);
        g.reduceClipRegion (clip);
        Access::paintCanvas (roll, g, clip);
        return image;
    }
}

TEST_CASE ("PianoRollComponent: a partial repaint covering only a note's glow still draws the glow", "[piano-roll][theme]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60 });
    fixture.track.setProperty (SongIDs::colorArgb, (int) 0xFFE0E0E0, nullptr);   // bright, opaque
    const auto bounds = Access::geometry (fixture.roll).noteBounds (fixture.source->getNote (0));
    REQUIRE (bounds.height >= ThemePaint::glowMinHeight);

    // Clip: the overhang rows directly above the note, running 20 px past its right end.
    const juce::Rectangle<int> glowStrip (bounds.x, bounds.y - ThemePaint::noteOverhang,
                                          bounds.width + 20, ThemePaint::noteOverhang);
    const auto image = paintCanvasClipped (fixture.roll, glowStrip);
    const auto glow = image.getPixelAt (bounds.x + bounds.width / 2, bounds.y - 1);
    const auto band = image.getPixelAt (bounds.x + bounds.width + 10, bounds.y - 1);   // same row, no note
    CHECK (glow.getBrightness() > band.getBrightness());
}

TEST_CASE ("PianoRollComponent: notes paint in their track's stored colour", "[piano-roll][theme]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60 });
    constexpr juce::uint32 legacy = 0xFF7FA8D0;   // an old-palette colour as saved in an existing song
    fixture.track.setProperty (SongIDs::colorArgb, (int) legacy, nullptr);
    const auto bounds = Access::geometry (fixture.roll).noteBounds (fixture.source->getNote (0));
    const auto image = paintCanvasClipped (fixture.roll, Access::canvas (fixture.roll).getLocalBounds());
    const auto centre = image.getPixelAt (bounds.x + bounds.width / 2, bounds.y + bounds.height / 2);
    const juce::Colour stored (legacy);
    CHECK (std::abs (centre.getRed() - stored.getRed()) <= 16);
    CHECK (std::abs (centre.getGreen() - stored.getGreen()) <= 16);
    CHECK (std::abs (centre.getBlue() - stored.getBlue()) <= 16);
}

TEST_CASE ("PianoRollComponent: white keys shade toward the right edge; black keys are glossy", "[piano-roll][keyboard][theme]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 64 });
    const auto image = paintKeyboard (fixture.roll);
    const int width = Access::gutter (fixture.roll).getWidth();
    const int rowHeight = Access::geometry (fixture.roll).getRowHeight();
    const int whiteY = keyTopY (fixture.roll, 62) + 2;
    CHECK (image.getPixelAt (2, whiteY).getBrightness() > image.getPixelAt (width - 3, whiteY).getBrightness());
    const int blackTop = keyTopY (fixture.roll, 61);
    CHECK (image.getPixelAt (3, blackTop + rowHeight - 2).getBrightness()
           > image.getPixelAt (3, blackTop + 2).getBrightness());
}
```

If `SourceTrackNoteSource` caches the track colour at construction (check `SourceTrackNoteSource.cpp`), set `colorArgb` before building the fixture's source instead: add an optional `juce::uint32 colour = 0xFF0000` parameter to `SourceRollFixture`'s constructor (default keeps every existing test unchanged). If the glow test's `band` pixel lands on a grid line (brighter), move it to `+ 7`.

`TrackNotePreview`: track-row notes are shorter than `glowMinHeight`, so they never glow; its change is the shared body (already pinned by `ThemePaint_tests`) plus the inflated cull. No new test there; its existing pixel tests must still pass.

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build && ./build/Tests/forge_tests "[theme]"`
Expected: glow test FAIL (note culled — `rect.intersects (clip)` excludes the strip), keyboard shade test FAIL (flat fills). Stored-colour test already passes with flat fills; it pins behaviour through the change.

- [ ] **Step 3: Implement.**

`drawNotes` — replace the body painting:

```cpp
        // Glow and selection edge reach noteOverhang px beyond the note.
        const auto paintedRect = rect.expanded (ThemePaint::noteOverhang);
        if (! (hasGhost ? paintedRect.getUnion (ghostRect) : paintedRect).intersects (clip))
            continue;

        const auto fill = juce::Colour (note.colourArgb);
        ThemePaint::NoteStyle style;
        if (role == Role::Preview)
            style.border = juce::Colour (note.state == NoteState::Normal ? SongsmithColours::previewNoteBorder
                                                                         : SongsmithColours::outOfRangeBorder);
        style.selected = role == Role::Source && sourceEditor != nullptr
            && sourceEditor->isSelected (SongDocument::getNotesNode (sourceEditor->getTrackNode()).getChild (i));
        ThemePaint::paintNoteBody (g, rect, fill, style);

        if (role != Role::Preview)
            continue;
```

(delete the old `fillRect` / `drawRect` / selection `drawRect` lines; keep the ghost and hatch code after it unchanged).

Row bands (`drawRowBands`, ~line 690):

```cpp
        g.setColour (juce::Colour (PianoRollGeometry::isBlackKey (pitch) ? rowBandDark : rowBandLight));
        g.fillRect (clip.getX(), row * rowHeight, clip.getWidth(), rowHeight);
        if (pitch == activePitch)
        {
            g.setColour (juce::Colour (activeRowTint));
            g.fillRect (clip.getX(), row * rowHeight, clip.getWidth(), rowHeight);
        }
```

`paintGutter`: replace the initial `fillRect` of `whiteKey` with a horizontal gradient

```cpp
    g.setGradientFill (juce::ColourGradient (juce::Colour (whiteKey), 0.0f, 0.0f,
                                             juce::Colour (whiteKeyShade), (float) gutterWidth, 0.0f, false));
    g.fillRect (0, 0, gutterWidth, gutterHeight);
```

and the black-key fill with

```cpp
                const juce::Rectangle<float> key (0.0f, (float) y, (float) blackKeyWidth, (float) rowHeight);
                if (hovered)
                    g.setColour (juce::Colour (blackKeyHover));
                else
                    g.setGradientFill (juce::ColourGradient (juce::Colour (blackKeyTop), 0.0f, key.getY(),
                                                             juce::Colour (blackKeyBottom), 0.0f, key.getBottom(), false));
                g.fillRect (key);
                g.setColour (juce::Colour (blackKeyHighlight));
                g.fillRect (0, y, blackKeyWidth - 1, 1);
                g.setColour (juce::Colours::black.withAlpha (0.35f));
                g.fillRect (blackKeyWidth, y, 2, rowHeight);   // shadow down the right side
```

Hover tints for white keys still fill `whiteKeyHover` (flat) — unchanged logic.

`TrackNotePreview.cpp`: where each note is filled (`g.fillRect` with the track colour), replace with `ThemePaint::paintNoteBody (g, noteRect, colour)` and inflate its cull check by `ThemePaint::noteOverhang` the same way.

`DockPanel::paint`: replace the header `setColour (panelHeader); fillRect (header);` with `ThemePaint::paintPanelGradient (g, header);`.

`SongsmithMainComponent.cpp:110`: `sourceHeader` is a `Label` — set its background to transparent and paint `paintPanelGradient` behind it from `SongsmithMainComponent::paint` (or, if the label fills a region the parent doesn't paint, keep `panelHeader` — a flat midpoint colour is acceptable for a label). Same for the `TrackRowComponent` instrument band: if it is filled with `instrumentBand`, leave it flat (it sits inside a row, not a header).

- [ ] **Step 4: Run all tests**

Run: `cmake --build build && ctest --test-dir build --output-on-failure 2>&1 | tail -30`
Expected: new tests PASS. Existing piano-roll / track-note-preview pixel tests that sampled a note's edge or exact fill: switch exact-fill checks at note centres to the ±16 tolerance helper; edge checks (e.g. `PianoRollComponent_tests.cpp:149-150, 390` border pixels) should still hold with `style.border` — if one samples the exact top-left corner, move the sample to the top edge's midpoint. Anything else failing is a regression.

- [ ] **Step 5: Commit**

```bash
git add Source/UI Tests
git commit -m "feat(ui): glowing rounded notes, glossy keyboard, gradient panel headers

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Copper playhead glow, ivory marker/loop, ruler cap

**Files:**
- Modify: `Source/UI/Playback/PlayheadOverlay.{h,cpp}`, `Source/UI/Playback/TimelineRuler.{h,cpp}`
- Modify: `Source/UI/PianoRollComponent.h` (`onPlayheadMoved` callback), `Source/UI/TrackEditorPanel.cpp` (~line 199-230), `Source/UI/TrackListComponent.{h,cpp}` (~line 143, `playbackPositionChanged`)
- Test: `Tests/PlayheadOverlay_tests.cpp`, `Tests/TimelineRulerMarks_tests.cpp` or the ruler's existing test file (`grep -ln "TimelineRuler ruler\|TimelineRuler (" Tests`)

**Interfaces:**
- Produces: `PlayheadDirtyTracker::paintHalfWidth` (`static constexpr int = 4`: 1 px core half + 3 px glow); `TimelineRuler::setPlayhead (std::function<std::optional<double>()>)`, `TimelineRuler::playheadMoved()`, `TimelineRuler::playheadCapBounds() const -> std::optional<juce::Rectangle<int>>`; `PianoRollComponent::onPlayheadMoved` (`std::function<void()>`).

- [ ] **Step 1: Write failing tests.** All in `Tests/PlayheadOverlay_tests.cpp` (it already includes the overlay, the ruler, `PlaybackTestSupport.h` with `RecordingSink`/`addTrack`/`addNote`, and has `containsStrip` near line 600):

```cpp
TEST_CASE ("PlayheadDirtyTracker: the repaint strip spans the glow", "[playback][overlay][theme]")
{
    const auto strip = PlayheadDirtyTracker::dirtyStrip (100, 50);
    CHECK (strip.getX() <= 100 - PlayheadDirtyTracker::paintHalfWidth);
    CHECK (strip.getRight() >= 100 + PlayheadDirtyTracker::lineWidth + PlayheadDirtyTracker::paintHalfWidth);
    CHECK (strip.getHeight() == 50);
}

TEST_CASE ("PlayheadDirtyTracker: a repaint covering the core but not the glow does not count as erased", "[playback][overlay][theme]")
{
    PlayheadDirtyTracker t;
    t.onPositionChanged (100, 500);
    t.onPaint (100, 500, 50, { 0, 0, 500, 50 });
    t.onPaint (200, 500, 50, { 99, 0, 4, 50 });   // the core of 100 only
    CHECK (containsStrip (t.onPositionChanged (200, 500), 100));
}

namespace
{
    juce::Image paintOverlayAt (double tick)
    {
        SongDocument doc;
        RecordingSink sink;
        PlaybackController controller (doc, sink);
        addNote (addTrack (doc), 60, 0, 9600);
        controller.flushRebuild();
        PlayheadOverlay overlay (controller, [] (double t) { return (int) (t * 0.1); });
        overlay.setSize (500, 100);
        controller.seekToTick (tick);
        juce::Image image (juce::Image::ARGB, 500, 100, true, juce::SoftwareImageType());
        juce::Graphics g (image);
        g.fillAll (juce::Colours::black);
        overlay.paint (g);
        return image;
    }

    bool nearColour (juce::Colour a, juce::Colour b, int tolerance)
    {
        return std::abs (a.getRed() - b.getRed()) <= tolerance && std::abs (a.getGreen() - b.getGreen()) <= tolerance
               && std::abs (a.getBlue() - b.getBlue()) <= tolerance;
    }
}

TEST_CASE ("PlayheadOverlay: a copper core with a glow that fades with distance", "[playback][overlay][theme]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto image = paintOverlayAt (1000.0);   // x = 100
    CHECK (nearColour (image.getPixelAt (100, 50), juce::Colour (SongsmithColours::accentCopper), 8));
    CHECK (image.getPixelAt (99, 50).getBrightness() > image.getPixelAt (97, 50).getBrightness());
    CHECK (image.getPixelAt (97, 50).getBrightness() > 0.0f);
    CHECK (image.getPixelAt (95, 50) == juce::Colours::black);
}

TEST_CASE ("PlayheadOverlay: at x = 0 the glow clips cleanly and its strip reaches past the edge", "[playback][overlay][theme]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto image = paintOverlayAt (0.0);
    CHECK (nearColour (image.getPixelAt (0, 50), juce::Colour (SongsmithColours::accentCopper), 8));
    CHECK (PlayheadDirtyTracker::dirtyStrip (0, 100).getX() < 0);   // JUCE clips it; the left glow is covered
}

TEST_CASE ("TimelineRuler: the playhead cap is copper at the playhead x", "[playback][ruler][theme]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TimelineRuler ruler ([] (int x) { return (double) x * 10.0; });
    ruler.setSize (300, TimelineRuler::height);
    ruler.setMarks ([] (double tick) { return (int) (tick / 10.0); }, [] { return RulerGrid {}; });
    ruler.setPlayhead ([] { return std::optional<double> (1000.0); });   // x = 100
    const auto cap = ruler.playheadCapBounds();
    REQUIRE (cap.has_value());
    CHECK (cap->getCentreX() == 100);
    juce::Image image (juce::Image::ARGB, 300, TimelineRuler::height, true, juce::SoftwareImageType());
    { juce::Graphics g (image); ruler.paint (g); }
    CHECK (nearColour (image.getPixelAt (100, 2), juce::Colour (SongsmithColours::accentCopper), 24));
}

TEST_CASE ("TimelineRuler: no cap when the playhead is unset or off-screen", "[playback][ruler][theme]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TimelineRuler ruler ([] (int x) { return (double) x * 10.0; });
    ruler.setSize (300, TimelineRuler::height);
    ruler.setMarks ([] (double tick) { return (int) (tick / 10.0); }, [] { return RulerGrid {}; });
    CHECK (! ruler.playheadCapBounds().has_value());
    ruler.setPlayhead ([] { return std::optional<double> (99999.0); });   // x = 9999
    CHECK (! ruler.playheadCapBounds().has_value());
}

TEST_CASE ("TimelineRuler: playheadMoved repaints only when the cap moves", "[playback][ruler][theme]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TimelineRuler ruler ([] (int x) { return (double) x * 10.0; });
    ruler.setSize (300, TimelineRuler::height);
    ruler.setMarks ([] (double tick) { return (int) (tick / 10.0); }, [] { return RulerGrid {}; });
    double tick = 1000.0;
    ruler.setPlayhead ([&tick] { return std::optional<double> (tick); });
    tick = 1500.0;
    ruler.playheadMoved();
    CHECK (ruler.playheadCapBounds()->getCentreX() == 150);
}
```

If `RulerGrid {}` isn't valid, copy the grid lambda from the existing ruler paint test at line ~138 (`"the marker's line and triangle are drawn through both rows"`).

Existing tests to update (not loosen): after Task 1's rename, lines ~148, ~207 and ~579 read `SongsmithColours::accentCopper` for the marker/loop colour — change them to `SongsmithColours::markerIvory`, and rename their local `amber` variables to `ivory`. That is the failing test for the marker/loop colour change.

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build 2>&1 | tail -5`
Expected: compile errors — `paintHalfWidth`, `dirtyStrip`, `setPlayhead`, `playheadCapBounds` undefined.

- [ ] **Step 3: Implement.**

`PlayheadOverlay.h`, in `PlayheadDirtyTracker`:

```cpp
    static constexpr int lineWidth = 2;
    // The copper glow reaches this far either side of the line.
    static constexpr int paintHalfWidth = 4;

    // The strip to repaint for a line at x: the line plus its glow, with a 1 px margin.
    static juce::Rectangle<int> dirtyStrip (int x, int overlayHeight)
    {
        return { x - paintHalfWidth - 1, 0, lineWidth + 2 * paintHalfWidth + 2, overlayHeight };
    }
```

`PlayheadOverlay.cpp`:
- In `onPaint`, the wipe test becomes `clip.contains (dirtyStrip (d.x, overlayHeight).getIntersection ({ 0, 0, overlayWidth, overlayHeight }))`, and `line` used for the intersects/full checks stays the core `(x, 0, lineWidth, overlayHeight)`.
- `playbackPositionChanged`: `repaint (PlayheadDirtyTracker::dirtyStrip (dirtyX, getHeight()));` (JUCE clips negatives).
- `PlayheadOverlay::paint`:

```cpp
    const auto copper = juce::Colour (SongsmithColours::accentCopper);
    for (int d = PlayheadDirtyTracker::paintHalfWidth - 1; d >= 1; --d)
    {
        g.setColour (copper.withAlpha (0.30f * (1.0f - (float) d / (float) PlayheadDirtyTracker::paintHalfWidth)));
        g.fillRect (x - d, 0, 1, getHeight());
        g.fillRect (x + PlayheadDirtyTracker::lineWidth - 1 + d, 0, 1, getHeight());
    }
    g.setColour (copper);
    g.fillRect (x, 0, PlayheadDirtyTracker::lineWidth, getHeight());
```

- `MarkerOverlay::paint` and `LoopRangeOverlay::paint`: `accentCopper` → `markerIvory` (keep alphas). Update those classes' comments ("An amber…" → "An ivory…").

`TimelineRuler`:
- Header: add after `setLoopRange`:

```cpp
    // Where the playhead is, for the copper cap in the top row (none = don't draw).
    void setPlayhead (std::function<std::optional<double>()> playheadIn)
    {
        playhead = std::move (playheadIn);
        lastCap = playheadCapBounds();
        repaint();
    }
    // The owner calls this when the playhead moves; repaints only the old and new cap.
    void playheadMoved();
    // The cap's triangle (10 x 7 px at the top), in ruler coordinates; none when unset or off-screen.
    std::optional<juce::Rectangle<int>> playheadCapBounds() const;
```

  and members `std::function<std::optional<double>()> playhead; std::optional<juce::Rectangle<int>> lastCap;`.
- `.cpp`:

```cpp
std::optional<juce::Rectangle<int>> TimelineRuler::playheadCapBounds() const
{
    if (! playhead || ! xForTick)
        return std::nullopt;
    const auto tick = playhead();
    if (! tick.has_value())
        return std::nullopt;
    const int x = xForTick (*tick);
    if (x < contentLeft || x >= getWidth())
        return std::nullopt;
    constexpr int capWidth = 10, capHeight = 7;
    return juce::Rectangle<int> (x - capWidth / 2, 0, capWidth, capHeight);
}

void TimelineRuler::playheadMoved()
{
    const auto cap = playheadCapBounds();
    if (cap == lastCap)
        return;
    if (lastCap) repaint (lastCap->expanded (1));
    if (cap)     repaint (cap->expanded (1));
    lastCap = cap;
}
```

  In `paint`, after the marker flag (so the playhead cap is on top) and inside the saved clip:

```cpp
        if (const auto cap = playheadCapBounds())
        {
            juce::Path triangle;
            triangle.addTriangle ((float) cap->getX(), 0.0f, (float) cap->getRight(), 0.0f,
                                  (float) cap->getCentreX() + 0.5f, (float) cap->getBottom());
            g.setColour (juce::Colour (SongsmithColours::accentCopper));
            g.fillPath (triangle);
        }
```

  Also in `paint`: loop shading, brackets and the marker flag use `markerIvory` instead of `accentCopper` (keep alphas). Replace the ruler background `background.brighter (0.1f)` with `ThemePaint::paintPanelGradient (g, getLocalBounds())` and drop the final black line (the gradient's bottom border replaces it) — check existing ruler pixel tests still pass; if one samples the background, switch it to compare against a rendered `paintPanelGradient` reference.
- Wiring:
  - `TrackListComponent.cpp:143` area: `ruler->setPlayhead ([this] { return playback != nullptr ? std::optional<double> (playback->getPositionTicks()) : std::nullopt; });` and in `TrackListComponent.h` `playbackPositionChanged()` add `if (ruler != nullptr) ruler->playheadMoved();` as its first line.
  - `PianoRollComponent.h`: add `std::function<void()> onPlayheadMoved;` next to `onViewChanged`, and make `playbackPositionChanged` call `if (onPlayheadMoved) onPlayheadMoved();` after `followPlayhead (...)`.
  - `TrackEditorPanel.cpp` (next to `ruler.setMarker`): `ruler.setPlayhead ([&controller] { return std::optional<double> (controller.getPositionTicks()); });` and next to `rollIn.onViewChanged = ...`: `rollIn.onPlayheadMoved = [this] { ruler.playheadMoved(); };`; clear it where `roll.onViewChanged = nullptr;` is (line ~237): `roll.onPlayheadMoved = nullptr;`.
  - `followPlayhead` scrolls change the tick→x map; `onViewChanged` already repaints the whole ruler then, and `setPlayhead`'s `lastCap` is refreshed in `paint`? No — set `lastCap = playheadCapBounds();` at the end of `paint` so a full repaint resyncs it.

- [ ] **Step 4: Run all tests**

Run: `cmake --build build && ctest --test-dir build --output-on-failure 2>&1 | tail -30`
Expected: all PASS (existing marker/loop/playhead pixel tests updated to `markerIvory` / `accentCopper` tokens; the old "white playhead" asserts become copper).

- [ ] **Step 5: Commit**

```bash
git add Source/UI Tests
git commit -m "feat(ui): copper glowing playhead with ruler cap; ivory start marker and loop

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Transport strip, Play button, About logo, window icon

**Files:**
- Modify: `Source/UI/Playback/TransportStrip.{h,cpp}`, `Source/UI/AboutBox.cpp`, `Source/UI/MainWindow.cpp` (constructor)
- Test: `Tests/TransportStrip_tests.cpp`, `Tests/AboutBox_tests.cpp`

**Interfaces:**
- Consumes: `ThemePaint::{paintPanelGradient, paintLogo, renderAppIcon}`.
- Produces: `class lotro::PlayButton : public juce::DrawableButton` with `void setPlaying (bool)` and `bool isPlayingLook() const`, declared in `TransportStrip.h`; `TransportStrip::playButtonForTesting()` keeps returning `juce::Button&`.

- [ ] **Step 1: Write failing tests.** Append to `Tests/TransportStrip_tests.cpp` (add `#include "UI/SongsmithColours.h"`):

```cpp
namespace
{
    int copperPixels (const juce::Image& image)
    {
        const juce::Colour copper (SongsmithColours::accentCopper);
        int n = 0;
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
            {
                const auto p = image.getPixelAt (x, y);
                n += std::abs (p.getRed() - copper.getRed()) < 30 && std::abs (p.getGreen() - copper.getGreen()) < 30
                     && std::abs (p.getBlue() - copper.getBlue()) < 30;
            }
        return n;
    }

    juce::Image paintOnBlack (juce::Component& c, bool withChildren)
    {
        juce::Image image (juce::Image::ARGB, c.getWidth(), c.getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g (image);
        g.fillAll (juce::Colours::black);
        if (withChildren)
            c.paintEntireComponent (g, true);
        else
            c.paint (g);
        return image;
    }
}

TEST_CASE ("TransportStrip: Play has a copper outline and lights up while playing", "[playback][strip][theme]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    addNote (addTrack (doc), 60, 0, 4800);
    controller.flushRebuild();
    TransportStrip strip (controller, doc);
    strip.setSize (900, 40);

    auto& play = dynamic_cast<PlayButton&> (strip.playButtonForTesting());
    REQUIRE (play.getWidth() > 8);
    const auto stopped = paintOnBlack (play, true);
    CHECK (copperPixels (stopped) > 0);   // the outline
    CHECK (! play.isPlayingLook());

    click (play);   // starts playback; refreshPlayIcon sets the look
    CHECK (play.isPlayingLook());
    const auto playing = paintOnBlack (play, true);
    const juce::Point<int> inside (4, play.getHeight() / 2);   // inside the outline, left of the icon
    CHECK (playing.getPixelAt (inside.x, inside.y).getBrightness() > stopped.getPixelAt (inside.x, inside.y).getBrightness());
}

TEST_CASE ("TransportStrip: the strip background is the panel gradient", "[playback][strip][theme]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    TransportStrip strip (controller, doc);
    strip.setSize (900, 40);
    const auto image = paintOnBlack (strip, false);
    CHECK (image.getPixelAt (5, 3).getBrightness() > image.getPixelAt (5, 36).getBrightness());
}
```

`Tests/AboutBox_tests.cpp`:

```cpp
TEST_CASE ("AboutComponent: paints the copper anvil logo", "[about][theme]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const BuildInfo info { "0.1.0", 1, "abc1234", false };
    AboutComponent about (info);
    juce::Image image (juce::Image::ARGB, about.getWidth(), about.getHeight(), true, juce::SoftwareImageType());
    { juce::Graphics g (image); about.paint (g); }
    const juce::Colour copper (SongsmithColours::accentCopper);
    bool found = false;
    for (int y = 0; y < 80 && ! found; ++y)
        for (int x = 0; x < image.getWidth() && ! found; ++x)
        {
            const auto p = image.getPixelAt (x, y);
            found = std::abs (p.getRed() - copper.getRed()) < 24 && std::abs (p.getGreen() - copper.getGreen()) < 24
                    && std::abs (p.getBlue() - copper.getBlue()) < 24;
        }
    CHECK (found);
}
```

(`AboutComponent (const BuildInfo&)` is declared in `AboutBox.h`; add `#include "UI/SongsmithColours.h"`.)

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build 2>&1 | tail -5`
Expected: `PlayButton` undeclared.

- [ ] **Step 3: Implement.**

`TransportStrip.h`, before `class TransportStrip`:

```cpp
// The Play/Pause button: a rounded copper outline; while playing, a faint
// copper fill and a soft glow (the mock-up's lit Play button).
class PlayButton : public juce::DrawableButton
{
public:
    PlayButton() : juce::DrawableButton ("play", juce::DrawableButton::ImageFitted) {}

    void setPlaying (bool shouldLookPlaying)
    {
        if (playing == shouldLookPlaying)
            return;
        playing = shouldLookPlaying;
        repaint();
    }
    bool isPlayingLook() const noexcept { return playing; }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;

private:
    bool playing = false;
};
```

and change the member `playButton { "play", ImageFitted }` to `PlayButton playButton;` (split it out of the `juce::DrawableButton` declarator list).

`TransportStrip.cpp`:

```cpp
void PlayButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto copper = juce::Colour (SongsmithColours::accentCopper);
    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    constexpr float radius = 6.0f;
    if (playing)
    {
        for (int ring = 2; ring >= 1; --ring)
        {
            g.setColour (copper.withAlpha (0.12f / (float) ring));
            g.fillRoundedRectangle (r.expanded ((float) ring), radius + (float) ring);
        }
        g.setColour (copper.withAlpha (0.15f));
        g.fillRoundedRectangle (r, radius);
    }
    g.setColour (highlighted ? copper.brighter (0.2f) : copper);
    g.drawRoundedRectangle (r, radius, 1.5f);
    juce::DrawableButton::paintButton (g, highlighted, down);   // the icon
}
```

`DrawableButton::paintButton` draws its background colour (`backgroundColourId`, transparent by default) before the image; make sure it is transparent (`setColour (juce::DrawableButton::backgroundColourId, juce::Colours::transparentBlack)` for `playButton`) so the fill shows.

`setIcon`: `over` → `accentCopper`, `down` → `accentCopper.darker (0.2f)`:

```cpp
        const auto normal = make (SongsmithColours::text);
        const auto over   = make (SongsmithColours::accentCopper);
        const auto down   = make (juce::Colour (SongsmithColours::accentCopper).darker (0.2f).getARGB());
```

`refreshPlayIcon()`: add `playButton.setPlaying (controller.isPlaying());`.

`TransportStrip::paint`: `ThemePaint::paintPanelGradient (g, getLocalBounds());`.

The loop `for (auto* b : { &startButton, &rewindButton, &playButton, ... })` needs `juce::DrawableButton*` — `&playButton` converts implicitly since `PlayButton` derives from it; if the initializer list deduction fails, write `std::initializer_list<juce::DrawableButton*> { ... }`. Same for `buttonsForTesting()`.

`AboutBox.cpp`: `paint`:

```cpp
    void AboutComponent::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colour (SongsmithColours::background));
        ThemePaint::paintLogo (g, logoArea.toFloat(), true);
    }
```

with `juce::Rectangle<int> logoArea;` a new member of `AboutComponent` set in `resized()` by `logoArea = area.removeFromTop (56); area.removeFromTop (8);` **before** `appName` is laid out; grow `dialogHeight` by 64. If `appName` is the "SongSmith" title label, hide it (`appName.setVisible (false)`) — the wordmark replaces it — but keep it constructed so `AboutBox_tests`' label-text checks still find it; if a test asserts the visible title text, keep the label visible and drop the wordmark (`paintLogo (..., false)`).

`MainWindow.cpp` constructor, after `setVisible (true)` / content setup:

```cpp
    const auto icon = ThemePaint::renderAppIcon (256);
    setIcon (icon);
    if (auto* peer = getPeer())
        peer->setIcon (icon);
```

(Window icon: no automated test — needs a native peer; verified manually in Task 7.)

- [ ] **Step 4: Run all tests**

Run: `cmake --build build && ctest --test-dir build --output-on-failure 2>&1 | tail -30`
Expected: all PASS.

- [ ] **Step 5: Commit**

```bash
git add Source/UI Tests
git commit -m "feat(ui): copper Play button, gradient transport strip, anvil logo and window icon

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Docs, Windows verification, deploy

**Files:**
- Modify: `docs/ARCHITECTURE.md` (new §9.21 after §9.20, before `## 10.`), `docs/UI_GUIDE.md`, `docs/TESTING.md` (count line 5 + a theme bullet), `CLAUDE.md` (status line + docs-map row)

- [ ] **Step 1: Write `docs/ARCHITECTURE.md` §9.21** "Theme layer — `Source/UI/Theme/{ThemePaint,SongsmithLookAndFeel,PianoRollPalette}`, `SongsmithColours.h`": palette tokens and the "stored track colours are never migrated" rule; LookAndFeel install/uninstall order in `UiMain`; which widgets are overridden vs colour-ID-only; `ThemePaint` functions and `noteOverhang` (cull/dirty rects must inflate by it); playhead `paintHalfWidth`/`dirtyStrip`; ruler cap wiring (`setPlayhead`/`playheadMoved`, owners call it); copper playhead vs ivory marker/loop; exe icon deferred.

- [ ] **Step 2: `docs/UI_GUIDE.md`** — a short "Theming" note: colours come from `SongsmithColours.h`; never hard-code a colour in a component; notes go through `ThemePaint::paintNoteBody`.

- [ ] **Step 3: `docs/TESTING.md`** — update the count from `ctest --test-dir build -N | tail -1`; add a bullet: `ThemePaint_tests.cpp` / `SongsmithLookAndFeel_tests.cpp` / `SongsmithColours_tests.cpp` (`[theme]` tag) pin the palette, note body (glow falloff, no glow < 6 px, tiny notes, selection edge), panel gradient, anvil bounds, LookAndFeel colour IDs and copper toggles/sliders; overlay and ruler `[theme]` cases pin the glow dirty strip and the cap.

- [ ] **Step 4: `CLAUDE.md`** — append "and the restyle (charcoal/copper palette, `SongsmithLookAndFeel`, `ThemePaint` notes/headers/logo, copper playhead with ruler cap, ivory marker/loop)" to the status line; add a docs-map row: `docs/superpowers/specs/2026-10-15-songsmith-restyle-design.md`, `docs/superpowers/plans/2026-10-15-songsmith-restyle.md` | Design spec and plan for the restyle; code walkthrough: `docs/ARCHITECTURE.md` §9.21.

- [ ] **Step 5: Full verification**

Run: `cmake --build build && ctest --test-dir build --output-on-failure 2>&1 | tail -5`
Expected: 100 % pass.
Run: `./build-windows.sh forge_tests` then the Windows test run as described in `docs/BUILD.md`.
Expected: pass (real font rasteriser; any `[theme]` failure here is real).

- [ ] **Step 6: Commit docs**

```bash
git add docs CLAUDE.md
git commit -m "docs: Songsmith restyle — theme layer walkthrough, testing notes

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 7: Deploy for the user's visual check** (lead only, not a subagent):

```bash
./build-windows.sh forge_ui && cp build-windows/forge_ui_artefacts/Release/song-smith.exe /mnt/c/Apps/SongSmith/ \
  && mkdir -p /mnt/c/Apps/SongSmith/resources && cp resources/soundfonts/SongSmith.sf2 /mnt/c/Apps/SongSmith/resources/
```

Ask the user to check: notes/glow at several zooms, keyboard, playhead + cap while playing (no trails), ivory marker/loop, Play button lit while playing, menus/dialogs/sliders, About box, window/taskbar icon. Do not push without explicit approval.

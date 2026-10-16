# Songsmith restyle — design

Date: 2026-10-15
Status: approved in conversation, awaiting written-spec review

## Goal

Give Songsmith the visual language of the user's mock-up (dark charcoal
surfaces, copper accent, rounded glowing notes, a real-looking piano
keyboard, outlined Play button, glowing playhead, anvil logo) **without
changing the layout or behaviour**. Menus, dock panels, the track list,
the MIDI editor, the transport strip and every dialog stay where and what
they are; only how they paint changes.

### Decisions taken (with the user)

| Question | Decision |
|---|---|
| Scope | Restyle only — no nav rail, song card, section picker or new screens |
| Note colour | Keep per-track GM-family colours; restyle the note *shape* only |
| Surfaces | Subtle vertical gradients + 1 px bevels; no brushed-metal texture |
| Extras | Copper accent + Play outline; glowing playhead with ruler cap; anvil logo; restyled keyboard |
| Architecture | Central theme layer: palette + app-wide `LookAndFeel` + `ThemePaint` helpers |
| Exe / file icon | Deferred to a later change |

### Non-goals

- No layout, menu, shortcut, or interaction change.
- No new font files (JUCE default sans; weights/sizes only).
- No runtime-switchable themes / light mode (the palette stays `constexpr`;
  this does not preclude a theme struct later).
- No change to `Source/Core/` — this is UI-only.
- No build-time image assets (exe `ICON_BIG` deferred).

## 1. Palette — `Source/UI/SongsmithColours.h`

Stays header-only and `juce_core`-only (it is compiled into `SongModelBridge`
and `forge_tests`). Existing token names stay so call sites don't churn,
with one rename. Values sampled from the mock-up:

| Token | Old | New | Role |
|---|---|---|---|
| `background` | `#2B2B2B` | `#17191C` | canvas, panel body |
| `panelHeader` | `#383838` | `#24272B` | header/strip base (gradient midpoint) |
| `surfaceTop` *(new)* | — | `#2C2F34` | top of header/strip/button gradient |
| `surfaceBottom` *(new)* | — | `#1C1E22` | bottom of that gradient |
| `edgeHighlight` *(new)* | — | `0x0FFFFFFF` (white ~6 %) | 1 px top bevel |
| `border` | `#1A1A1A` | `#0E0F11` | 1 px bottom / outer shadow edge |
| `text` | `#D0D0D0` | `#DADCE0` | |
| `textMuted` | `#888888` | `#8B8F96` | |
| `accentAmber` → **`accentCopper`** | `#E0B080` | `#E8955A` | accent, Play outline, playhead, logo |
| `conductorTempo` | `#E0B080` | `= accentCopper` | keeps "tempo = accent" |
| `markerIvory` *(new)* | — | `#E6DFD2` | start marker + loop range (§4.2) |
| `previewNoteNormal`, `conductorMeter` | `#7FA8D0` | `#5CC3DE` | the mock-up's cyan |
| `previewNoteBorder` | `#A0C0E0` | `#8FD8EC` | cyan border, lighter |
| `rangeBandFill` / `rangeBandBorder` | cyan-blue | re-derived from the new cyan (same alphas) | |
| `selectedRow` | `#3D4A5A` | `#2E3238` (+ copper 1 px left edge where painted) | |
| `sourceHeaderLabel` *(new)* | literal `0xFF7FA8D0` in `SongsmithMainComponent.cpp:111` | `= previewNoteNormal` | removes the stray literal |
| `familyBaseColour[17]` | as now | same hues, ~+10 % saturation and value | read on the darker canvas |

`trackDivider`, `columnDivider`, `instrumentBand` are darkened in step with
`background` (keeping their current relative contrast). Out-of-range reds,
the green `eventMarker`, and the section/merge tokens keep their hues;
alphas are tweaked only where contrast on the new background requires it.

`accentAmber` is renamed everywhere it's used (~20 call sites). Today it
also colours the **start marker** (`MarkerOverlay` line + ruler flag), the
**loop range** (overlay fill, ruler brackets), part-slot highlights, the
ghost toggle and the strip-button "on" state — see §4.2 for how the copper
playhead stays distinguishable from the start marker.

**Track colours are stored per song.** `colorArgb` is set on each track node
at import (`SongModelBridge` via `trackColourFor`) and saved in the
`.songsmith` file. The retuned `familyBaseColour` therefore reaches newly
imported tracks only; existing songs keep their saved colours (and the
user's own picks). No migration — that would overwrite user choices.
`TrackRowComponent` (instrument segments) reads `familyBaseColour` live, so
in an old song a segment tint may differ slightly from the stored track
colour; accepted.

**Grid lines** (`GridLinePaint.h`) already draw white at per-level alpha,
which reads on any row shade; the scheme stays. The alpha table may be
retuned (lowered slightly) for the darker canvas — no new grid tokens.

The piano roll's file-local `rowBandLight/Dark`, `activeRowBand` and
keyboard colours (`PianoRollComponent.cpp` anon namespace) stay
file-local but get new values (§3).

## 2. `SongsmithLookAndFeel` — `Source/UI/Theme/SongsmithLookAndFeel.{h,cpp}`

A `juce::LookAndFeel_V4` subclass.

- **Construction.** Built with a `LookAndFeel_V4::ColourScheme` derived from
  the palette (window background, widget background, menu background,
  outline, default text, default fill, highlighted text/fill, menu text),
  so any widget not explicitly themed still lands in charcoal + copper.
  Then `setColour` for the colour IDs of the widgets the app uses:
  `Label`, `TextEditor`, `ComboBox`, `PopupMenu`, `ListBox`, `TreeView`,
  `ScrollBar`, `Slider`, `ToggleButton`, `TextButton`, `DocumentWindow` /
  `ResizableWindow`, `AlertWindow`, caret and text highlight.
- **Install.** `Main.cpp` owns one instance, created before the first window,
  installed via `juce::LookAndFeel::setDefaultLookAndFeel`, and cleared
  (`setDefaultLookAndFeel (nullptr)`) in `shutdown()` before it is
  destroyed.
- **Draw overrides** (only for widgets the app uses):
  - `drawButtonBackground` — 4 px rounded rect, `surfaceTop → surfaceBottom`
    gradient, 1 px `edgeHighlight` top line, 1 px `border` outline; hover
    lightens slightly, pressed darkens; toggled-on / default button gets a
    copper outline.
  - `drawToggleButton` / `drawTickBox` — rounded box, copper fill + dark
    tick when on.
  - `drawLinearSlider` — recessed dark track, copper fill up to the thumb,
    round metallic thumb (light-grey radial gradient) with a copper ring.
  - `drawRotarySlider` — no inset, dark arc track with copper value arc.
    **Absorbs `TransportControls::KnobLook`**, which is deleted along with
    the `rateSlider.setLookAndFeel` calls.
  - `drawScrollbar` — thin rounded thumb (`textMuted` at low alpha, brighter
    on hover), no track fill.
  - `drawPopupMenuBackground` — charcoal with a 1 px `border` outline.
  - `drawMenuBarBackground` — `paintPanelGradient`.
  - Popup-menu items, menu-bar items, combo boxes and document-window title
    bars are **not** overridden: `LookAndFeel_V4` already draws them from
    the colour IDs set above (copper highlight bar, muted shortcut text,
    rounded combo), and the app's dialogs use native title bars, so an
    override would never be seen.
- **Fonts.** No new font file. Weight/size tweaks only, where the mock-up
  reads lighter.

**Dock headers** (`DockPanel` / `DockStack`) are custom-painted and call
`ThemePaint::paintPanelGradient` directly rather than going through the
LookAndFeel.

## 3. `ThemePaint` — `Source/UI/Theme/ThemePaint.{h,cpp}`

Stateless free functions shared by the custom-painted components.

### 3.1 `paintPanelGradient (g, bounds)`

`surfaceTop → surfaceBottom` vertical gradient, 1 px `edgeHighlight` top
line, 1 px `border` bottom line. Used by: transport strip, dock headers,
panel headers (`sourceHeader` and friends), the instrument band.

### 3.2 `paintNoteBody (g, rect, fill, NoteStyle)`

- Corner radius `min (3, h / 3)` — degrades to square at small heights.
- Body: vertical gradient `fill.brighter (0.15) → fill.darker (0.15)`.
- Edge: 1 px `fill.brighter (0.4)`.
- Glow: `fill` at ~25 % alpha, 2 px outside the body — **skipped when the
  note is under 6 px tall** (track-row mini notes, zoomed-out rolls).
- `NoteStyle` fields: `selected` (adds the 2 px white selection edge on top),
  `borderOverride` (optional — the preview-role normal / out-of-range border
  colours used today).
- Ghost outlines and the dropped-note hatch stay in `drawNotes`; they
  simply call `paintNoteBody` for the body first.

**Overhang.** The glow and selection edge draw outside `noteBounds`. A
shared `ThemePaint::noteOverhang` constant (glow + selection edge, 2 px)
inflates the note's cull rect in `drawNotes` / `TrackNotePreview`, and any
note-sized dirty rect (today the canvas repaints wholesale, so this mainly
guards viewport partial repaints and future targeted repaints). Test: move a
note, repaint, and the old glow pixels are background again.

Callers: `PianoRollComponent::drawNotes` (source and preview roles) and
`TrackNotePreview` (track-row notes). Colours remain the per-track
`note.colourArgb`.

### 3.3 Grid rows (in `PianoRollComponent`)

Row bands: `background` / `background` lifted ~3 %. Active row: copper at
~8 % alpha over the band (replacing today's grey `activeRowBand`).

### 3.4 Keyboard (`PianoRollComponent::paintGutter`)

Structure and behaviour unchanged (hover, active-row naming, divider
logic). Paint only:

- White keys: ivory `#ECE8E0`, horizontal shade to `#D4D0C8` at the right
  edge.
- Black keys: vertical gradient `#1A1B1E → #2A2C30`, 1 px highlight on the
  top edge (glossy), 2 px shadow down the right side.
- Hover tints re-derived from the new key colours.
- The keyboard and row-band constants move from the `PianoRollComponent.cpp`
  anonymous namespace into `Source/UI/Theme/PianoRollPalette.h` so
  `PianoRollComponent_tests` can assert against names, not literals.
- **C rows are always labelled** ("C4") in muted text, as in the mock-up.
  Hover / active naming of other keys stays as today.

### 3.5 Anvil logo — `anvilPath()` / `paintLogo (g, area, withWordmark)`

A hand-built `juce::Path` (horn, face, waist, foot) in unit space; filled
and stroked in `accentCopper`. With `withWordmark`, "Songsmith" in copper
plus a small letter-spaced "MIDI EDITOR" in `textMuted` to its right/below.
Pure code — no image asset.

## 4. Playhead

### 4.1 `PlayheadOverlay`

A 2 px `accentCopper` core with a glow 3 px each side (copper ~30 % alpha
falling to 0). `PlayheadDirtyTracker` gains a `paintHalfWidth` so dirty
rects cover the glow (no trails).

### 4.2 Playhead vs start marker

The start marker (`MarkerOverlay`, the ruler flag) and the loop range are
amber today, distinct from the white playhead (see the `eventMarker` comment
in `SongsmithColours.h`). With a copper playhead they would merge.

**Decision (option a):** the playhead goes copper; the start marker
(`MarkerOverlay` line, ruler flag) and the loop range (overlay fill, ruler
brackets) move to a new `markerIvory` token (`#E6DFD2`), keeping their
current alphas. `conductorTempo` stays copper (it lives on the Conductor
row, not the timeline overlay, so it doesn't sit on the playhead).

### 4.3 Ruler cap (`TimelineRuler`)

A small copper downward triangle at the playhead x in the ruler. Like the
marker and loop range, the ruler is not a controller listener: it gets a
`setPlayhead (std::function<std::optional<double>()>)` provider, and its
owners (`TrackListComponent`, `TrackEditorPanel`'s roll) call
`ruler.playheadMoved()` from their existing `playbackPositionChanged`,
which repaints only the old and new cap rects. When the playhead is
off-screen the cap isn't drawn.

## 5. Transport strip and logo placement

- `TransportStrip` background → `paintPanelGradient`.
- Transport icons keep their shapes (`transportIconPath` unchanged); drawn
  in `text` at rest, `accentCopper` on hover/down (the `DrawableButton`
  normal/over/down images get the new colours).
- **Play button** becomes a small `PlayButton : juce::DrawableButton`
  subclass overriding `paintButton`: 6 px rounded rect with a 1.5 px copper
  outline; while playing, a 15 % copper fill plus a soft outer glow.
  `refreshPlayIcon()` (already called on every state change) also toggles
  the playing look. Start / Rewind / Stop / End stay frameless.
- `TransportControls` and `StripButtonsBar` pick up the LookAndFeel; no
  direct changes beyond removing `KnobLook`.
- **About box** (`AboutComponent::paint`): `paintLogo` with wordmark above
  the existing version/build info.
- **Window icon** (`MainWindow`): render `anvilPath()` into a 256 px
  `juce::Image` at startup and `setIcon` on the window (and its peer), so
  the title bar / taskbar show it while running. The exe/file icon
  (`ICON_BIG`) is deferred.

## 6. Testing

TDD throughout: each new test is written and seen failing before its code.

### 6.1 New tests

- `ThemePaint_tests`:
  - note body centre pixel within ±12 per channel of `fill`;
  - corner pixel differs from the body fill (rounded) when height ≥ 9 px —
    with glow, corners carry glow alpha rather than being transparent;
  - no glow pixels outside the body when height < 6 px; glow alpha
    decreases with distance when ≥ 6 px;
  - `selected` puts white on the edge;
  - panel gradient: top row lighter than bottom row, highlight line present;
  - `anvilPath()` non-empty, bounds within the unit square, wider than tall.
- `SongsmithLookAndFeel_tests`:
  - key colour IDs resolve to the palette (e.g.
    `ResizableWindow::backgroundColourId == background`,
    `PopupMenu::highlightedBackgroundColourId` copper-based);
  - `TextButton`, `Slider` (linear and rotary), `ToggleButton` paint
    without assertions at several sizes;
  - a toggled-on `ToggleButton` shows copper pixels.
- `PlayheadDirtyTracker`: dirty rects cover `x ± paintHalfWidth`.
- `TimelineRuler`: copper cap pixels at the playhead x; absent when the
  playhead is off-screen.
- Keyboard: a C row shows its label without hover.
- Play button: copper outline present; fill/glow only while playing.

Components that theme through `findColour` are tested with the
`SongsmithLookAndFeel` installed via a small RAII helper
(`ScopedSongsmithLookAndFeel`) in those test files; the test runner's global
default stays `LookAndFeel_V4`, so unrelated tests are unaffected.

### 6.2 Existing pixel tests (~150 `getPixelAt` asserts)

- Asserts comparing against literals that *mean* "the theme colour" switch
  to `SongsmithColours::…` tokens so future retunes don't break them again.
- Asserts on note interiors sample the centre with a tolerance helper
  (`isNear (pixel, expected, 12)`) since bodies are now gradients.
- No test is deleted, and none is loosened beyond the above. A failure that
  isn't explained by colour/shape is treated as a regression and
  investigated.

## 7. Rollout

Branch `feat/songsmith-restyle`, roughly six commits in dependency order,
each with a green `ctest`:

1. Palette retune + `accentCopper` rename + `sourceHeaderLabel` token.
2. `ThemePaint` (panel gradient, note body, logo path) with tests.
3. `SongsmithLookAndFeel` + install in `Main.cpp`; `KnobLook` removed.
4. Notes, grid rows and keyboard via `ThemePaint`; dock/panel headers.
5. Playhead glow + dirty-tracker half-width + ruler cap.
6. Transport strip, `PlayButton`, About-box logo, window icon.

Docs: new `docs/ARCHITECTURE.md` §9.21 (theme layer), a theming note in
`docs/UI_GUIDE.md`, `docs/TESTING.md` counts, and the `CLAUDE.md` status
line + docs-map row for this spec.

Verification: Linux `ctest`; Windows cross-build test run (real font
rasteriser for pixel tests); deploy to `C:\Apps\SongSmith` for the user's
visual check; `windows-2022` CI remains the release gate.

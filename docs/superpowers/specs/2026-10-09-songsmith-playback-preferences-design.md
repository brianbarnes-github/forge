# Songsmith Preferences ▸ Playback page + SongSmith.sf2 — design

Date: 2026-10-09. Status: draft for review (sub-project 2 of 3; follows
`2026-10-09-songsmith-preferences-hardening-design.md`).

Sub-projects: 1 hardening (done), **2 Playback page (this spec)**, 3 Appearance /
Editing pages (default grid size, wheel sensitivity, follow-playhead, show band,
restore window placement).

## Intent

File ▸ Preferences… gains a **Playback** page that shows which SoundFont is active
and lets the user Browse for another or Clear their choice. This closes the open
item "no UI to clear a SoundFont". The bundled default SoundFont is renamed to
`SongSmith.sf2`.

Success: the user can see the active SoundFont, pick a new one (live, a failed load
keeps the old one and says why), and Clear back to the bundled default (live); the
choice survives a restart; nothing about Song ▸ SoundFont… regresses.

Said by the user: Clear = **forget my chosen file and go back to the bundled
default** (option A); settings apply live; rename `TimGM6mb.sf2` to `SongSmith.sf2`
"to be used from now on". Chosen with the user: `PreferencesServices` struct as the
way a page reaches the app (sub-project 3 reuses it); Song ▸ SoundFont… stays.

## Scope

In: the rename; `AppSettings` SoundFont accessors; `PreferencesServices`; the Playback
page; `MainWindow` load/clear/label logic behind the services; Song ▸ SoundFont…
moved onto the same load path; docs.

Out: unloading a SoundFont (`SynthVoice` has no unload); choosing a SoundFont per Song
(stays app-wide); a SoundFont list/library; bank/preset selection; the Appearance /
Editing pages (sub-project 3). All UI-only, nothing in `Source/Core/`
(`forge-engine-ui-boundary`); no new dependencies.

## 1. Rename to `SongSmith.sf2`

The file is the GPL-2 TimGM6mb bank (md5 `1f1ad87ae6f87033d9a591eca567d919`),
git-ignored and never committed or shipped by CI. Renaming does not change the
licence; provenance is recorded in `docs/BUILD.md` and the CLAUDE.md licensing
guardrail ("`SongSmith.sf2` is TimGM6mb, GPL-2, …").

- Local file: `resources/soundfonts/TimGM6mb.sf2` → `resources/soundfonts/SongSmith.sf2`
  (`git mv` is not applicable, it is ignored; a plain `mv`).
- `CMakeLists.txt:189` `FORGE_SOUNDFONT` → `.../SongSmith.sf2`.
- `Source/UI/MainWindow.cpp` startup lookup: candidates are the configured path, then
  `<exe dir>/resources/SongSmith.sf2`. **No fallback to the old name.**
- `Tests/SynthVoice_tests.cpp:17` path → `resources/soundfonts/SongSmith.sf2`.
- Docs: `CLAUDE.md` (deploy command, licensing guardrail), `docs/BUILD.md:130`,
  `docs/UI_GUIDE.md:301`, `docs/TESTING.md:129`, `docs/ARCHITECTURE.md:759`. The
  2026-10-03 specs/plans are historical records and are left as written.
- Deploy: `/mnt/c/Apps/SongSmith/resources/SongSmith.sf2` is copied beside the exe; the
  old `TimGM6mb.sf2` there is left alone (a saved path may still point at it) and can
  be deleted by hand.

## 2. `AppSettings`

Typed accessors over the existing key `soundFontPath` (key and any saved value are
unchanged):

| Method | Behaviour |
|---|---|
| `juce::String soundFontPath() const` | `""` when never chosen or cleared |
| `bool setSoundFontPath (const juce::File&)` | stores the full path, returns the save result |
| `bool clearSoundFontPath()` | removes the key, returns the save result |

All writes go through the existing `save()` so `lastSaveFailed()` stays accurate.
`MainWindow` stops calling `settings->getValue/setValue ("soundFontPath")` directly.

## 3. `PreferencesServices`

`Source/UI/Preferences/PreferencesServices.h` (header-only):

```cpp
enum class SoundFontResult { loaded, failed, bundledUnavailable };
struct SoundFontLoad
{
    SoundFontResult result;
    juce::String    detail;   // failed: the error text; otherwise empty
};

struct PreferencesServices
{
    AppSettings& settings;
    std::function<SoundFontLoad (const juce::File&)> loadSoundFont;   // load, then remember
    std::function<SoundFontLoad ()>                  useBundledSoundFont;
    std::function<juce::String ()>                   activeSoundFontLabel;
};
```

`PreferencesPage::make` becomes
`std::function<std::unique_ptr<juce::Component> (PreferencesServices&, std::function<void()> onChanged)>`;
`showPreferencesDialog` and `PreferencesDialog` take `PreferencesServices&` instead of
`AppSettings&` (the dialog reads `services.settings` for the save-failure notice).
General and Import keep their `(AppSettings&, onChanged)` constructors; their registry
lambdas pass `services.settings`. Sub-project 3 adds callbacks to this struct.

Why a struct and not `AppSettings` broadcasting or giving the page the synth: the page
needs the load result synchronously (to show a failure and not write the setting), and
must not depend on playback types.

## 4. `MainWindow` behaviour behind the services

State: `juce::File activeSoundFont` — set on every successful load (startup, Browse,
Clear, Song ▸ SoundFont…), empty while none is loaded.

- `loadSoundFont (file)`: `synth.loadSoundFont (file)`; on success
  `appSettings.setSoundFontPath (file)`, `activeSoundFont = file`, return `loaded`;
  on `PlaybackError` / `std::exception` return `failed` with `what()` — previous
  SoundFont stays active (as `SynthVoice` already guarantees), nothing is written.
- `useBundledSoundFont()`: **always** `appSettings.clearSoundFontPath()` first (the
  user's intent is "forget my choice"), then if `bundledSoundFont()` does not exist
  return `bundledUnavailable` (the current SoundFont, if any, keeps playing until
  quit — there is no unload), else load it: `loaded` (`activeSoundFont = bundled`) or
  `failed` with the error text.
- `activeSoundFontLabel()`: `activeSoundFont` empty → "No SoundFont loaded";
  equals the bundled file → "Using bundled SongSmith.sf2"; else "Using " + full path.
- `loadStartupSoundFont()` uses the same candidate loop but through these helpers so
  `activeSoundFont` is set; behaviour unchanged apart from the new file name.
- `chooseSoundFont()` (Song ▸ SoundFont…) calls `loadSoundFont`; on `failed` it still
  shows the "SoundFont" message box as today.
- `bundledSoundFont()`: `currentExecutableFile.getSiblingFile ("resources").getChildFile ("SongSmith.sf2")`.

## 5. Playback page

`Source/UI/Preferences/PlaybackPreferencesPage.{h,cpp}`, third entry in
`preferencePages()` ("Playback" after "Import"; order General, Import, Playback).

Layout (12 px margin like the other pages): a muted heading "SoundFont", a label showing
`activeSoundFontLabel()`, a **Browse…** button and a **Clear** button on one row, and a
status line under them (muted; shows the last outcome).

- **Browse…** opens a `juce::FileChooser` ("Choose a SoundFont", `*.sf2`) launched
  async; the page owns the chooser (member `std::unique_ptr<juce::FileChooser>`) and
  guards its callback with a `SafePointer` to itself. A cancelled chooser does nothing.
  On a chosen file: `loadSoundFont`; `loaded` → refresh the label, status "Loaded";
  `failed` → status "Could not load: <detail>. Still using the previous SoundFont."
  and the label is unchanged.
- **Clear**: `useBundledSoundFont`; `loaded` → refresh label, status "Using the bundled
  SoundFont."; `bundledUnavailable` → status "Your choice was cleared, but the bundled
  SongSmith.sf2 was not found. The current SoundFont stays until you quit."; `failed` →
  "Your choice was cleared, but the bundled SoundFont could not be loaded: <detail>".
- After every action the page calls `onChanged()` so the dialog refreshes its
  save-failure notice (a failed save of `soundFontPath` shows the existing notice).
- The page uses `services` only in its constructor and click / chooser handlers, never
  in its destructor (the rule from the hardening spec).
- Clear is enabled only while `settings.soundFontPath()` is non-empty.

## Testing

TDD. Suite stays green (822 now); `docs/TESTING.md` count updated.

- `AppSettings`: `soundFontPath` default empty; set/get round trip and persistence over
  a re-opened file; `clearSoundFontPath` removes it; an unwritable settings file →
  both return false and set `lastSaveFailed()`.
- Playback page (constructed directly with fake services): label comes from
  `activeSoundFontLabel`; Browse is not unit-tested through a real `FileChooser`, so the
  page exposes `loadChosenFileForTesting (juce::File)` (the same code the chooser
  callback calls) — `loaded` refreshes the label and status, `failed` shows the error
  text and leaves the label; Clear calls `useBundledSoundFont` and shows the right
  status for each of the three results; Clear is disabled when no path is saved;
  `onChanged` fires after each action.
- Registry/dialog: the page list is General, Import, Playback; existing dialog tests
  adapt to `PreferencesServices` (a test fixture builds one with no-op callbacks).
- Not covered headless (by hand after deploy): real Browse dialog, live switching the
  sound mid-Play, Clear with and without `resources/SongSmith.sf2`, Song ▸ SoundFont…
  still works, startup with only `SongSmith.sf2`, startup with a saved path that no
  longer exists (falls back to bundled).

## Risks

- Switching SoundFont while a Song is playing: `SynthVoice` already builds the new
  instance off to the side and swaps it (retired instances freed by `SynthGc`); this
  spec adds no new threading. Checked by hand.
- Clear always erases the saved choice even if the bundled file then cannot load. That
  is the stated intent (forget my choice) and is surfaced in the status line.
- The rename leaves any existing `<exe>/resources/TimGM6mb.sf2` unused: a machine that
  relied on the old bundled name loses its default until the new file is copied. Only
  this machine (deployed by hand) is affected; the deploy step copies the new file.

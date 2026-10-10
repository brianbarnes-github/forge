# Songsmith Preferences hardening + loose ends — design

Date: 2026-10-09. Status: implemented (sub-project 1 of 3).

Sub-projects, each with its own spec, plan and build, in this order:
1. **Hardening** (this spec) — lifetime and robustness fixes, plus small loose ends.
2. Playback page — SoundFont path with Browse / Clear.
3. Appearance / Editing pages — default grid size, wheel sensitivity, follow-playhead,
   show band, restore window placement.

Decided with the user: order 1 → 2 → 3; new settings apply live except window
placement (applied at launch).

## Intent

Make the Preferences dialog and its neighbours safe to build more pages on. Success:
no path where the dialog or a page touches `AppSettings` / `PropertiesFile` after
`MainWindow` is gone; a failed settings write is visible, not silent; the dialog
cannot be shrunk unusable; a damaged `.songsmith` with bad SECTION nodes is rejected
at load; a long import does not leave a dismissed dialog on screen; the two known
compiler warnings and one misleading comment are gone.

Said by the user: "start the Preferences enhancement and fixes, lets tackle all you
know". Assumed (from the handoff list): the items below are exactly that list.

## Scope

All UI-only (`Source/UI/`, `Tests/`). Nothing in `Source/Core/` changes
(`forge-engine-ui-boundary`). No new dependencies.

### 1. Dialog lifetime

Today `showPreferencesDialog` fire-and-forgets a `DialogWindow` that holds an
`AppSettings&` (a `MainWindow` member over `MainWindow::settings`); page lambdas
capture the same reference. If `MainWindow` is destroyed while the dialog is open,
all of that dangles.

- `showPreferencesDialog` returns the window as a
  `juce::Component::SafePointer<juce::DialogWindow>`; `MainWindow` keeps it in a
  member and, in `~MainWindow()` before `settings->saveIfNeeded()`, calls
  `exitModalState (0)` on it if still alive.
- Rule (documented in `PreferencesDialog.h`): a page uses `AppSettings` only in its
  constructor and in click handlers, never in its destructor. Not pinned by an
  automated test (the only way to observe a violation is to use a dangling
  reference, i.e. undefined behaviour); the `SafePointer` close above is the real
  guard, and the quit-while-open case is checked by hand.

### 2. `AppSettings` write failure

`write()` and `setImportTrackOptions()` ignore `saveIfNeeded()`'s `false`.

- Setters return `bool` (true = persisted). The in-memory value is kept either way
  (`PropertiesFile` holds it), so the toggle still works for the session.
- `AppSettings` remembers the latest result: `bool lastSaveFailed() const`.
- `PreferencesDialog` shows a muted line at the bottom-left, "Settings could not be
  saved; changes last until you quit.", visible while `lastSaveFailed()`; each page
  takes an optional `std::function<void()> onChanged` (default empty, so direct
  construction in tests is unchanged) and calls it after each setter.
- Tests: a `PropertiesFile` on an unwritable path (read-only dir) → setter returns
  false, `lastSaveFailed()` true, value still reads back; a later successful write
  clears the flag.

### 3. Minimum dialog size

`options.resizable = true` with no limits. After `launchAsync`, call
`window->setResizeLimits (480, 300, 1600, 1200)`; `PreferencesDialog` initial size
stays 640 × 400. Test: constructed content's `getWidth/Height` ≥ the minimum (the
limit itself is a window property and is checked by hand).

### 4. `validateLoaded` and SECTION nodes

`SongDocument::validateLoaded` (`Source/UI/SongDocument.cpp:273`) walks tracks, notes
and events but ignores SECTIONS children. Add, per non-conductor track's SECTIONS
child (absent is fine — files that predate sections), rejecting with
`InvalidStructure` ("This Song file is damaged: …"):
- each child is a SECTION with an integer `sectionId` ≥ 1;
- `sectionId` unique across the whole Song;
- `startTick` ≥ 0 and `endTick` > `startTick`;
- if the root has `nextSectionId`, every `sectionId` < it (absent → fine, as
  `mintSectionId` already repairs it).

Overlap is deliberately NOT a rule: the sections design allows sections to overlap
(`2026-10-06-songsmith-sections-design.md`, "Sections may overlap"). Tests: each rule
has a failing fixture; a fresh, arranged and sectioned document still passes.

### 5. Dismissed import dialog stays on screen

`showImportOptionsDialog` (`Source/UI/ImportOptionsDialog.cpp:101`) calls
`window->exitModalState (1)` and then `accepted (chosen)` synchronously inside the
button click. The window is only deleted later by the modal manager, so a long
import runs with the dialog still painted. Fix: `window->setVisible (false)` before
invoking `accepted`. (Deferring via `MessageManager::callAsync` is the alternative;
rejected: it adds a re-entrancy window where the user can act before the import
starts.) Not headless-testable beyond "callback still fires once with the chosen
options" (existing test stays); visibility is checked by hand with a large MIDI.

### 6. Warnings and comment

- `-Wfloat-equal`, `Source/UI/SectionEdit.cpp` ~253 (`hitTestSection` tie-break
  `distance == bestDistance`): replace with a comparison that does not use `==` on
  doubles (`! (distance < best) && ! (best < distance)`), behaviour unchanged; the
  existing hit-test tie-break tests are the guard.
- `-Wreorder-ctor`, `Source/UI/SongsmithMainComponent.cpp:93`: reorder the
  constructor initialisers to match member declaration order. Must not change
  construction order (initialisers already run in declaration order; only the
  written order moves). Plan verifies no initialiser depends on a later member.
- `Source/UI/MidiImportPlan.cpp:181`: comment says `importMidi` guarantees notes are
  tick-sorted; it is really JUCE sequence order. Reword to say so. No code change.

## Testing

TDD per item. Suite stays green (807 now); `docs/TESTING.md` count updated. Items
checked by hand after deploy: dialog min size, import dialog dismissal with a large
file, quitting while Preferences is open (should close it, no crash).

## Risks

- Item 1 closes a modal dialog from `~MainWindow`; `exitModalState` on a window being
  torn down at shutdown must not re-enter `MainWindow`. Guard: only call when the
  SafePointer is non-null, and do it before any member the dialog uses is destroyed.
- Item 4 may reject a Song file that loads today. Each rule above is one the editor
  never produces (ids come from `mintSectionId`, edits clamp at tick 0 and keep
  `endTick` > `startTick`); a file that breaks one is damaged. Overlap is excluded
  because the editor does produce it. An accepted-today file that breaks a rule would
  be a bug in the rule, not the file.

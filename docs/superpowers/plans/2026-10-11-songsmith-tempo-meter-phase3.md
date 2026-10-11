# Songsmith Tempo/Meter Editor Window (phase 3, table) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A `TempoMeterEditorWindow` with an editable table of the song's tempo and meter events, reachable from Edit ▸ Tempo and Meter…, a double-click on the Conductor row and the Conductor head's right-click menu.

**Architecture:** A JUCE-free table module (`TempoMeterTable`) turns `listTempoMeterEvents` into display rows, parses typed cells and applies every edit through `TempoEdit` (so the window never writes the tree itself). A thin `juce::DocumentWindow` hosts a `juce::TableListBox` over those rows, refreshes (coalesced, async) when `SOURCE_MIDI` changes, and is owned by `SongsmithMainComponent` like the Track editor. Task 1 first pins the Conductor row's rebuild trigger, which phase 2 left unpinned.

**Tech Stack:** C++17, JUCE (`TableListBox`, `DocumentWindow`), Catch2. No new dependencies.

**Spec:** `docs/superpowers/specs/2026-10-10-songsmith-tempo-meter-design.md` (§4 entry points, §5 table half of the window, §6 phase 3). Phase 2 plan for style reference: `docs/superpowers/plans/2026-10-11-songsmith-tempo-meter-phase2.md`.

## Global Constraints

- Everything lives in `Source/UI/`; `Source/Core/` and `forge_core` are untouched (spec requirement 4).
- Neither the table module nor the window writes the ValueTree: every edit goes through `setTempo` / `moveTempo` / `removeTempo` / `setMeter` / `moveMeter` / `removeMeter` (`Source/UI/TempoEdit.h`). Each is one undo transaction.
- Events are addressed by tick, never by row index (rows are re-listed after every change).
- BPM range `minBpm() .. 1000` is rejected, not clamped; meter numerator 1–32, denominator one of 1, 2, 4, 8, 16, 32; a meter change snaps to the nearest bar start; the tick-0 event can be edited, not removed; at most one tempo and one meter per tick (all enforced by `TempoEdit`; this phase only surfaces the errors).
- Custom error types, no bare strings as errors. Add types/annotations everywhere; no `any`-style escapes. Prefer free functions over classes where practical.
- Use `juce::GlyphArrangement::getStringWidthInt`, never the deprecated `juce::Font::getStringWidth` (MSVC C4996 warnings; see `PartSlotComponent.cpp:63`).
- Do not launch the GUI (`forge_ui` / `song-smith`) from tests or agents; headless component tests only (`juce::ScopedJuceInitialiser_GUI`).
- Commits: conventional-commit prefixes, ending with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`. Never push.
- New `.cpp`/`.h` files go in **both** `CMakeLists.txt` (UI sources, next to `Source/UI/TrackEditorWindow.cpp`) and `Tests/CMakeLists.txt` (the source list next to `.../TrackEditorWindow.cpp`, plus the new `*_tests.cpp`).
- Test song fixtures: build bytes with `Tests/MidiTestBytes.h` (`miditest::smf`, `TrackBody`) and import with `importMidiFile`, as `Tests/TempoEdit_tests.cpp` and the phase 2 test in `Tests/TrackListComponent_tests.cpp:2853` do.

## Review Focus

Failure modes the spec implies but a straight implementation would miss (each is pinned by a test in the task named in brackets):

1. Typing nonsense into a cell (`""`, `abc`, `0`, `-5`, `1e9`, `nan`, `3/`, `3/5`, `0/4`) leaves the document unchanged and opens no undo step; the user sees a message and the old value. [Task 2]
2. Typing a meter tick that is not on a bar line snaps to the bar, and the table shows the snapped tick, not the typed one. Moving onto an occupied tick says so and changes nothing. [Task 2]
3. Undo/redo (and File ▸ New/Open) while the window is open: the table refreshes to the new events, a selected row that no longer exists is dropped without a crash, and the window closes when the document is replaced. [Task 3, Task 4]
4. Opening the window before anything is imported (no time base): empty table, Add/Remove disabled, no crash, no edit possible. [Task 3]
5. A tempo and a meter on the same tick are two rows; selection survives a refresh by `(kind, tick)`, not by index. [Task 3]

## File Structure

| File | Responsibility |
|---|---|
| NEW `Source/UI/TempoMeterTable.{h,cpp}` | JUCE-free: `tableRows` (display model), `parseBpm`/`parseMeter`/`parseTick`, `editValue`/`editTick`/`addEventAfter`/`removeEvent` over `TempoEdit`, `describe (TableEditError)` |
| NEW `Source/UI/TempoMeterEditorWindow.{h,cpp}` | `juce::DocumentWindow` hosting the table, Add Tempo / Add Meter / Remove buttons, a message label; async refresh; Ctrl+Z / Ctrl+Y forwarding |
| MOD `Source/UI/TrackListComponent.{h,cpp}` | `rebuildCountForTesting()`; `onConductorDoubleClicked`, `onTempoMeterRequested` forwards |
| MOD `Source/UI/TrackRowComponent.{h,cpp}` | Conductor double-click and conductor head right-click fire the new callbacks |
| MOD `Source/UI/TrackHeadComponent.{h,cpp}` | Context menu item 2 "Tempo and Meter…" on the Conductor head only |
| MOD `Source/UI/MenuModel.h` | `EditTempoMeter` command, `MenuState::hasTimeBase` |
| MOD `Source/UI/SongsmithMainComponent.{h,cpp}`, `MainWindow.cpp` | Own and open the window; menu dispatch; close on `documentReplaced` |
| NEW `Tests/TempoMeterTable_tests.cpp`, `Tests/TempoMeterEditorWindow_tests.cpp`; additions to `Tests/TrackListComponent_tests.cpp`, `Tests/TrackHeadComponent_tests.cpp`, `Tests/MenuModel_tests.cpp` | |

## Decisions made for the user (veto any)

- The table columns are Kind, Position (`bar.beat`, read-only), Tick (editable except on tick 0), Value (BPM or `n/d`, editable). The spec's "position" is thereby shown as bar.beat and tick, edited by tick.
- **Add Tempo / Add Meter** insert at the first bar start strictly after the selected row (or after the last event of that kind when nothing is selected) that holds no event of that kind, with the value currently in force there. No playhead dependency, deterministic.
- **Remove** acts on the selected row; Remove on the tick-0 row shows the `CannotRemoveFirst` message.
- Cell edits that fail revert the text and show the message in a label under the table (no modal dialogs).
- The menu item is enabled only once a MIDI has been imported (`SongDocument::hasTimeBase()`).

---

### Task 1: Pin the Conductor row's rebuild trigger

Phase 2's Conductor test passes even if `TrackListComponent` stops rebuilding on `SOURCE_MIDI` changes, because `renderOf` paints the existing preview straight from the tree. Add a rebuild counter and assert it moves.

**Files:**
- Modify: `Source/UI/TrackListComponent.h` (public, near the other `...ForTesting` members), `Source/UI/TrackListComponent.cpp` (`rebuild()`)
- Test: `Tests/TrackListComponent_tests.cpp` (after the test at line ~2853)

**Interfaces:**
- Produces: `int TrackListComponent::rebuildCountForTesting() const noexcept` — number of times `rebuild()` has run.

- [ ] **Step 1: Write the failing test**

Append after the existing test `"the Conductor row's markers follow setTempo, undo and redo"`:

```cpp
TEST_CASE ("TrackListComponent: a conductor tempo edit, undo and redo each rebuild the rows once the loop has run", "[track-list][conductor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const auto file = juce::File::createTempFile (".mid");
    const auto bytes = conductorSong();
    REQUIRE (file.replaceWithData (bytes.data(), bytes.size()));
    SongDocument doc;
    Diagnostics diags;
    REQUIRE (importMidiFile (doc, file, 1, diags));
    file.deleteFile();

    TrackListComponent list (doc);
    list.setBounds (0, 0, 900, 400);
    const auto pump = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil (100); };
    pump();

    int before = list.rebuildCountForTesting();
    REQUIRE_FALSE (setTempo (doc, 960, 90.0).has_value());
    CHECK (list.rebuildCountForTesting() == before);   // coalesced: nothing until the loop runs
    pump();
    CHECK (list.rebuildCountForTesting() > before);

    before = list.rebuildCountForTesting();
    doc.undo();
    pump();
    CHECK (list.rebuildCountForTesting() > before);

    before = list.rebuildCountForTesting();
    doc.redo();
    pump();
    CHECK (list.rebuildCountForTesting() > before);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target forge_tests 2>&1 | tail -5` (expected: compile error, no member `rebuildCountForTesting`).

- [ ] **Step 3: Implement**

In `TrackListComponent.h` add to the public section: `int rebuildCountForTesting() const noexcept { return rebuildCount; }` and to the private data `int rebuildCount = 0;   // test observability`. In `TrackListComponent::rebuild()` (first line of the body) add `++rebuildCount;`.

- [ ] **Step 4: Run to verify it passes, then prove it can fail**

Run: `cmake --build build --target forge_tests && ctest --test-dir build -R "conductor tempo edit, undo and redo" --output-on-failure` → PASS.
Then temporarily change `valueTreePropertyChanged`'s final `triggerAsyncUpdate();` and `valueTreeChildAdded`'s `triggerAsyncUpdate();` to no-ops, rebuild, confirm the new test FAILS, and **revert the mutation** (`git diff` must show only the counter).

- [ ] **Step 5: Commit**

```bash
git add Source/UI/TrackListComponent.h Source/UI/TrackListComponent.cpp Tests/TrackListComponent_tests.cpp
git commit -m "test(ui): pin the Conductor row's rebuild trigger with a rebuild counter

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: `TempoMeterTable` — display rows, parsing and edits

**Files:**
- Create: `Source/UI/TempoMeterTable.h`, `Source/UI/TempoMeterTable.cpp`
- Modify: `CMakeLists.txt`, `Tests/CMakeLists.txt` (sources + test file)
- Test: `Tests/TempoMeterTable_tests.cpp`

**Interfaces:**
- Consumes: `TempoMeterEvent`, `TempoMeterKind`, `TempoEditResult`, `TempoEditError`, `listTempoMeterEvents`, `setTempo/moveTempo/removeTempo/setMeter/moveMeter/removeMeter`, `roundedBpm`, `minBpm`, `maxBpm` (`TempoEdit.h`); `meterSegments`, `segmentAt`, `nearestBarStart`, `MeterSegment` (`MeterSegments.h`); `SongDocument::getMeterChanges()`, `getSourceMidiNode()` (`SongIDs::ticksPerQuarter`, default 480).
- Produces (all in `namespace lotro`, header is JUCE-free except the `SongDocument` forward declaration):

```cpp
struct TableRow
{
    TempoMeterKind kind = TempoMeterKind::tempo;
    int  tick = 0;
    std::string kindText;       // "Tempo" | "Meter"
    std::string barBeatText;    // "3.2" = bar 3, beat 2 (both 1-based)
    std::string tickText;       // decimal tick
    std::string valueText;      // "93.75" | "3/4"
    bool tickEditable = false;  // false on tick 0
};
std::vector<TableRow> tableRows (const std::vector<TempoMeterEvent>&, const std::vector<MeterChange>& meterMap, int ticksPerQuarter);

std::optional<double> parseBpm (const std::string&);                 // finite, > 0, whole text
std::optional<std::pair<int,int>> parseMeter (const std::string&);   // "n/d", both positive ints
std::optional<int> parseTick (const std::string&);                   // non-negative int, whole text

struct TableEditError
{
    enum class Kind { UnparsableBpm, UnparsableMeter, UnparsableTick, Rejected, NothingToRemove };
    Kind kind = Kind::Rejected;
    std::optional<TempoEditError> cause;   // set when kind == Rejected
};
using TableEditResult = std::optional<TableEditError>;   // nullopt = success
std::string describe (const TableEditError&);            // the one-line message the window shows

TableEditResult editValue (SongDocument&, const TempoMeterEvent& row, const std::string& text);
TableEditResult editTick  (SongDocument&, const TempoMeterEvent& row, const std::string& text);
// Inserts a new event as described under "Decisions"; `selected` may be empty.
TableEditResult addEvent    (SongDocument&, TempoMeterKind, std::optional<TempoMeterEvent> selected);
TableEditResult removeEvent (SongDocument&, const TempoMeterEvent& row);
```

Formatting rules: `valueText` for a tempo is the bare BPM with up to 2 decimals, trailing zeros trimmed (`120`, `93.75`; reuse `formatMarkerBpm` from `ConductorMarkers.h`); for a meter `n/d`. `barBeatText` is computed from `meterSegments (meterMap, ticksPerQuarter)`: `bar = firstBar + floor((tick - startTick)/ticksPerBar) + 1`, `beat = floor(((tick - startTick) mod ticksPerBar)/ticksPerBeat) + 1`. `tickEditable = tick > 0`. `describe` messages (exact): `UnparsableBpm` → "Enter a tempo in BPM, e.g. 120"; `UnparsableMeter` → "Enter a meter as n/d, e.g. 3/4"; `UnparsableTick` → "Enter a tick number, 0 or more"; `NothingToRemove` → "Select an event first"; `Rejected` by cause: `NoTimeBase` "Import a MIDI file first", `NegativeTick` "Position cannot be negative", `BpmOutOfRange` → "Tempo must be between 3.58 and 1000 BPM" (format the lower bound from `minBpm()` with 2 decimals), `MeterOutOfRange` "Meter must be 1-32 over 1, 2, 4, 8, 16 or 32", `TickOccupied` "There is already an event of that kind at that position", `NoSuchEvent` "That event no longer exists", `CannotRemoveFirst` "The first event cannot be removed".

Behaviour: `editValue` parses (tempo → `parseBpm` then `setTempo (doc, row.tick, bpm)`; meter → `parseMeter` then `setMeter (doc, row.tick, n, d)`); `editTick` → `parseTick` then `moveTempo (doc, row.tick, newTick, row.bpm)` / `moveMeter (doc, row.tick, newTick, row.numerator, row.denominator)`. Parse failure returns the matching `Unparsable*` error without touching the document. `removeEvent` → `removeTempo/removeMeter (doc, row.tick)`. `addEvent` computes the target tick with `nearestBarStart`/`meterSegments` arithmetic: start from `selected->tick` if present else the last listed event's tick of that kind; step forward one bar at a time (using the segment containing the tick, `tick = segmentAt(...)`'s next bar start: `tick + ticksPerBar` rounded to `nearestBarStart`) until no event of that kind is at that tick; the value is the last event of that kind at or before it (default 120 / 4/4). Then `setTempo` / `setMeter`.

- [ ] **Step 1: Write the failing tests**

Create `Tests/TempoMeterTable_tests.cpp` using the `Loaded`/`standardSong` pattern from `Tests/TempoEdit_tests.cpp` (copy the small helper structs; do not include that test file). Write these tests, each with real assertions:

```cpp
TEST_CASE ("TempoMeterTable: rows show kind, bar.beat, tick and value in listing order", "[tempo-meter-table]")
// standardSong + setTempo(doc, 1920, 93.75) + setMeter(doc, 1920, 3, 4):
// rows == { Tempo 1.1 / 0 / 120 (tickEditable false), Meter 1.1 / 0 / 4/4 (false),
//           Tempo 2.1 / 1920 / 93.75 (true), Meter 2.1 / 1920 / 3/4 (true) }.
// Also: tick 2400 under 3/4 starting at 1920 (ppq 480, 1440 ticks per bar) is "2.1" -> after the change,
// bar numbering restarts: check tableRows over a meter map { {0,4,4}, {1920,3,4} } gives a tempo at 3360 as "3.1".
```

```cpp
TEST_CASE ("TempoMeterTable: parsing accepts exactly well-formed text", "[tempo-meter-table]")
// parseBpm: "120"->120, " 93.75 "->93.75 (trim), "1e2" -> 100 is fine; rejects "", "abc", "0", "-5", "nan", "inf", "12x".
// parseMeter: "3/4"->{3,4}, " 6 / 8 "->{6,8}; rejects "", "3", "3/", "/4", "0/4", "3/0", "a/b", "3/4/5".
// parseTick: "0"->0, "1920"->1920; rejects "", "-1", "1.5", "x", "99999999999".
```

```cpp
TEST_CASE ("TempoMeterTable: an unparsable cell changes nothing and opens no undo step", "[tempo-meter-table]")
// Loaded doc; doc.getUndoManager().clearUndoHistory(); auto before = listTempoMeterEvents (doc).
// editValue on the tempo row returns kind == UnparsableBpm for "", "abc", "0", "-5", "nan";
// Rejected/BpmOutOfRange for "1001" and "1" (below minBpm()); on the meter row UnparsableMeter for "3/",
// Rejected/MeterOutOfRange for "3/5" and "33/4". After all of them: listTempoMeterEvents (doc) == before
// and CHECK_FALSE (doc.canUndo()) -- no transaction was opened.
```

```cpp
TEST_CASE ("TempoMeterTable: editValue and editTick go through TempoEdit", "[tempo-meter-table]")
// editValue on the tick-0 tempo row with "90" -> events()[0].bpm == Approx(roundedBpm(90).value()).
// editValue on the tick-0 meter row with "6/8" -> 6/8. editTick on a tempo at 1920 to "960" -> moved.
// editTick of a meter at 1920 to "2000" (not a bar line) -> the listing shows the meter at the nearest bar start
// (1920 under 4/4, ppq 480), NOT 2000. editTick moving a tempo onto an occupied tick -> Rejected/TickOccupied, unchanged.
// editTick on tick-0 rows is the caller's job (tickEditable false) but moveTempo(0,...) on the implicit row
// returns Rejected/NoSuchEvent -- assert that it does not crash and changes nothing.
```

```cpp
TEST_CASE ("TempoMeterTable: addEvent picks the next free bar after the selection and copies the value in force", "[tempo-meter-table]")
// standardSong (4/4, 120): addEvent(doc, tempo, nullopt) -> tempo at 1920 (one bar after the last tempo at 0), 120 BPM.
// With a tempo already at 1920 and selected = the tick-0 tempo row: result is 3840 (1920 occupied).
// addEvent(doc, meter, selected = meter row at 0) -> meter at 1920 4/4. With no time base (empty SongDocument): Rejected/NoTimeBase.
```

```cpp
TEST_CASE ("TempoMeterTable: removeEvent removes by tick and refuses tick 0", "[tempo-meter-table]")
// remove the tempo at 1920 -> gone. remove the tick-0 row -> Rejected/CannotRemoveFirst, unchanged.
```

```cpp
TEST_CASE ("TempoMeterTable: every message from describe is non-empty and distinct", "[tempo-meter-table]")
// iterate the four Kind values (+ every TempoEditError as Rejected cause); collect describe() into a std::set; sizes match; none empty.
```

- [ ] **Step 2: Run to verify they fail**

Add `TempoMeterTable_tests.cpp` to `Tests/CMakeLists.txt` and the (empty) `TempoMeterTable.cpp` to both CMakeLists, create the header with the declarations above and stub bodies that `return {}`. Run `cmake --build build --target forge_tests && ctest --test-dir build -R tempo-meter-table --output-on-failure`. Expected: FAIL.

- [ ] **Step 3: Implement** `TempoMeterTable.cpp` to the behaviour above. `parseBpm`/`parseMeter`/`parseTick` use `std::string` trimming and `std::from_chars`/`strtod` with a check that the whole (trimmed) string was consumed; `parseBpm` rejects non-finite or ≤ 0 (range rejection is `TempoEdit`'s job so the user gets the range message for `1001`).

- [ ] **Step 4: Run to verify they pass.** Same command; then the whole suite: `ctest --test-dir build --output-on-failure` (expect 1047 + new tests, all green).

- [ ] **Step 5: Commit** `feat(ui): TempoMeterTable — table rows, cell parsing and TempoEdit-backed edits` (with the Co-Authored-By trailer).

---

### Task 3: `TempoMeterEditorWindow`

**Files:**
- Create: `Source/UI/TempoMeterEditorWindow.h`, `Source/UI/TempoMeterEditorWindow.cpp`
- Modify: `CMakeLists.txt`, `Tests/CMakeLists.txt`
- Test: `Tests/TempoMeterEditorWindow_tests.cpp`

**Interfaces:**
- Consumes: everything produced by Task 2; `SongDocument::getSourceMidiNode()`, `undo()`, `redo()`, `hasTimeBase()`; `SongsmithColours::background`.
- Produces:

```cpp
class TempoMeterEditorWindow : public juce::DocumentWindow,
                               private juce::TableListBoxModel,
                               private juce::ValueTree::Listener,
                               private juce::AsyncUpdater
{
public:
    TempoMeterEditorWindow (SongDocument&, juce::Component* centreAround);   // centred 520x420, resizable
    ~TempoMeterEditorWindow() override;
    void closeButtonPressed() override;            // setVisible(false); onClosed()
    bool keyPressed (const juce::KeyPress&) override;   // Ctrl+Z -> doc.undo(); Ctrl+Y / Ctrl+Shift+Z -> doc.redo(); Delete -> remove selected
    std::function<void()> onClosed;
private:
    friend struct TempoMeterEditorWindowTestAccess;
    // refreshNow(): re-list; if the listing equals the displayed one, do nothing (keeps an open cell editor alive);
    // else replace it, restore the selection by (kind,tick) if still present, else none, and updateContent().
};
```

Layout: a `Content` component (not owned by the table) with the `juce::TableListBox` (columns Kind 80, Position 80, Tick 90, Value 120; header visible; single-row selection), a row of three `juce::TextButton`s (Add Tempo, Add Meter, Remove) and a `juce::Label` message line under the table. Value and Tick cells are `juce::Label`s with `setEditable (false, true)` (single click edits) created in `refreshComponentForCell`; non-editable cells (Kind, Position, and Tick on tick 0) are painted in `paintCell`. A Label's `onTextChange` calls `editValue` / `editTick`; on error, `describe` goes into the message label and the table is refreshed so the cell shows the stored value again (an unchanged document triggers no refresh, so force `table.updateContent()` + repaint of that row); on success clear the message. Buttons: Add Tempo/Add Meter call `addEvent (doc, kind, selectedEvent)`, Remove calls `removeEvent` (message "Select an event first" when nothing is selected); all three are disabled while `! doc.hasTimeBase()`. The window title is "Tempo and Meter". It registers as a listener on `doc.getSourceMidiNode()` (add in the constructor, remove in the destructor) and calls `triggerAsyncUpdate()` from every `valueTree*` callback; `handleAsyncUpdate()` calls `refreshNow()`.

- [ ] **Step 1: Write the failing tests**

Create `Tests/TempoMeterEditorWindow_tests.cpp` with a `TempoMeterEditorWindowTestAccess` friend struct exposing: `rowCount (w)`, `cellText (w, row, column)` (reading from the displayed `TableRow`s), `selectRow (w, row)`, `selectedRow (w)`, `commitCell (w, row, column, text)` (calls the same path as a Label's `onTextChange`), `message (w)`, `buttonEnabled (w, "Add Tempo")`, `pressAdd (w, kind)`, `pressRemove (w)`, `refreshNow (w)`. Tests (pump with `juce::MessageManager::getInstance()->runDispatchLoopUntil (100)` where the async refresh is part of the behaviour):

```cpp
TEST_CASE ("TempoMeterEditorWindow: lists the events and refreshes on edit, undo and redo", "[tempo-meter-window]")
// import standardSong; window shows 2 rows (tempo 120, meter 4/4). setTempo(doc,1920,90) -> pump -> 3 rows, row 2 = "90".
// doc.undo() -> pump -> 2 rows; doc.redo() -> pump -> 3 rows.
```

```cpp
TEST_CASE ("TempoMeterEditorWindow: typing a value edits through TempoEdit and one undo restores it", "[tempo-meter-window]")
// commitCell(row 0, Value, "100") -> listTempoMeterEvents(doc)[0].bpm ~ 100; doc.undo() -> back to 120; message empty.
```

```cpp
TEST_CASE ("TempoMeterEditorWindow: a rejected edit shows the message and leaves the document alone", "[tempo-meter-window]")
// commitCell(row 0, Value, "abc") -> message == describe(UnparsableBpm); events unchanged; doc.canUndo() false after clearUndoHistory().
// commitCell on the meter row with "3/5" -> message mentions "Meter must be"; unchanged.
```

```cpp
TEST_CASE ("TempoMeterEditorWindow: a meter tick edit shows the snapped tick", "[tempo-meter-window]")
// setMeter(doc,1920,3,4); commitCell(that row, Tick, "2000"); after refresh the Tick cell reads the nearest bar start, not "2000".
```

```cpp
TEST_CASE ("TempoMeterEditorWindow: selection follows (kind, tick) across a refresh and is dropped when its row disappears", "[tempo-meter-window]")
// tempo+meter both at 1920 (two rows with the same tick). Select the meter row; setTempo(doc, 960, 100) inserts a row above -> pump ->
// selected row is still the meter at 1920 (new index). removeMeter(doc,1920) via the doc -> pump -> no selection, no crash.
```

```cpp
TEST_CASE ("TempoMeterEditorWindow: buttons add and remove, and Remove on tick 0 explains itself", "[tempo-meter-window]")
// pressAdd(tempo) -> new row at 1920; select it; pressRemove -> gone. select row 0; pressRemove -> message == describe(Rejected/CannotRemoveFirst).
// pressRemove with no selection -> "Select an event first".
```

```cpp
TEST_CASE ("TempoMeterEditorWindow: with no time base the table is empty and every button is disabled", "[tempo-meter-window]")
// default SongDocument (nothing imported): rowCount == 0 (or whatever listTempoMeterEvents gives -- assert against it, and that
// the three buttons are disabled); then import standardSong -> pump -> buttons enabled, 2 rows.
```

```cpp
TEST_CASE ("TempoMeterEditorWindow: Ctrl+Z and Ctrl+Y reach the document's undo manager", "[tempo-meter-window]")
// setTempo; send juce::KeyPress ('z', ModifierKeys::commandModifier, 0) through w.keyPressed -> tempo gone; Ctrl+Y -> back.
```

```cpp
TEST_CASE ("TempoMeterEditorWindow: closing fires onClosed and hides the window", "[tempo-meter-window]")
```

- [ ] **Step 2: Run to verify they fail** (compile error: missing header; then stubbed behaviour FAIL).
- [ ] **Step 3: Implement** the window as specified. Remember: destructor removes the listener from `doc.getSourceMidiNode()` and calls `cancelPendingUpdate()`; `setContentOwned (content, false)`.
- [ ] **Step 4: Run** `ctest --test-dir build -R tempo-meter-window --output-on-failure`, then the full suite.
- [ ] **Step 5: Commit** `feat(ui): TempoMeterEditorWindow — editable tempo/meter table`.

---

### Task 4: Entry points and ownership

**Files:**
- Modify: `Source/UI/MenuModel.h`, `Source/UI/MainWindow.cpp` (`currentMenuState`, `menuItemSelected`), `Source/UI/SongsmithMainComponent.{h,cpp}`, `Source/UI/TrackListComponent.{h,cpp}`, `Source/UI/TrackRowComponent.{h,cpp}`, `Source/UI/TrackHeadComponent.{h,cpp}`
- Test: `Tests/MenuModel_tests.cpp`, `Tests/TrackHeadComponent_tests.cpp`, `Tests/TrackListComponent_tests.cpp`, `Tests/TempoMeterEditorWindow_tests.cpp`

**Interfaces:**
- Consumes: `TempoMeterEditorWindow` (Task 3).
- Produces:
  - `MenuCommandId::EditTempoMeter` (append after `ViewDiagnosticsToggle`, before `HelpAbout`; `allCommandIds` grows to 30 and gains it); `MenuState::hasTimeBase` (bool, default false); Edit menu item `item (EditTempoMeter, "Tempo and Meter...", {}, s.hasTimeBase)` placed after the `Quantize` item and its Grid Size submenu, preceded by a separator.
  - `TrackHeadComponent::onTempoMeterRequested` (`std::function<void()>`); `buildContextMenu()` adds item 2 `"Tempo and Meter\xe2\x80\xa6"` **only when the track is the conductor** (item 1 Rename stays, disabled on the conductor, as today).
  - `TrackRowComponent::onConductorActivated` (`std::function<void()>`), fired by (a) a double-click on a conductor row (both the `notePreview.onNonToggleDoubleClick` path and `mouseDoubleClick`) and (b) the head's "Tempo and Meter…" choice; a conductor right-click on the head now opens the head menu (today `mouseDown` skips conductors).
  - `TrackListComponent::onTempoMeterRequested` (`std::function<void()>`) wired in the row-creation code beside `onTrackDoubleClicked` (`TrackListComponent.cpp:168`).
  - `SongsmithMainComponent::openTempoMeterEditor()`, `bool isTempoMeterEditorOpen() const noexcept`, member `std::unique_ptr<TempoMeterEditorWindow> tempoMeterWindow;` — created on demand (`onClosed` resets it), re-fronted via `toFront (true)` when it already exists; reset in `documentReplaced()`.

- [ ] **Step 1: Write the failing tests**
  - `MenuModel_tests.cpp`: Edit menu now reads `Undo, Redo, -, Split, Delete, Select All, Select All Tracks, Select All Sections, -, Quantize, Grid Size>, -, Tempo and Meter...` (update the existing Edit-layout assertion's expected `V` accordingly); `EditTempoMeter` is disabled with `MenuState{}` and enabled with `hasTimeBase = true`; the uniqueness test still passes with the new id in `allCommandIds`.
  - `TrackHeadComponent_tests.cpp` (near line 235): on a normal track the menu has exactly the Rename item (ids `{1}`); on the conductor it has Rename (disabled) and item 2 "Tempo and Meter…" (enabled); `contextMenuChosen (2)` on the conductor fires `onTempoMeterRequested` once; on a normal track `contextMenuChosen (2)` does nothing.
  - `TrackListComponent_tests.cpp`: double-click on the conductor row's preview (use the same helper the existing double-click tests use for normal rows; call `TrackNotePreview::mouseDoubleClick` with a point outside the ghost toggle, or the `onNonToggleDoubleClick` callback) fires `list.onTempoMeterRequested` once and does **not** fire `onTrackDoubleClicked`; a normal row still fires `onTrackDoubleClicked` only.
  - `TempoMeterEditorWindow_tests.cpp` (or a `SongsmithMainComponent` test if one exists — grep `Tests/` for `SongsmithMainComponent`; if none, test through a new `TempoMeterEditorWindow`-only path and cover ownership by compile-check + the manual list in Task 5): `openTempoMeterEditor()` twice yields one window; `documentReplaced()` closes it.
- [ ] **Step 2: Run to verify they fail** (compile errors, then assertion failures).
- [ ] **Step 3: Implement** as listed. In `MainWindow::currentMenuState` set `s.hasTimeBase = songDocument.hasTimeBase();`; in `menuItemSelected` add `case EditTempoMeter: body->getSongsmith().openTempoMeterEditor(); return;`. In `SongsmithMainComponent`'s constructor wire `trackList.onTempoMeterRequested = [this] { openTempoMeterEditor(); };`.
- [ ] **Step 4: Run the full suite** `ctest --test-dir build --output-on-failure`. Expected: all green.
- [ ] **Step 5: Commit** `feat(ui): open the Tempo/Meter editor from Edit, the Conductor row and its head menu`.

---

### Task 5: Docs, cross-compile, deploy, manual list

**Files:** `docs/ARCHITECTURE.md` (§9.18 + the window/table), `docs/UI_GUIDE.md` (window, menu item, Conductor double-click, head menu), `docs/songsmith-ui-map.html`, `docs/TESTING.md` (new count and notable tests), `CLAUDE.md` (status line, docs-map row for this plan), the spec header (`phase 3 implemented`).

- [ ] **Step 1:** Update the docs above (get the exact test count from `ctest --test-dir build | tail -3`).
- [ ] **Step 2:** `./build-windows.sh forge_ui` (and `forge_tests` if Wine is usable) to catch MSVC-class warnings; fix any `-Wshadow`/deprecation issue in the new files only.
- [ ] **Step 3:** Deploy with the repo's deploy command (see `CLAUDE.md`) once the user okays it.
- [ ] **Step 4:** Give the user this manual list: open a MIDI with tempo changes → Edit ▸ Tempo and Meter…; double-click the Conductor row; right-click the Conductor head; edit a BPM cell, a tick cell, a meter cell; type garbage and see the message; Add/Remove; Ctrl+Z / Ctrl+Y in the window and Edit ▸ Undo in the main window (the Conductor row's markers and the ruler follow); File ▸ New while the window is open (it closes).
- [ ] **Step 5: Commit** `docs: Tempo/Meter editor window (phase 3)`. Do not push.

## Self-review

- Spec §4 entry points (double-click, Edit menu, head right-click) → Task 4; §5 table (columns, typed cells, Add Tempo/Add Meter/Remove, refresh on undo) → Tasks 2–3; "both views call TempoEdit, neither writes the tree" → Global Constraints + Task 2; undo shortcuts → Task 3; graph, meter strip and selection sync with the graph → phase 4 (out of scope here).
- Phase 2 open question (unpinned rebuild trigger) → Task 1.
- Names used across tasks: `TableRow`, `tableRows`, `editValue`, `editTick`, `addEvent`, `removeEvent`, `describe`, `TableEditError`, `TempoMeterEditorWindow`, `openTempoMeterEditor`, `onTempoMeterRequested`, `onConductorActivated`, `EditTempoMeter`, `hasTimeBase`, `rebuildCountForTesting` — consistent.
- Known soft spots the implementer should flag rather than guess: the exact undo-step assertion style in the "no undo step" test (use `clearUndoHistory()` then `canUndo()`), and the absence of a `SongsmithMainComponent` test harness (if none exists, ownership is compile-checked plus the manual list).

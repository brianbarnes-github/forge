# Songsmith MIDI editor: Swing grid — design

Date: 2026-10-16. Status: approved for planning once reviewed.

## Intent

The MIDI editor's **Options ▸ Grid Division ▸ Swing** exists but spaces the grid as Straight. Swing is a feel, not a fixed note value: each pair of grid steps becomes long–short ("on" then "off"), and how hard it swings is a matter of taste. The user wants a **strength** control and wants it to work **exactly like REAPER's MIDI editor swing grid**, so the grid lines, snapping and the strength scale behave the way a REAPER user expects.

## REAPER's model (what we copy)

- Swing is one of the grid divisions (Straight / Triplet / Dotted / Swing) beside the grid size.
- Strength runs **−100 % … +100 %**, centred on **0 % = straight**. REAPER stores it as −1..1 (`GetSetProjectGrid`: "swingamt is -1..1").
- Every **second** grid line moves; the first of each pair stays put. **+100 % moves it half a grid step later; −100 % half a step earlier** (ReaScripts wiki, REAPER mode: "-100 moves the note 1/2 grid unit to the left, 100 moves the note 1/2 grid unit to the right").
- So on a 1/8 grid: 0 % even eighths (1:1); ≈ +67 % triplet feel (2:1); +100 % dotted eighth + sixteenth (3:1); negative values are the same feels reversed (short first).
- The swung grid is what the editor draws and what notes snap to.

Sources: <https://www.reaper.fm/sdk/reascript/reascripthelp.html>, <https://github.com/jeremybernstein/ReaScripts/wiki/Parameter-Types>.

## Goals

1. A **Swing strength slider** on the MIDI editor's bottom bar (`EditorBottomBar`, added in `d442d28`).
2. With Grid Division = Swing, the editor grid becomes the swung grid everywhere the editor grid is used today.
3. The roll draws the swung lines.

## Non-goals

- No change to notes already in the Song. Swing changes the grid, not the music; notes move only when the user moves, resizes, inserts or quantizes them (guiding principle: the MIDI is the source of truth).
- No swing in the main window's Edit ▸ Grid Size, Preferences ▸ Editing default grid, or the ABC/LOTRO export.
- No groove templates, no humanise, no swing in playback.
- No swing for Triplet or Dotted (REAPER's swing is its own division).

## Anti-goals

- Saving the swing strength anywhere (Song, Preferences, `AppSettings`). The editor grid is session-only scratch — the user's standing rule for Grid Length / Division.
- A strength scale that differs from REAPER's (e.g. 50 % = straight MPC style, or 100 % = triplet).

## Decided by the user

- Copy REAPER exactly: −100 %..+100 %, 0 % straight, ±100 % = half a grid step, ≈ 67 % triplet feel.
- The control is a slider on the editor's bottom bar; it is the swing strength when Swing is the division.
- **Swing pairs restart at every bar line**, so downbeats are never swung, in any meter.

## The swung grid (definition)

Spacing `s` = `editorGridTicks(length, Straight, ppq)` (Swing spaces as Straight; unchanged). Strength `w` ∈ [−1, 1].

**Pairs restart at each bar line (user's decision).** Bars come from the conductor's meter map through `meterSegments` (`Source/UI/MeterSegments.h`; the map repeats its last meter forever). For a bar starting at `B` and ending at `E`, the swung grid's lines in that bar are, for `j = 0, 1, 2, …` while `B + j · s < E`:

- `B + j · s` when `j` is even (so `j = 0`, the bar line, is always a line and never moves);
- `B + j · s + w · s / 2` when `j` is odd;

each rounded to the nearest whole tick. An odd line whose shifted position would land at or past `E` is dropped (only possible when the bar is less than 1.5 steps past it, e.g. a 5/16 bar on a 1/4 grid). *Claude's call.*

Consequences:
- In any bar that is an even number of steps long (4/4 at 1/4 and finer, 6/8 at 1/8, …) every pair is a whole beat-group inside the bar, as a musician expects.
- In a bar with an odd number of whole steps (3/4 at 1/4, 5/8 or 7/8 at 1/8) the last step has no partner and stays straight; the next bar starts a fresh pair.
- In a bar that is not a whole number of steps (5/8 at 1/4) the last, partial step ends at the bar line, which is the next bar's first line.

This per-bar origin applies **only to the Swing division**. Straight, Triplet and Dotted keep today's lines (multiples of their spacing from tick 0). So Swing at 0 % equals Straight wherever every bar is a whole number of steps, and differs only in bars that are not (where Swing restarts at the bar line). At `|w| = 1` adjacent lines within a pair are `s/2` and `3s/2` apart, never coincident.

## Where the swung grid applies

Everything that today reads the editor's grid ticks uses the swung grid when the division is Swing:

| Use | Today | With Swing |
|---|---|---|
| Roll grid lines (`PianoRollComponent::drawGridlines` → `computeGridLines(..., EditorGridLines)`) | lines every `s` | even lines straight, odd lines shifted |
| Marker click on empty canvas (`PianoRollComponent::handleEditorMouseUp`, `snapTickToGrid`) | nearest line | nearest swung line |
| Editor ruler marker press/drag (`TrackEditorPanel::Content`, `onSetMarker`) | nearest line | nearest swung line |
| Note move (`SourceRollEditor`, grabbed note's start) | nearest line | nearest swung line (same delta for all dragged notes, as today) |
| Resize right / left (`SourceRollEditor`) | nearest line, at least one line from the other edge | same with swung lines |
| Double-click insert (`SourceRollEditor`, start at line at/before click, length `s`) | start = line at/before; length = `s` | start = swung line at/before; **length = to the next swung line** (*Claude's call*: gives the long/short pattern) |
| Navigate ▸ Move Marker Left / Right by Grid (`stepTickByGrid`) | next/previous line | next/previous swung line |
| Loop-range snap while the editor shows a track (`currentLoopSnapTicks`, editor ruler brackets) | editor grid | swung grid (*Claude's call*: it already follows the editor grid incl. Triplet) |
| Edit ▸ Quantize (`SourceRollEditor::quantizeSelection`) | start to nearest line; duration to a multiple of `s`, ≥ `s` | **start** to nearest swung line; duration rule unchanged (*Claude's call*) |

Alt still frees every snap, as today. Snap to Grid off still means no snap.

## Grid-line drawing

`computeGridLines` with an editor grid keeps bar lines, keeps note values whose spacing is a whole multiple of the editor spacing, and adds the editor grid's own lines at its level. With swing:

- The editor's own lines are the swung lines (even + odd), all at the length's level.
- With Swing, only **bar lines and the swung lines** are drawn; the coarser note-value lines are left out. (Note values are measured from tick 0, so after an odd bar a half-note line could fall right beside a shifted swung line.) *Claude's call.*
- The `minGridLinePixels` (8 px) test for the editor's own lines uses the **smallest gap** between swung lines, `s · (1 − |w|/2)`. *Claude's call.*

## The slider

On `EditorBottomBar`, at the left:

- A "Swing" label, a horizontal `juce::Slider` (LinearHorizontal, range −100..100, step 1, starts at 0), and a value readout such as `+67%`, `0%`, `-25%`. Drawn by `SongsmithLookAndFeel::drawLinearSlider` (exists). Width ≈ 160 px for the slider. *Claude's call.*
- Double-click resets to 0 (as the rate knob does).
- **Enabled only when** a track is loaded and Grid Division is Swing and Grid Length is not Off; otherwise greyed but visible (keeps the bar's layout stable). *Claude's call.*
- Moving it updates the grid live (lines repaint; snapping uses the new value on the next gesture). It never changes notes.
- Session-only. Lives with the panel's grid state: switching tracks keeps it; an editor opened afresh (first time / after New / Open — the same rule that resets the division to Straight) resets it to 0. *Claude's call, following the division's rule.*
- The value is not shown in or settable from the Options menu. *Claude's call.*

## Code shape (guidance for the builder)

- A JUCE-free value in `Source/UI/` (e.g. a new `EditorGrid.{h,cpp}`) describing the editor grid: spacing, division (or a swing flag), strength, and — for Swing — the meter segments it needs for bar starts. Operations: `snap(tick)`, `step(tick, direction)`, `lineAtOrBefore(tick)`, `lineAfter(tick)` and the lines in a tick range for drawing. For any non-Swing division it must reproduce `snapTickToGrid` / `stepTickByGrid` exactly (those stay, for the main window and other callers). Existing editor call sites switch from `(tick, gridTicks)` to the grid value. `editorGridTicks` stays. The value is rebuilt when the length, division, strength, PPQ or the meter map changes (tempo/meter edits and undo already notify the roll; reuse that path).
- `PianoRollComponent::setGrid(length, division)` gains the strength (or a separate `setSwing`); the roll pushes the `EditorGrid` to `SourceRollEditor`.
- `TrackEditorPanel` owns the bottom-bar slider wiring; `EditorBottomBar` gets the slider as a child and lays it out in `resized()`.
- `currentLoopSnapTicks` / `snapLoopTick` take the grid value instead of an int.
- All in `Source/UI/`; nothing in `Source/Core/` (check the `forge-engine-ui-boundary` skill).
- TDD; the pure grid maths gets its own tests (`[swing]`), including: non-Swing divisions equal today's functions; Swing `w = 0` in 4/4 equals Straight; ±100 % at 1/8; ≈ 67 % gives 2:1; 3/4 at 1/4 (third beat straight, every downbeat straight); 5/8 at 1/4 (partial last step); a meter change mid-song restarting pairs; a dropped odd line; rounding at odd PPQ; step from between lines and across a bar line; snap never below 0.

## Left to the builder

Names, file split, slider styling details, test file layout, and how the bottom bar exposes the slider to tests.

## Open questions

None. (Resolved: pairs restart at each bar line — user, 2026-10-16.)

# LOTRO ABC Converter — UI Design Notes

A design document for a JUCE-based MIDI editor and LOTRO ABC converter with a REAPER-inspired aesthetic.

---

## Core workflow

The app exists to bridge two worlds:

1. **MIDI source** — a pool of tracks accumulated from one or more imported MIDI files, each editable. Once imported, notes live entirely inside the song; nothing is referenced by path.
2. **LOTRO song arrangement** — a collection of parts, each assigned to a LOTRO instrument, which produces the final ABC output

Tracks are dragged from the source into part slots to create assignments. Assignments are non-destructive references: the source track stays editable, and edits propagate to the preview automatically.

---

## Layout: three stacked regions

The final layout is vertical, with the workflow flowing top → bottom:

```
┌────────────────────────────────────────────────┐
│ Menu bar + Toolbar (transport, tempo, export)  │
├────────────────────────────────────────────────┤
│ ▲ MIDI SOURCE                                  │
│ ┌──────────┬───────────────────────────────┐   │
│ │ Tracks   │  Source piano roll            │   │
│ │ (drag)   │  (edit notes here)            │   │
│ └──────────┴───────────────────────────────┘   │
├────────────────────────────────────────────────┤
│ PART STRIP — drop targets + assignment chips   │
│ [ Part 1 Lute ] [ Part 2 Harp ] [ Part 3 … ]   │
├────────────────────────────────────────────────┤
│ ▼ LOTRO PREVIEW — selected part                │
│ ┌──────────┬───────────────────────────────┐   │
│ │ Assigned │  Preview piano roll           │   │
│ │ tracks + │  (range band + out-of-range   │   │
│ │ inspector│   warnings + ghost notes)     │   │
│ └──────────┴───────────────────────────────┘   │
└────────────────────────────────────────────────┘
```

### Top half: MIDI source

Pure MIDI editing. Doesn't know or care about LOTRO.

- **Track list (left)** — draggable rows, one per MIDI track. Each row shows track number, name, note count, pitch range, and a color swatch.
- **Source piano roll (right)** — standard piano roll showing notes for the selected track. Full editing: drag, resize, create, delete, quantize, transpose.

### Middle: Part strip

Horizontal strip of LOTRO part slots. Acts as both the arrangement and as navigation for the preview.

- Each slot shows: part number, instrument badge, part name, assignment chips (one per assigned track)
- **Drop zone** for dragging tracks from above
- **Selection driven** — clicking a slot selects it; selected slot gets an amber top-border
- **Multi-track parts** — a slot can hold multiple chips (Track 1 + Track 2 both → Lute)
- **Multi-part tracks** — a single track can be dropped onto multiple parts (Track 1 → Lute *and* Harp)
- **Unassign** — × button on each chip
- **+ Add** button at the end to create new parts

### Bottom half: LOTRO preview

The "reality check" view — shows what will actually play in-game.

- **Left panel** — the assigned tracks (with their per-assignment settings), instrument info (name, range), range policy, output stats (notes emitted, notes clipped)
- **Preview piano roll (right)** — renders the assigned notes *after* all transformations: transpose applied, range clamped, quantized
- **Range band overlay** — shaded background showing the instrument's playable range
- **Out-of-range zones** — tinted red above and below the range band
- **Out-of-range notes** — rendered in red
- **Ghost notes** — dashed outlines showing where notes will end up after range policy (octave shift, clamp, etc.)
- **Status bar** — in-range count, out-of-range count, current policy action

---

## Key design principles

### 1. Two distinct worlds bridged by drag-and-drop

The MIDI and LOTRO domains are kept separate. Tracks exist independently; assignment is a separate act. Users can re-import MIDI or re-edit tracks without disturbing arrangement, and vice versa.

### 2. Non-destructive assignments

An assignment is a *reference* to a track plus per-assignment settings (transpose, quantize, range policy). The source track stays editable. Editing a track updates every part it's assigned to automatically.

### 3. Selection-driven preview

One part is selected at a time. The bottom piano roll shows what *that* part will sound like. This keeps the preview focused on debugging a single part rather than getting lost in a tangle of overlaid ensemble notes.

Consider adding an "All parts" mode as a dropdown on the bottom header, for the occasional "how does the whole thing look" check.

### 4. Make invisible logic visible

LOTRO's constraints (instrument ranges, range policy behavior) are invisible in typical MIDI editors — you only find out when the song sounds wrong in-game. This app surfaces those constraints directly in the piano roll:

- **Range band** shows the instrument's playable range
- **Red out-of-range zones** make violations instantly obvious
- **Red notes** mark specific violations
- **Ghost notes** show where the range policy will move them

This is the single biggest ergonomic win over a generic DAW for LOTRO conversion.

### 5. REAPER-inspired aesthetic

Dark theme, dense information, utilitarian. Not a pretty app — an *efficient* app. Rows with controls, minimal chrome, information over decoration.

---

## Data model

```
Song
 ├── SongMetadata (title, tempo, time sig, global transpose)
 ├── SourceMidi (embedded track pool — no external file references)
 │    └── Tracks[] (editable — user cleanup happens here; accumulated
 │         │        from one or more MIDI imports)
 │         └── Notes[]
 └── Parts[]
      ├── PartMetadata (name, instrument, player number)
      └── Assignments[]
           ├── trackId (reference into SourceMidi.Tracks)
           ├── transpose (semitones)
           ├── quantize (grid)
           └── rangePolicy (octaveShift | clamp | drop)
```

Key decisions:

- **`SourceMidi` is an embedded track pool, not a file reference.** Importing a MIDI file copies its tracks into `SourceMidi.Tracks[]`; importing another file appends more. The original `.mid` files are not retained or re-read — once imported, every note lives inside the song data file, which is fully self-contained.
- **Source tracks are the single source of truth** for notes. Parts don't own notes; they reference tracks.
- **Assignments are the linking layer.** They hold per-(track, part) settings.
- **The ABC exporter is a pure function** of the song model. No UI state involved.
- Use JUCE's `ValueTree` for this — gives you undo/redo, listeners, and serialization for free.

---

## JUCE implementation notes

### Layout primitives

- Top-level `Component` with nested `StretchableLayoutManager`s
- Vertical splitter between the three main regions (resizable is nice)
- Horizontal splitter within each piano-roll region (track list | piano roll)
- Consider `ResizableEdgeComponent` for splitter handles

### Piano roll (shared component)

Build **one** `PianoRollComponent` used in both halves. Takes:
- A note source (`MidiMessageSequence` or your own model)
- Optional render decorations: `rangeBand`, `outOfRangeOverlay`, `ghostNoteSource`, `colorByTrack`

The top roll gets a plain render. The bottom roll gets overlays, red zones, and ghost notes.

Implementation:
- Custom `Component::paint()` drawing notes as rectangles
- Mouse handlers for drag/resize/create/delete
- Viewport wrapping it for scroll
- Velocity lane as a sibling component below (optional second track)

### Drag-and-drop

JUCE's drag-and-drop is solid:
- Wrap the main window in a `DragAndDropContainer`
- Track rows call `startDragging()` with a payload identifying the track
- Part slot components implement `DragAndDropTarget`
- `isInterestedInDragSource()` validates drops
- `itemDropped()` creates the assignment in the ValueTree

### Scroll sync

Top and bottom piano rolls should share horizontal scroll and zoom (same time axis).

- Shared `Range<double>` for visible time range
- Both rolls listen to it (`Value::Listener` or similar)
- Vertical scroll stays independent per roll — the preview may zoom to the instrument's range while the source shows everything

### Live preview pipeline

When selected part or its assignment settings change:

1. Gather all assignments for the selected part
2. For each assignment: fetch source track notes → apply transpose → apply quantize → apply range policy
3. Merge the resulting note streams (if multi-track)
4. Feed the merged `MidiMessageSequence` to the bottom piano roll

Keep this pipeline **pure** — a function from (song model, selected part ID) → preview sequence. Trivially testable. Caches well.

### Theming

Subclass `LookAndFeel_V4`, override color IDs to match REAPER's palette:

- Background: `#2b2b2b` / `#2a2a2a`
- Panel headers: `#383838`
- Selected rows: `#3d4a5a` (blue tint)
- Borders: `#1a1a1a`
- Text primary: `#d0d0d0`
- Text muted: `#888`
- Accent (selection/export): `#e0b080` (amber)
- Warning: `#c06060` (red)

Use a monospace font for numeric fields (tempo, positions, stats). JUCE's default sans is fine for labels.

---

## Build sequence

The project is large. Suggested sequence to avoid getting stuck in UI paralysis:

1. **Data model first.** Define the `ValueTree` schema on paper. Song → Tracks → Notes, Song → Parts → Assignments. Every UI decision later is "how do I show/edit this node?"
2. **ABC exporter as a pure function** of the model. No UI. Verify you can produce valid LOTRO ABC from a hand-constructed tree. This forces you to confront conversion rules (range clamping, quantization, part splitting) before they tangle with UI code.
3. **MIDI import → model.** Another pure function. Now you can round-trip: MIDI in → model → ABC out.
4. **Part strip UI.** Simplest UI piece — just a list of parts with chips and drop targets. Lets you assign tracks to parts without any piano roll yet.
5. **Source piano roll.** View-only first, editing later. Shows one track at a time.
6. **Preview piano roll.** Shares the component from step 5, adds range band + red zones + ghost notes.
7. **Piano roll editing.** Add mouse handlers for creating, moving, resizing, deleting notes.
8. **Polish.** Themeing, keyboard shortcuts, undo/redo surface, presets.

This keeps a working ABC converter available at every stage.

---

## Open questions worth deciding early

### Can one part hold multiple tracks?

Yes. Common LOTRO workflow (stacking tracks onto one instrument). The chip model handles it: a slot holds a list of chips.

### Can one track feed multiple parts?

Yes. Also common (same notes, two instruments). The chip is a reference, not ownership.

### Track segments (regions) vs. whole tracks?

Defer. Start with whole-track assignment. Add time-range selection later if real songs need it.

### Where does ABC preview live?

A collapsible bottom panel, toggleable with a keyboard shortcut. Visible for debugging, hidden during normal editing. Could also be a separate window — JUCE makes that easy.

### Master preview (all parts overlaid)?

Add as a dropdown on the bottom header: `Part 1 Lute ▾` → `All parts`. Cheap to implement, occasionally very useful for ensemble-level checks.

---

## What this UI gets right

- **Matches the mental model** — source and destination are visually separated, connected by an explicit drag gesture
- **Non-destructive workflow** — edits to source propagate; assignments are cheap to make and undo
- **LOTRO-specific affordances** — range bands, out-of-range warnings, ghost notes are things no general DAW gives you
- **One piano-roll component, two roles** — the same widget serves source editing and destination preview, reducing code and cognitive load
- **Selection-driven** — one part previewed at a time keeps the bottom panel focused and uncluttered
- **Familiar aesthetic** — REAPER-style density means experienced users feel at home immediately

---

## References to check before coding

- **Helio Workstation** (open source JUCE DAW) — largest reference implementation of a JUCE piano roll
- **JUCE DragAndDropContainer docs** — validation flow and visual feedback
- **JUCE ValueTree tutorial** — especially undo/redo and listener patterns
- **Maestro / BruTE** (existing LOTRO ABC tools) — conversion conventions, header format, pitch shift markers, part-splitting rules
- **LotroInstruments.sf2** — for optional audio preview of the converted output

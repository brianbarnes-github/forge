#pragma once

// Pure model behind the Tempo/Meter editor's table: display rows, cell text
// parsing and the TempoEdit-backed edits (spec 2026-10-10, sections 2 and 5).
// No JUCE types in this API.

#include "UI/TempoEdit.h"

#include "Core/Song.h" // MeterChange

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lotro
{

class SongDocument;

struct TableRow
{
    TempoMeterKind kind = TempoMeterKind::tempo;
    int            tick = 0;
    std::string    kindText;          // "Tempo" | "Meter"
    std::string    barBeatText;       // "3.2" = bar 3, beat 2 (both 1-based)
    std::string    tickText;          // decimal tick
    std::string    valueText;         // "93.75" | "3/4"
    bool           tickEditable = false;   // false on tick 0
};

std::vector<TableRow> tableRows (const std::vector<TempoMeterEvent>& events,
                                 const std::vector<MeterChange>& meterMap,
                                 int ticksPerQuarter);

std::optional<double>             parseBpm   (const std::string& text);   // finite, > 0, whole text
std::optional<std::pair<int,int>> parseMeter (const std::string& text);   // "n/d", both positive ints
std::optional<int>                parseTick  (const std::string& text);   // non-negative int, whole text

struct TableEditError
{
    enum class Kind { UnparsableBpm, UnparsableMeter, UnparsableTick, Rejected, NothingToRemove };
    Kind                         kind = Kind::Rejected;
    std::optional<TempoEditError> cause;   // set when kind == Rejected
};

using TableEditResult = std::optional<TableEditError>;   // nullopt = success

std::string describe (const TableEditError& error);      // the one-line message the window shows

TableEditResult editValue (SongDocument&, const TempoMeterEvent& row, const std::string& text);
TableEditResult editTick  (SongDocument&, const TempoMeterEvent& row, const std::string& text);
// Inserts an event of `kind` at the first free bar start after `selected` (or after the
// last event of that kind), carrying the value in force there. `selected` may be empty.
TableEditResult addEvent    (SongDocument&, TempoMeterKind kind, std::optional<TempoMeterEvent> selected);
TableEditResult removeEvent (SongDocument&, const TempoMeterEvent& row);
// Removes every row in one undo step; tick-0 rows and rows no longer present are skipped.
TableEditResult removeEvents (SongDocument&, const std::vector<TempoMeterEvent>& rows);

// Creates the tempo at `tick` or edits the one there (works on the implicit tick-0 default).
TableEditResult setTempoAt  (SongDocument&, int tick, double bpm);
// Moves the tempo `row` to `toTick` with `bpm`. Tick-0 rows and toTick == row.tick use setTempo (the
// implicit default has no event to move). Re-finds `row` by (kind, tick): NoSuchEvent if gone.
// Does nothing and returns success when the tick and the (rounded) bpm are both unchanged.
TableEditResult moveTempoTo (SongDocument&, const TempoMeterEvent& row, int toTick, double bpm);
// Creates or edits the meter at the bar nearest `tick`.
TableEditResult setMeterAt  (SongDocument&, int tick, int numerator, int denominator);
// Moves the meter `row` to the bar nearest `toTick`, keeping its stored n/d (re-found, not the caller's copy).
// Unchanged (snapped tick == row.tick) -> success without an edit. Tick-0 row: unchanged -> success.
TableEditResult moveMeterTo (SongDocument&, const TempoMeterEvent& row, int toTick);

} // namespace lotro

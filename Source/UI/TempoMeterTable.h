#pragma once

// Pure model behind the Tempo/Meter editor's table: display rows, cell text
// parsing and the TempoEdit-backed edits (spec 2026-10-10, sections 2 and 5).
// No JUCE types in this API.

#include "UI/TempoEdit.h"

#include "Core/Song.h" // MeterChange
#include "UI/MeterSegments.h"

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

// "3.2" = bar 3, beat 2 (1-based) at `tick`, counted through the segments; empty when
// there are none. The Position column and the window's readout share this.
std::string barBeatTextAt (const std::vector<MeterSegment>& segments, int tick);

// "0:12.5": minutes, then seconds rounded to a tenth (negative reads as 0).
std::string formatClock (double seconds);

// The window's readout for a selected event: "Selected: bar 5.2  ·  tick 7680  ·  0:12.5".
// `seconds` is the tick's time from the tempo map.
std::string selectedPositionText (int tick, double seconds, const std::vector<MeterChange>& meterMap,
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

} // namespace lotro

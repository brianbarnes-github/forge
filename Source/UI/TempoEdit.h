#pragma once

// Undoable tempo and meter edits. They write only the conductor's FF 51 / FF 58
// EVENTs; TempoMapSync rebuilds the maps from those (spec 2026-10-10,
// sections 1 and 2). Each successful call is one undo transaction; a rejected
// call changes nothing and opens none.

#include <optional>
#include <vector>

namespace lotro
{

class SongDocument;

enum class TempoEditError
{
    NoTimeBase,          // nothing imported yet: there is no tempo or meter to edit
    NegativeTick,
    BpmOutOfRange,       // outside minBpm() .. maxBpm
    MeterOutOfRange,     // numerator 1-32, denominator 1, 2, 4, 8, 16 or 32
    TickOccupied,        // a move onto a tick that already holds an event of that kind
    NoSuchEvent,         // no event of that kind at the given tick
    CannotRemoveFirst    // the tick-0 event can be edited but not removed
};

using TempoEditResult = std::optional<TempoEditError>;   // nullopt = success

constexpr double maxBpm = 1000.0;
double minBpm();                      // 60e6 / 16777215: what 24-bit microseconds can encode
double roundedBpm (double bpm);       // the BPM an event of whole microseconds actually stores

// Creates the tempo at `tick`, or edits the one already there.
TempoEditResult setTempo    (SongDocument&, int tick, double bpm);
// Moves the tempo at `fromTick` to `toTick` and sets its BPM. The tick-0 tempo stays at 0.
TempoEditResult moveTempo   (SongDocument&, int fromTick, int toTick, double bpm);
TempoEditResult removeTempo (SongDocument&, int tick);

// Creates or edits the meter at the bar start nearest `tick` (nearestBarStart over
// the current meters). Editing keeps the event's clocks-per-click / 32nds bytes.
TempoEditResult setMeter    (SongDocument&, int tick, int numerator, int denominator);
// Moves the meter at `fromTick` to the bar start nearest `toTick`, computed with
// the meters in force without it. The tick-0 meter stays at 0.
TempoEditResult moveMeter   (SongDocument&, int fromTick, int toTick, int numerator, int denominator);
TempoEditResult removeMeter (SongDocument&, int tick);

enum class TempoMeterKind { tempo, meter };

struct TempoMeterEvent
{
    TempoMeterKind kind        = TempoMeterKind::tempo;
    int            tick        = 0;
    double         bpm         = 0.0;   // tempo events
    int            numerator   = 0;     // meter events
    int            denominator = 0;
};

// Every tempo and meter event (the derived maps), by tick, tempo before meter on a tie.
// Before the first edit of a kind, a conductor without that kind lists the implicit
// default (120 BPM, 4/4) as a tick-0 row that has no event behind it: setX works on it
// (and the first setX at a later tick writes the default at tick 0 explicitly, in the
// same undo step), while moveX at tick 0 gives NoSuchEvent and removeX(0) CannotRemoveFirst.
std::vector<TempoMeterEvent> listTempoMeterEvents (const SongDocument&);

} // namespace lotro

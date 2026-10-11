#include "UI/TempoMeterTable.h"

#include "UI/ConductorMarkers.h"
#include "UI/MeterSegments.h"
#include "UI/SongDocument.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace lotro
{

namespace
{
    std::string trim (const std::string& s)
    {
        const auto isSpace = [] (unsigned char c) { return std::isspace (c) != 0; };
        const auto first = std::find_if_not (s.begin(), s.end(), isSpace);
        const auto last  = std::find_if_not (s.rbegin(), s.rend(), isSpace).base();
        return first < last ? std::string (first, last) : std::string();
    }

    // A positive int from `s`: digits only, in range.
    std::optional<int> parsePositiveInt (const std::string& s)
    {
        const auto t = trim (s);
        if (t.empty() || ! std::all_of (t.begin(), t.end(), [] (char c) { return c >= '0' && c <= '9'; }))
            return std::nullopt;
        int value = 0;
        const auto [end, ec] = std::from_chars (t.data(), t.data() + t.size(), value);
        if (ec != std::errc() || end != t.data() + t.size() || value <= 0)
            return std::nullopt;
        return value;
    }

    int ticksPerQuarterOf (const SongDocument& doc)
    {
        return (int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter, 480);
    }

    // The first bar line strictly after `tick`: counted in the bars of the segment holding
    // `tick`, or the next segment's start when that comes first.
    int nextBarStartAfter (const std::vector<MeterSegment>& segments, int tick)
    {
        const auto& seg = segmentAt (segments, (double) tick);
        const double bars = std::floor (((double) tick - seg.startTick) / seg.ticksPerBar) + 1.0;
        double next = seg.startTick + bars * seg.ticksPerBar;
        for (const auto& s : segments)
            if (s.startTick > seg.startTick)
            {
                next = std::min (next, s.startTick);
                break;
            }
        return (int) std::llround (next);
    }

    TableEditResult rejected (TempoEditError cause) { return TableEditError { TableEditError::Kind::Rejected, cause }; }
    TableEditResult fail (TableEditError::Kind kind) { return TableEditError { kind, std::nullopt }; }

    // The event of `row`'s (kind, tick) as the document holds it now.
    std::optional<TempoMeterEvent> findCurrent (const SongDocument& doc, const TempoMeterEvent& row)
    {
        for (const auto& e : listTempoMeterEvents (doc))
            if (e.kind == row.kind && e.tick == row.tick)
                return e;
        return std::nullopt;
    }

    TableEditResult fromTempoEdit (const TempoEditResult& r)
    {
        return r ? rejected (*r) : TableEditResult {};
    }
}

std::string barBeatTextAt (const std::vector<MeterSegment>& segments, int tick)
{
    if (segments.empty())
        return {};
    const auto& seg = segmentAt (segments, (double) tick);
    const double into = (double) tick - seg.startTick;
    const long long bar = seg.firstBar + (long long) std::floor (into / seg.ticksPerBar) + 1;
    const double inBar = into - std::floor (into / seg.ticksPerBar) * seg.ticksPerBar;
    const long long beat = (long long) std::floor (inBar / seg.ticksPerBeat) + 1;
    return std::to_string (bar) + "." + std::to_string (beat);
}

std::string formatClock (double seconds)
{
    const long long tenths = std::isfinite (seconds) && seconds > 0.0 ? std::llround (seconds * 10.0) : 0;
    const long long minutes = tenths / 600;
    const long long rest = tenths % 600;   // tenths of a second within the minute
    char buffer[48];
    std::snprintf (buffer, sizeof buffer, "%lld:%02lld.%lld", minutes, rest / 10, rest % 10);
    return buffer;
}

std::string selectedPositionText (int tick, double seconds, const std::vector<MeterChange>& meterMap,
                                  int ticksPerQuarter)
{
    const auto barBeat = barBeatTextAt (meterSegments (meterMap, ticksPerQuarter), tick);
    const std::string separator = "  \xC2\xB7  ";   // "  ·  " in UTF-8
    return "Selected: bar " + (barBeat.empty() ? std::string ("-") : barBeat) + separator
         + "tick " + std::to_string (tick) + separator + formatClock (seconds);
}

std::vector<TableRow> tableRows (const std::vector<TempoMeterEvent>& events,
                                 const std::vector<MeterChange>& meterMap, int ticksPerQuarter)
{
    const auto segments = meterSegments (meterMap, ticksPerQuarter);
    std::vector<TableRow> rows;
    rows.reserve (events.size());

    for (const auto& e : events)
    {
        TableRow row;
        row.kind = e.kind;
        row.tick = e.tick;
        row.tickText = std::to_string (e.tick);
        row.tickEditable = e.tick > 0;
        row.kindText = e.kind == TempoMeterKind::tempo ? "Tempo" : "Meter";
        row.valueText = e.kind == TempoMeterKind::tempo
                            ? formatMarkerBpm (e.bpm)
                            : std::to_string (e.numerator) + "/" + std::to_string (e.denominator);

        row.barBeatText = barBeatTextAt (segments, e.tick);
        rows.push_back (std::move (row));
    }
    return rows;
}

std::optional<double> parseBpm (const std::string& text)
{
    const auto t = trim (text);
    // Plain decimal / exponent notation only: no hex, "nan" or "inf" spellings.
    if (t.empty() || ! std::all_of (t.begin(), t.end(), [] (char c)
                                    { return (c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-'; }))
        return std::nullopt;

    char* end = nullptr;
    const double value = std::strtod (t.c_str(), &end);
    if (end != t.c_str() + t.size() || ! std::isfinite (value) || value <= 0.0)
        return std::nullopt;
    return value;
}

std::optional<std::pair<int,int>> parseMeter (const std::string& text)
{
    const auto slash = text.find ('/');
    if (slash == std::string::npos)
        return std::nullopt;
    const auto n = parsePositiveInt (text.substr (0, slash));
    const auto d = parsePositiveInt (text.substr (slash + 1));   // a second '/' is not a digit: rejected
    if (! n || ! d)
        return std::nullopt;
    return std::make_pair (*n, *d);
}

std::optional<int> parseTick (const std::string& text)
{
    const auto t = trim (text);
    if (t == "0")
        return 0;
    return parsePositiveInt (t);
}

std::string describe (const TableEditError& error)
{
    switch (error.kind)
    {
        case TableEditError::Kind::UnparsableBpm:   return "Enter a tempo in BPM, e.g. 120";
        case TableEditError::Kind::UnparsableMeter: return "Enter a meter as n/d, e.g. 3/4";
        case TableEditError::Kind::UnparsableTick:  return "Enter a tick number, 0 or more";
        case TableEditError::Kind::NothingToRemove: return "Select an event first";
        case TableEditError::Kind::Rejected:        break;
    }

    switch (error.cause.value_or (TempoEditError::NoSuchEvent))
    {
        case TempoEditError::NoTimeBase:        return "Import a MIDI file first";
        case TempoEditError::NegativeTick:      return "Position cannot be negative";
        case TempoEditError::BpmOutOfRange:
        {
            char low[32];
            std::snprintf (low, sizeof low, "%.2f", minBpm());
            return std::string ("Tempo must be between ") + low + " and 1000 BPM";
        }
        case TempoEditError::MeterOutOfRange:   return "Meter must be 1-32 over 1, 2, 4, 8, 16 or 32";
        case TempoEditError::TickOccupied:      return "There is already an event of that kind at that position";
        case TempoEditError::NoSuchEvent:       return "That event no longer exists";
        case TempoEditError::CannotRemoveFirst: return "The first event cannot be removed";
    }
    return {};
}

TableEditResult editValue (SongDocument& doc, const TempoMeterEvent& row, const std::string& text)
{
    // `row` may be a stale copy: only its (kind, tick) identity is used, and the event must
    // still exist (setTempo / setMeter would otherwise recreate a deleted one).
    const auto current = findCurrent (doc, row);
    if (row.kind == TempoMeterKind::tempo)
    {
        const auto bpm = parseBpm (text);
        if (! bpm)
            return fail (TableEditError::Kind::UnparsableBpm);
        return current ? fromTempoEdit (setTempo (doc, row.tick, *bpm)) : rejected (TempoEditError::NoSuchEvent);
    }
    const auto meter = parseMeter (text);
    if (! meter)
        return fail (TableEditError::Kind::UnparsableMeter);
    return current ? fromTempoEdit (setMeter (doc, row.tick, meter->first, meter->second))
                   : rejected (TempoEditError::NoSuchEvent);
}

TableEditResult editTick (SongDocument& doc, const TempoMeterEvent& row, const std::string& text)
{
    const auto tick = parseTick (text);
    if (! tick)
        return fail (TableEditError::Kind::UnparsableTick);
    const auto current = findCurrent (doc, row);   // the stored value, not the caller's copy
    if (! current)
        return rejected (TempoEditError::NoSuchEvent);
    return fromTempoEdit (row.kind == TempoMeterKind::tempo
                              ? moveTempo (doc, row.tick, *tick, current->bpm)
                              : moveMeter (doc, row.tick, *tick, current->numerator, current->denominator));
}

TableEditResult addEvent (SongDocument& doc, TempoMeterKind kind, std::optional<TempoMeterEvent> selected)
{
    const auto listed = listTempoMeterEvents (doc);
    std::vector<TempoMeterEvent> ofKind;
    for (const auto& e : listed)
        if (e.kind == kind)
            ofKind.push_back (e);

    const auto occupied = [&] (int tick)
    {
        return std::any_of (ofKind.begin(), ofKind.end(), [tick] (const TempoMeterEvent& e) { return e.tick == tick; });
    };

    const auto segments = meterSegments (doc.getMeterChanges(), ticksPerQuarterOf (doc));
    int tick = selected ? selected->tick : (ofKind.empty() ? 0 : ofKind.back().tick);

    // Step a bar at a time, in the bars of the meter in force, until the tick is free.
    do
    {
        if (segments.empty())
            return rejected (TempoEditError::NoTimeBase);
        tick = nextBarStartAfter (segments, tick);
    } while (occupied (tick));

    // The value in force at that tick: the last event of the kind at or before it.
    TempoMeterEvent inForce;
    inForce.bpm = 120.0;
    inForce.numerator = 4;
    inForce.denominator = 4;
    for (const auto& e : ofKind)
        if (e.tick <= tick)
            inForce = e;

    return fromTempoEdit (kind == TempoMeterKind::tempo
                              ? setTempo (doc, tick, inForce.bpm)
                              : setMeter (doc, tick, inForce.numerator, inForce.denominator));
}

TableEditResult removeEvent (SongDocument& doc, const TempoMeterEvent& row)
{
    if (! findCurrent (doc, row))
        return rejected (TempoEditError::NoSuchEvent);
    return fromTempoEdit (row.kind == TempoMeterKind::tempo ? removeTempo (doc, row.tick) : removeMeter (doc, row.tick));
}

} // namespace lotro

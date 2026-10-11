#include "UI/TempoEdit.h"

#include "UI/MeterSegments.h"
#include "UI/SongDocument.h"
#include "UI/TempoMapSync.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <tuple>

namespace lotro
{

namespace
{
    constexpr long long maxMicroseconds = 16777215;   // 24 bits

    enum class Kind { tempo, meter };

    using Bytes = std::vector<std::uint8_t>;

    const Bytes defaultTempoBytes { 0xFF, 0x51, 0x07, 0xA1, 0x20 };         // 500000 us = 120 BPM
    const Bytes defaultMeterBytes { 0xFF, 0x58, 0x04, 0x02, 0x18, 0x08 };   // 4/4, 24 clocks, 8 32nds

    Bytes bytesOf (const juce::ValueTree& event)
    {
        if (const auto* block = event.getProperty (SongIDs::data).getBinaryData())
        {
            const auto* d = static_cast<const std::uint8_t*> (block->getData());
            return Bytes (d, d + block->getSize());
        }
        return {};
    }

    bool isKind (const juce::ValueTree& event, Kind kind)
    {
        const auto b = bytesOf (event);
        return b.size() >= 2 && b[0] == 0xFF && b[1] == (kind == Kind::tempo ? 0x51 : 0x58);
    }

    int tickOf (const juce::ValueTree& event) { return (int) event.getProperty (SongIDs::tick, 0); }

    juce::var toVar (const Bytes& bytes) { return juce::var (juce::MemoryBlock (bytes.data(), bytes.size())); }

    // The conductor EVENT of `kind` at `tick` that the maps use: the last one in
    // export order (relocatedFrom, order), as conductorEventsOf sorts. Invalid if none.
    juce::ValueTree findEvent (const SongDocument& doc, Kind kind, int tick)
    {
        juce::ValueTree found;
        std::tuple<int, int> foundKey { 0, 0 };
        for (auto event : SongDocument::getEventsNode (doc.getConductorTrack()))
        {
            if (tickOf (event) != tick || ! isKind (event, kind))
                continue;
            const std::tuple<int, int> key { (int) event.getProperty (SongIDs::relocatedFrom, -1),
                                             (int) event.getProperty (SongIDs::order, 0) };
            if (! found.isValid() || key >= foundKey)
            {
                found = event;
                foundKey = key;
            }
        }
        return found;
    }

    int nextOrderAt (const SongDocument& doc, int tick)
    {
        int order = -1;
        for (auto event : SongDocument::getEventsNode (doc.getConductorTrack()))
            if (tickOf (event) == tick)
                order = std::max (order, (int) event.getProperty (SongIDs::order, 0));
        return order + 1;
    }

    std::optional<Bytes> tempoBytes (double bpm)
    {
        if (! (bpm >= minBpm() && bpm <= maxBpm))   // also rejects NaN
            return std::nullopt;
        const long long us = std::llround (60000000.0 / bpm);
        if (us < 1 || us > maxMicroseconds)
            return std::nullopt;
        return Bytes { 0xFF, 0x51, (std::uint8_t) (us >> 16), (std::uint8_t) (us >> 8), (std::uint8_t) us };
    }

    std::optional<int> denominatorPower (int denominator)
    {
        for (int power = 0; power <= 5; ++power)
            if ((1 << power) == denominator)
                return power;
        return std::nullopt;
    }

    bool meterValid (int numerator, int denominator)
    {
        return numerator >= 1 && numerator <= 32 && denominatorPower (denominator).has_value();
    }

    // FF 58 nn dd cc bb, keeping `previous`'s cc/bb when it has them (24 / 8 otherwise).
    Bytes meterBytes (int numerator, int denominator, const Bytes& previous)
    {
        const bool keep = previous.size() >= 6;
        return { 0xFF, 0x58, (std::uint8_t) numerator, (std::uint8_t) *denominatorPower (denominator),
                 keep ? previous[4] : (std::uint8_t) 24, keep ? previous[5] : (std::uint8_t) 8 };
    }

    int ticksPerQuarterOf (const SongDocument& doc)
    {
        return (int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter, 480);
    }

    // The bar start nearest `tick`, over the meter changes other than the one at `excludeTick`.
    int snapToBar (const SongDocument& doc, int tick, std::optional<int> excludeTick)
    {
        auto changes = doc.getMeterChanges();
        if (excludeTick)
            changes.erase (std::remove_if (changes.begin(), changes.end(),
                                           [&] (const MeterChange& c) { return c.tick == *excludeTick; }),
                           changes.end());
        return (int) std::llround (nearestBarStart (meterSegments (std::move (changes), ticksPerQuarterOf (doc)), (double) tick));
    }

    // Writes `bytes` at `tick` as one undo transaction: into `existing` when valid
    // (taking along the same-kind duplicates it shadowed on its old tick, when it
    // moves), otherwise as a new event ordered after those already on `tick`.
    void writeEvent (SongDocument& doc, Kind kind, juce::ValueTree existing, int tick, const Bytes& bytes)
    {
        doc.getUndoManager().beginNewTransaction();
        auto events = SongDocument::getEventsNode (doc.getConductorTrack());

        const auto addEvent = [&] (int at, const Bytes& data)
        {
            juce::ValueTree event (SongIDs::EVENT);
            event.setProperty (SongIDs::tick, at, nullptr);
            event.setProperty (SongIDs::order, nextOrderAt (doc, at), nullptr);
            event.setProperty (SongIDs::data, toVar (data), nullptr);
            doc.addChild (events, event, false);
        };

        if (! existing.isValid())
        {
            // Spec section 1, Defaults: the first edit writes the implicit default as an
            // explicit event at tick 0, so the maps, the meter segments and the export agree.
            bool kindPresent = false;
            for (auto e : events)
                kindPresent = kindPresent || isKind (e, kind);
            if (! kindPresent && tick > 0)
                addEvent (0, kind == Kind::tempo ? defaultTempoBytes : defaultMeterBytes);
            addEvent (tick, bytes);
            return;
        }

        const int fromTick = tickOf (existing);
        if (fromTick != tick)
        {
            for (int i = events.getNumChildren() - 1; i >= 0; --i)
            {
                auto other = events.getChild (i);
                if (other != existing && tickOf (other) == fromTick && isKind (other, kind))
                    doc.removeChild (events, other, false);
            }
            doc.setProperty (existing, SongIDs::order, nextOrderAt (doc, tick), false);
            doc.setProperty (existing, SongIDs::tick, tick, false);
        }
        doc.setProperty (existing, SongIDs::data, toVar (bytes), false);
    }

    TempoEditResult remove (SongDocument& doc, Kind kind, int tick)
    {
        if (! doc.hasTimeBase())
            return TempoEditError::NoTimeBase;
        if (tick == 0)
            return TempoEditError::CannotRemoveFirst;
        if (! findEvent (doc, kind, tick).isValid())
            return TempoEditError::NoSuchEvent;

        doc.getUndoManager().beginNewTransaction();
        auto events = SongDocument::getEventsNode (doc.getConductorTrack());
        for (int i = events.getNumChildren() - 1; i >= 0; --i)   // every event of that kind on the tick
        {
            auto event = events.getChild (i);
            if (tickOf (event) == tick && isKind (event, kind))
                doc.removeChild (events, event, false);
        }
        return std::nullopt;
    }
}

double minBpm() { return 60000000.0 / (double) maxMicroseconds; }

double roundedBpm (double bpm)
{
    return bpmFromMicroseconds ((std::uint32_t) std::llround (60000000.0 / bpm));
}

TempoEditResult setTempo (SongDocument& doc, int tick, double bpm)
{
    if (! doc.hasTimeBase())
        return TempoEditError::NoTimeBase;
    if (tick < 0)
        return TempoEditError::NegativeTick;
    const auto bytes = tempoBytes (bpm);
    if (! bytes)
        return TempoEditError::BpmOutOfRange;

    writeEvent (doc, Kind::tempo, findEvent (doc, Kind::tempo, tick), tick, *bytes);
    return std::nullopt;
}

TempoEditResult moveTempo (SongDocument& doc, int fromTick, int toTick, double bpm)
{
    if (! doc.hasTimeBase())
        return TempoEditError::NoTimeBase;
    if (toTick < 0)
        return TempoEditError::NegativeTick;
    const auto bytes = tempoBytes (bpm);
    if (! bytes)
        return TempoEditError::BpmOutOfRange;
    const auto event = findEvent (doc, Kind::tempo, fromTick);
    if (! event.isValid())
        return TempoEditError::NoSuchEvent;

    const int target = fromTick == 0 ? 0 : toTick;   // the tick-0 tempo can change value, not position
    if (target != fromTick && findEvent (doc, Kind::tempo, target).isValid())
        return TempoEditError::TickOccupied;

    writeEvent (doc, Kind::tempo, event, target, *bytes);
    return std::nullopt;
}

TempoEditResult removeTempo (SongDocument& doc, int tick) { return remove (doc, Kind::tempo, tick); }

TempoEditResult setMeter (SongDocument& doc, int tick, int numerator, int denominator)
{
    if (! doc.hasTimeBase())
        return TempoEditError::NoTimeBase;
    if (tick < 0)
        return TempoEditError::NegativeTick;
    if (! meterValid (numerator, denominator))
        return TempoEditError::MeterOutOfRange;

    const int snapped = snapToBar (doc, tick, std::nullopt);
    const auto existing = findEvent (doc, Kind::meter, snapped);
    writeEvent (doc, Kind::meter, existing, snapped, meterBytes (numerator, denominator, bytesOf (existing)));
    return std::nullopt;
}

TempoEditResult moveMeter (SongDocument& doc, int fromTick, int toTick, int numerator, int denominator)
{
    if (! doc.hasTimeBase())
        return TempoEditError::NoTimeBase;
    if (toTick < 0)
        return TempoEditError::NegativeTick;
    if (! meterValid (numerator, denominator))
        return TempoEditError::MeterOutOfRange;
    const auto event = findEvent (doc, Kind::meter, fromTick);
    if (! event.isValid())
        return TempoEditError::NoSuchEvent;

    // The tick-0 meter can change value, not position; any other snaps with the
    // meters in force without it (spec: "computed with the meters in force before it").
    const int snapped = fromTick == 0 ? 0 : snapToBar (doc, toTick, fromTick);
    if (snapped != fromTick && findEvent (doc, Kind::meter, snapped).isValid())
        return TempoEditError::TickOccupied;

    writeEvent (doc, Kind::meter, event, snapped, meterBytes (numerator, denominator, bytesOf (event)));
    return std::nullopt;
}

TempoEditResult removeMeter (SongDocument& doc, int tick) { return remove (doc, Kind::meter, tick); }

std::vector<TempoMeterEvent> listTempoMeterEvents (const SongDocument& doc)
{
    std::vector<TempoMeterEvent> out;
    for (auto change : doc.getTempoMapNode())
        out.push_back ({ TempoMeterKind::tempo, (int) change.getProperty (SongIDs::tick), (double) change.getProperty (SongIDs::bpm), 0, 0 });
    for (auto change : doc.getMeterMapNode())
        out.push_back ({ TempoMeterKind::meter, (int) change.getProperty (SongIDs::tick), 0.0,
                         (int) change.getProperty (SongIDs::numerator), (int) change.getProperty (SongIDs::denominator) });
    std::stable_sort (out.begin(), out.end(), [] (const TempoMeterEvent& a, const TempoMeterEvent& b)
                      { return a.tick != b.tick ? a.tick < b.tick : a.kind < b.kind; });
    return out;
}

} // namespace lotro

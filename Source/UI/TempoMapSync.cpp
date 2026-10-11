#include "UI/TempoMapSync.h"

#include "UI/SongDocument.h"

#include <algorithm>
#include <tuple>

namespace lotro
{

double bpmFromMicroseconds (std::uint32_t microsecondsPerQuarter)
{
    return 60.0 / ((double) microsecondsPerQuarter / 1000000.0);
}

DerivedMaps deriveMaps (const std::vector<RawMidiEvent>& events)
{
    DerivedMaps maps;
    for (const auto& event : events)
    {
        const auto& b = event.bytes;
        if (b.size() >= 5 && b[0] == 0xFF && b[1] == 0x51)
        {
            const std::uint32_t us = ((std::uint32_t) b[2] << 16) | ((std::uint32_t) b[3] << 8) | (std::uint32_t) b[4];
            if (us > 0)
                maps.tempo.push_back ({ event.tick, bpmFromMicroseconds (us) });
        }
        // dd > 15 is ignored: intentionally stricter than the importer (1 << dd would overflow).
        else if (b.size() >= 4 && b[0] == 0xFF && b[1] == 0x58 && b[3] <= 15)
        {
            maps.meter.push_back ({ event.tick, (int) b[2], 1 << b[3] });
        }
    }
    if (maps.tempo.empty())
        maps.tempo.push_back ({ 0, TempoMap::defaultBpm });
    if (maps.meter.empty())
        maps.meter.push_back ({});
    return maps;
}

namespace
{
    std::vector<std::uint8_t> bytesOf (const juce::ValueTree& event)
    {
        if (const auto* block = event.getProperty (SongIDs::data).getBinaryData())
        {
            const auto* d = static_cast<const std::uint8_t*> (block->getData());
            return std::vector<std::uint8_t> (d, d + block->getSize());
        }
        return {};
    }

    bool isTempoOrMeterBytes (const std::vector<std::uint8_t>& b)
    {
        return b.size() >= 2 && b[0] == 0xFF && (b[1] == 0x51 || b[1] == 0x58);
    }

    void writeIfDifferent (juce::ValueTree node, const juce::Identifier& childType,
                           const std::vector<std::vector<std::pair<juce::Identifier, juce::var>>>& wanted)
    {
        bool same = node.getNumChildren() == (int) wanted.size();
        for (int i = 0; same && i < node.getNumChildren(); ++i)
            for (const auto& [name, value] : wanted[(size_t) i])
                if (node.getChild (i).getProperty (name) != value)
                    same = false;
        if (same)
            return;

        node.removeAllChildren (nullptr);
        for (const auto& props : wanted)
        {
            juce::ValueTree child (childType);
            for (const auto& [name, value] : props)
                child.setProperty (name, value, nullptr);
            node.addChild (child, -1, nullptr);
        }
    }
}

std::vector<RawMidiEvent> conductorEventsOf (const SongDocument& doc)
{
    // The same key buildRawMidiFile sorts a track's events by (EVENTs have no note group),
    // so the maps read same-tick events in the order the export writes them.
    struct Item { int tick; int relocatedFrom; int order; std::vector<std::uint8_t> bytes; };
    std::vector<Item> items;
    for (auto event : SongDocument::getEventsNode (doc.getConductorTrack()))
        items.push_back ({ (int) event.getProperty (SongIDs::tick, 0), (int) event.getProperty (SongIDs::relocatedFrom, -1),
                           (int) event.getProperty (SongIDs::order, 0), bytesOf (event) });
    std::stable_sort (items.begin(), items.end(), [] (const Item& a, const Item& b)
                      { return std::tie (a.tick, a.relocatedFrom, a.order) < std::tie (b.tick, b.relocatedFrom, b.order); });

    std::vector<RawMidiEvent> out;
    out.reserve (items.size());
    for (auto& item : items)
        out.push_back ({ item.tick, std::move (item.bytes) });
    return out;
}

void rebuildMaps (SongDocument& doc)
{
    auto tempoNode = doc.getTempoMapNode();
    auto meterNode = doc.getMeterMapNode();

    if (! doc.hasTimeBase())
    {
        tempoNode.removeAllChildren (nullptr);
        meterNode.removeAllChildren (nullptr);
        return;
    }

    const auto maps = deriveMaps (conductorEventsOf (doc));

    std::vector<std::vector<std::pair<juce::Identifier, juce::var>>> tempo, meter;
    for (const auto& t : maps.tempo)
        tempo.push_back ({ { SongIDs::tick, t.tick }, { SongIDs::bpm, t.bpm } });
    for (const auto& m : maps.meter)
        meter.push_back ({ { SongIDs::tick, m.tick }, { SongIDs::numerator, m.numerator }, { SongIDs::denominator, m.denominator } });

    writeIfDifferent (tempoNode, SongIDs::TEMPO_CHANGE, tempo);
    writeIfDifferent (meterNode, SongIDs::METER_CHANGE, meter);
}

void moveTempoAndMeterToConductor (SongDocument& doc)
{
    auto conductorEvents = SongDocument::getEventsNode (doc.getConductorTrack());
    const auto nextOrderAt = [&] (int tick)
    {
        int next = 0;
        for (auto e : conductorEvents)
            if ((int) e.getProperty (SongIDs::tick, 0) == tick)
                next = std::max (next, (int) e.getProperty (SongIDs::order, 0) + 1);
        return next;
    };

    for (auto track : doc.getSourceMidiNode())
    {
        if ((bool) track.getProperty (SongIDs::isConductor, false))
            continue;
        auto events = SongDocument::getEventsNode (track);
        for (int i = 0; i < events.getNumChildren();)
        {
            auto event = events.getChild (i);
            if (! isTempoOrMeterBytes (bytesOf (event)))
            {
                ++i;
                continue;
            }
            events.removeChild (i, nullptr);
            event.setProperty (SongIDs::order, nextOrderAt ((int) event.getProperty (SongIDs::tick, 0)), nullptr);
            conductorEvents.addChild (event, -1, nullptr);
        }
    }
}

TempoMapSync::TempoMapSync (SongDocument& document) : doc (document), sourceMidi (document.getSourceMidiNode())
{
    sourceMidi.addListener (this);
}

TempoMapSync::~TempoMapSync() { sourceMidi.removeListener (this); }

bool TempoMapSync::isConductorEvent (const juce::ValueTree& node)
{
    if (! node.hasType (SongIDs::EVENT))
        return false;
    const auto events = node.getParent();
    return events.isValid() && events.hasType (SongIDs::EVENTS)
        && (bool) events.getParent().getProperty (SongIDs::isConductor, false);
}

bool TempoMapSync::concernsTempoOrMeter (const juce::ValueTree& changed) const
{
    // Only a conductor EVENT holding FF 51 / FF 58 counts. Whole tracks or EVENTS nodes
    // coming or going are not watched: the conductor is never removed, and bulk
    // replacements (import, load) run under a MapSyncPause that rebuilds at its end.
    return isConductorEvent (changed) && isTempoOrMeterBytes (bytesOf (changed));
}

void TempoMapSync::maybeRebuild (const juce::ValueTree& changed)
{
    if (pauseDepth == 0 && concernsTempoOrMeter (changed))
        rebuildMaps (doc);
}

void TempoMapSync::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    // A conductor event's bytes may have just stopped being a tempo or meter (or, on
    // undo, started again): any data change rebuilds; writeIfDifferent keeps it cheap.
    if (pauseDepth == 0 && property == SongIDs::data && isConductorEvent (tree))
        rebuildMaps (doc);
    else
        maybeRebuild (tree);
}
void TempoMapSync::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child) { maybeRebuild (child); }

void TempoMapSync::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    // The removed child no longer has a parent: judge it by its own bytes and the node it left.
    if (pauseDepth != 0 || ! child.hasType (SongIDs::EVENT) || ! parent.hasType (SongIDs::EVENTS)
        || ! (bool) parent.getParent().getProperty (SongIDs::isConductor, false))
        return;
    if (isTempoOrMeterBytes (bytesOf (child)))
        rebuildMaps (doc);
}

MapSyncPause::MapSyncPause (SongDocument& document) : doc (document) { doc.getTempoMapSync().pause(); }

MapSyncPause::~MapSyncPause()
{
    if (doc.getTempoMapSync().resume())
        rebuildMaps (doc);
}

} // namespace lotro

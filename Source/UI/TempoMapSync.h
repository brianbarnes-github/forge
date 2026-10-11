#pragma once

// Tempo and meter live in the conductor track's FF 51 / FF 58 EVENTs and nowhere
// else; the TEMPO_MAP / METER_MAP nodes are a view derived from them (spec
// 2026-10-10-songsmith-tempo-meter-design.md, section 1). This header holds the
// pure derivation and the document listener that keeps the map nodes in step.

#include "UI/MeterSegments.h"
#include "UI/Playback/TempoMap.h"
#include "UI/RawMidi.h"

#include <juce_data_structures/juce_data_structures.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace lotro
{

class SongDocument;

struct DerivedMaps
{
    std::vector<TempoPoint>  tempo;
    std::vector<MeterChange> meter;
};

// 60 / (microsecondsPerQuarter / 1e6), written exactly as the importer does.
double bpmFromMicroseconds (std::uint32_t microsecondsPerQuarter);

// `events` are in (tick, order) order. Defaults {0, 120 BPM} / {0, 4/4} when no
// event of that kind exists. Mirrors importTempoAndMeter (Source/Core/MidiImporter.cpp).
DerivedMaps deriveMaps (const std::vector<RawMidiEvent>& events);

// The conductor's EVENTs sorted by (tick, order), as raw events.
std::vector<RawMidiEvent> conductorEventsOf (const SongDocument& doc);

// Writes the derived maps into TEMPO_MAP / METER_MAP (never undoable; only when
// they differ). With no time base yet both nodes are cleared.
void rebuildMaps (SongDocument& doc);

// Moves every FF 51 / FF 58 EVENT out of the note tracks into the conductor
// (non-undoable; tick kept, `order` after the conductor's events at that tick).
// For loading files saved before tempo and meter always lived in the conductor.
void moveTempoAndMeterToConductor (SongDocument& doc);

// Rebuilds the maps whenever a conductor FF 51 / FF 58 event is added, removed or
// changed (and on any change of a conductor event's data). Attached to SOURCE_MIDI
// (whose node object survives replaceContents). Listeners on deeper nodes run before
// this one, so they may read stale maps from inside their own callbacks.
class TempoMapSync : public juce::ValueTree::Listener
{
public:
    explicit TempoMapSync (SongDocument& document);
    ~TempoMapSync() override;

    void pause() noexcept { ++pauseDepth; }
    // Returns true when this call ended the outermost pause (the caller rebuilds).
    bool resume() noexcept { return --pauseDepth == 0; }

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;

private:
    static bool isConductorEvent (const juce::ValueTree& node);
    bool concernsTempoOrMeter (const juce::ValueTree& changed) const;
    void maybeRebuild (const juce::ValueTree& changed);

    SongDocument& doc;
    juce::ValueTree sourceMidi;
    int pauseDepth = 0;
};

// Holds map rebuilds back for bulk writes (import, load); the outermost pause
// rebuilds once when it ends.
class MapSyncPause
{
public:
    explicit MapSyncPause (SongDocument& document);
    ~MapSyncPause();
    MapSyncPause (const MapSyncPause&) = delete;
    MapSyncPause& operator= (const MapSyncPause&) = delete;

private:
    SongDocument& doc;
};

} // namespace lotro

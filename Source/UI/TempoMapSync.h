#pragma once

// Tempo and meter live in the conductor track's FF 51 / FF 58 EVENTs and nowhere
// else; the TEMPO_MAP / METER_MAP nodes are a view derived from them (spec
// 2026-10-10-songsmith-tempo-meter-design.md, section 1). This header holds the
// pure derivation; the document listener is added in a later task.

#include "UI/MeterSegments.h"
#include "UI/Playback/TempoMap.h"
#include "UI/RawMidi.h"

#include <cstdint>
#include <vector>

namespace lotro
{

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

} // namespace lotro

#pragma once

#include "UI/SongDocument.h"
#include "UI/TimelineViewState.h"

#include <juce_data_structures/juce_data_structures.h>

#include <vector>

// A track's instrument (Program Change) changes (spec:
// docs/superpowers/specs/2026-10-10-songsmith-instrument-changes-design.md).
// Pure document logic: no UI painting, no Source/Core. Never mutates.
namespace lotro
{

struct ProgramChange
{
    int tick = 0;
    int channel = 1;   // 1..16
    int program = 0;   // 0..127
    juce::ValueTree event;   // the EVENT node
};

// Every `Cn pp` event of the track, by tick then raw order. The conductor has none.
std::vector<ProgramChange> programChangesOf (const juce::ValueTree& track);

struct InstrumentSegment
{
    int startTick = 0;
    int endTick = 0;   // half-open
    int program = 0;
};

// One segment per distinct instrument. The span before a first change that is
// after tick 0 (and a track with no change at all) uses `sourceProgram`.
// Consecutive changes to the same program merge; two changes on one tick keep the
// later. The last segment ends at the track's endTick / last note end (at least
// one tick past its start). Empty for the conductor.
std::vector<InstrumentSegment> instrumentSegmentsOf (const juce::ValueTree& track);

struct BandSegment
{
    int x0 = 0;   // preview-local pixels
    int x1 = 0;
    int program = 0;
};

// The segments in pixels for the current zoom/scroll; each at least 1 px wide.
std::vector<BandSegment> bandSegments (const std::vector<InstrumentSegment>& segments, const TimelineViewState& view);

} // namespace lotro

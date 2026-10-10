#pragma once

#include "UI/SongDocument.h"

// The two instrument-band menu actions (spec:
// docs/superpowers/specs/2026-10-10-songsmith-instrument-changes-design.md).
// Pure document logic: no UI, no Source/Core. Each call is one undo transaction;
// a call that would change nothing touches nothing and opens none.
namespace lotro
{

// True when the track has at least two instrument segments.
bool canAutoSplit (const juce::ValueTree& track);

// Splits the track at every instrument change after tick 0.
void autoSplitOnInstrumentChange (SongDocument& doc, juce::int64 trackId);

// Makes the whole track one instrument: the first program change on each channel is
// set to `program`, the later ones are deleted, a track with none gets one at tick 0
// on its defaultChannel, and sourceProgram follows. The conductor and unknown
// tracks are ignored.
void setTrackInstrument (SongDocument& doc, juce::int64 trackId, int program);

} // namespace lotro

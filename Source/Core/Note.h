#pragma once

namespace lotro
{

struct Note
{
    int  pitch         = 0;
    int  startTick     = 0;
    int  durationTicks = 0;
    int  velocity      = 0;
    bool isDrum        = false;

    // Provenance — populated by MidiImporter and preserved by the pipeline.
    // An editor can use these to jump back to the exact source event when a
    // Diagnostic mentions a specific note. -1 means "unknown" (e.g. a Note
    // synthesised in a test that didn't go through MidiImporter).
    int sourceTrackIndex = -1;  // MIDI-file track index (0-based, includes empty tracks)
    int sourceEventIndex = -1;  // ordinal of the note-on event within that source track
};

} // namespace lotro

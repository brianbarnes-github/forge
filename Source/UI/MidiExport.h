#pragma once

// Builds the song's source MIDI back into a RawMidiFile (2026-10-03 spec,
// "Export"). Format 1, conductor first, then every other MIDI_TRACK in
// document order. Unedited imports come back event-for-event; new or
// re-timed notes sort around a tick's original events (new note-offs before
// them, new note-ons after).
// LOTRO parts/assignments never affect it.

#include "RawMidi.h"
#include "SongDocument.h"

namespace lotro
{

RawMidiFile buildRawMidiFile (const SongDocument& doc);

// One track on its own (the MIDI editor's File > Export to New MIDI File): format 1,
// the conductor then that track, each exactly as the whole-song export writes it.
// Throws MidiExportError for the conductor's id or an id with no track.
RawMidiFile buildRawMidiFile (const SongDocument& doc, juce::int64 trackId);

} // namespace lotro

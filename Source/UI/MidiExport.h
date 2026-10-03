#pragma once

// Builds the song's source MIDI back into a RawMidiFile (2026-10-03 spec,
// "Export"). Format 1, conductor first, then every other MIDI_TRACK in
// document order. Unedited imports come back event-for-event; new or
// re-timed notes sort after a tick's original events (note-offs first).
// LOTRO parts/assignments never affect it.

#include "RawMidi.h"
#include "SongDocument.h"

namespace lotro
{

RawMidiFile buildRawMidiFile (const SongDocument& doc);

} // namespace lotro

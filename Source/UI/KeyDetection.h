#pragma once

// Key detection over a Song document: gathers every MIDI_TRACK's NOTE nodes
// for KeyDetector. Songsmith runs it once per MIDI import (see
// MainWindow::importMidiWithOptions).

#include "KeyDetector.h"

#include <juce_data_structures/juce_data_structures.h>

#include <vector>

namespace lotro
{

class SongDocument;

// Every NOTE under SOURCE_MIDI's MIDI_TRACKs (drums included, flagged).
std::vector<KeyNote> collectKeyNotes (const juce::ValueTree& songRoot);

// formatKeyText of the document's notes: "key: G major", or empty when
// there are no pitched notes to judge.
juce::String detectedKeyText (const SongDocument& doc, KeyProfileSet set = KeyProfileSet::krumhanslKessler);

} // namespace lotro

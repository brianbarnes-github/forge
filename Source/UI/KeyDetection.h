#pragma once

// Key detection over a Song document: gathers every MIDI_TRACK's NOTE nodes
// for KeyDetector. Songsmith runs it after each MIDI import and on
// Song ▸ Set Key ▸ Detect Key (MainWindow::detectSongKey).

#include "KeyDetector.h"

#include <juce_data_structures/juce_data_structures.h>

#include <optional>
#include <vector>

namespace lotro
{

class SongDocument;

// Every NOTE under SOURCE_MIDI's MIDI_TRACKs (drums included, flagged).
std::vector<KeyNote> collectKeyNotes (const juce::ValueTree& songRoot);

// The keyIndex of the document's detected key; nullopt when there is nothing to
// judge (no pitched notes, or a flat histogram).
std::optional<int> detectedKeyIndex (const SongDocument& doc, KeyProfileSet set = KeyProfileSet::krumhanslKessler);

} // namespace lotro

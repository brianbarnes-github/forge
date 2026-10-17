#pragma once

#include "UI/SongDocument.h"

#include <vector>

// The MIDI editor's Edit > Split Notes / Join Notes / Legato on a track's
// selected notes. Pure document logic: no UI, no Source/Core. Each edit is one
// undo transaction, and one that would change nothing opens none; the can*
// checks answer exactly that question, for the menu's enabled state. Every
// note whose start or length changes loses its recorded event order
// (markNoteTimingEdited), as a drag does.
namespace lotro
{

// Some note of `notes` has `tick` strictly inside it (start < tick < end).
bool canSplitNotes (const std::vector<juce::ValueTree>& notes, int tick);

// Cuts each such note in two at `tick`: the note keeps its head, and a copy (the
// same pitch, velocity, channel, section and provenance) becomes the tail. Returns
// the new selection: `notes` plus the tails. Nothing to split returns no notes.
std::vector<juce::ValueTree> splitNotes (SongDocument& doc, juce::ValueTree track,
                                         const std::vector<juce::ValueTree>& notes, int tick);

// Two or more of `notes` share a pitch.
bool canJoinNotes (const std::vector<juce::ValueTree>& notes);

// Merges the notes of each shared pitch into the earliest-starting one, which
// then runs to the latest end among them (gaps filled) and keeps its own
// velocity; the others are removed. Returns `notes` without the removed ones.
std::vector<juce::ValueTree> joinNotes (SongDocument& doc, juce::ValueTree track,
                                        const std::vector<juce::ValueTree>& notes);

// Some note of `notes` would change length under legatoNotes.
bool canLegatoNotes (const juce::ValueTree& track, const std::vector<juce::ValueTree>& notes);

// Ends each of `notes` where the next note on `track` starts -- the earliest
// start strictly after its own, on any pitch, selected or not -- lengthening or
// trimming it. A note with no later note keeps its length. False when nothing changed.
bool legatoNotes (SongDocument& doc, juce::ValueTree track, const std::vector<juce::ValueTree>& notes);

} // namespace lotro

#pragma once

// An exact replica of what juce::MidiFile::readFrom (stream, true) -- and so
// importMidi -- makes of one track's note-ons, computed on RawMidi events so
// every note can be tied back to the exact raw bytes it came from.
//
// JUCE does two things per track (juce_MidiFile.cpp / juce_MidiMessageSequence.cpp):
//  1. reorderNoteOnsAfterNoteOffs, per group of events sharing a tick: find
//     the FIRST note-on, find the LAST same-channel/key note-off after it in
//     the group, swap them, continue after the first note-on's slot; stop the
//     group as soon as a first note-on has no such off. This can move a
//     note-on past other note-ons.
//  2. updateMatchedPairs: each note-on pairs with the next same-channel/key
//     note-off, or -- if a same-channel/key note-on comes first -- with a
//     note-off JUCE INSERTS into the list just before that re-strike.
// Predicates match JUCE's: note-on = 0x9n with velocity > 0; note-off = 0x8n
// or 0x9n with velocity 0.

#include "RawMidi.h"

#include <vector>

namespace lotro
{

struct ReplicaNoteOn
{
    int  onRawIndex     = -1;
    int  onTick         = 0;
    int  channel        = 1;
    int  pitch          = 0;
    int  velocity       = 0;
    bool hasOff         = false;
    bool offSynthesized = false;
    int  offRawIndex    = -1;
    int  offTick        = -1;
};

// result[k] is JUCE's k-th note-on (importMidi's Note::sourceEventIndex == k).
std::vector<ReplicaNoteOn> replicateJuceNoteOns (const RawMidiTrack& track);

} // namespace lotro

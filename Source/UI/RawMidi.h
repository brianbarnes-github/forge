#pragma once

// Lossless Standard MIDI File reader/writer for Songsmith's MIDI-fidelity
// path (docs/superpowers/specs/2026-10-03-songsmith-midi-fidelity-design.md).
// JUCE-free on purpose: juce::MidiFile reorders note events within a tick
// and invents note-offs, so it can't give an exact copy. Track structure
// deliberately matches juce::MidiFile::readFrom (RIFF wrapper, non-MTrk
// chunks consuming a track slot, meta/SysEx not cancelling running status)
// so track indices line up with importMidi's sourceTrackIndex.

#include "Core/MidiImporter.h" // MidiImportError

#include <cstdint>
#include <iosfwd>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace lotro
{

// One event at an absolute tick. bytes layout:
//   channel message: explicit status byte + data bytes (running status resolved)
//   meta event:      FF <type> <data>        (length not stored)
//   SysEx:           F0 <data> or F7 <data>  (length not stored)
struct RawMidiEvent
{
    int                       tick = 0;
    std::vector<std::uint8_t> bytes;

    bool operator== (const RawMidiEvent& other) const;
    bool operator!= (const RawMidiEvent& other) const { return ! (*this == other); }
};

struct RawMidiTrack
{
    std::vector<RawMidiEvent> events;  // file order; End-of-Track is never stored here
    int                       endTick = 0; // first End-of-Track's tick, else the last event's tick

    bool operator== (const RawMidiTrack& other) const;
    bool operator!= (const RawMidiTrack& other) const { return ! (*this == other); }
};

struct RawMidiFile
{
    int                       format          = 1;
    int                       ticksPerQuarter = 480;
    std::vector<RawMidiTrack> tracks;

    bool operator== (const RawMidiFile& other) const;
    bool operator!= (const RawMidiFile& other) const { return ! (*this == other); }
};

class MidiExportError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

// Both throw MidiImportError on malformed input or SMPTE time division.
RawMidiFile readMidiFile  (std::istream& input, std::string_view sourceName);
RawMidiFile readMidiBytes (const std::vector<std::uint8_t>& bytes, std::string_view sourceName);

// Writes every status byte explicitly (no running-status compression) and one
// End-of-Track per track at max(endTick, last event tick). Both throw
// MidiExportError on an empty event, events out of tick order, or (for
// writeMidiFile) a failed stream.
std::vector<std::uint8_t> writeMidiBytes (const RawMidiFile& file);
void                      writeMidiFile  (const RawMidiFile& file, std::ostream& output);

} // namespace lotro

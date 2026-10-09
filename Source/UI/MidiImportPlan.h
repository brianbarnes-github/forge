#pragma once

// Pure import planning for the MIDI-fidelity path (2026-10-03 spec, "Conductor
// rules" and "Import"). Given importMidi's Song and the lossless RawMidiFile
// of the same bytes, decides what goes into the song's conductor, which raw
// events each Song note is tied to, and which raw events become EVENT nodes.
// Never touches a SongDocument.

#include "Core/Diagnostics.h"
#include "Core/Song.h"
#include "RawMidi.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace lotro
{

class MidiImportPlanError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

enum class TempoMode { keep, replace };
enum class TrackMode { expanded, merged };

// What Import > MIDI does beyond the default. The defaults are today's behaviour.
struct ImportOptions
{
    TempoMode tempo  = TempoMode::keep;     // replace: the file's conductor replaces the Song's
    TrackMode tracks = TrackMode::expanded; // merged: all the file's note tracks become one track
};

struct PlannedEvent
{
    int                       tick          = 0;
    int                       order         = 0;  // index within its raw source track
    int                       relocatedFrom = -1; // raw track a conductor event was moved from; -1 = not moved
    std::vector<std::uint8_t> bytes;
};

struct PlannedNoteLink
{
    int  channel         = 1;
    int  onOrder         = -1;
    int  offOrder        = -1;
    bool offSynthesized  = false;
    int  offVelocity     = 64;
    bool offIsNoteOnZero = false;
};

struct PlannedTrack
{
    int                          rawTrackIndex     = 0;
    int                          songTrackIndex    = -1; // index into Song::tracks; -1 = note-less track
    std::string                  name;                   // note-less tracks only
    int                          sourceMidiChannel = 0;  // note-less tracks only
    int                          sourceProgram     = 0;  // note-less tracks only
    int                          defaultChannel    = 1;
    int                          endTick           = 0;
    std::vector<PlannedNoteLink> noteLinks;              // parallel to Song::tracks[songTrackIndex].notes
    std::vector<PlannedEvent>    events;
};

struct MidiImportPlan
{
    bool                      writesConductor     = false; // first import only
    std::vector<PlannedEvent> conductorEvents;
    int                       conductorEndTick    = 0;
    std::vector<PlannedTrack> tracks;                       // raw order, file's conductor excluded
    int                       relocatedEventCount = 0;
    int                       droppedEventCount   = 0;
};

// Tempo, time signature, key signature, SMPTE offset, marker, copyright.
bool isSongWideMetaEvent (const RawMidiEvent& event);

// Format 1 and the first track has no note-on with velocity > 0.
bool hasConductorTrack (const RawMidiFile& raw);

// Throws MidiImportPlanError for format 2 or when the two parsers disagree.
// Appends Info/Warning diagnostics (source "SongModelBridge") only on success.
MidiImportPlan planMidiImport (const Song& song, const RawMidiFile& raw, bool isFirstImport,
                               Diagnostics& diagnostics, const ImportOptions& options = {});

} // namespace lotro

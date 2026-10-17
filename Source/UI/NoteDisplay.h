#pragma once

namespace lotro
{

// The MIDI editor's View > Piano Roll Notes: how the editor roll draws a note.
// Saved in AppSettings.
enum class NoteShape
{
    rectangle,     // the note's full length as a bar
    diamond,       // a row-sized diamond at the note's start; length not shown
    diamondLength  // the diamond plus a thin line out to the note's end
};

// The MIDI editor's View > Piano Roll Notes checkboxes: the text drawn on each
// note of the edited track. Saved in AppSettings.
struct NoteLabels
{
    bool names = false;      // Show Note Names on Notes ("C4")
    bool velocities = false; // Show Velocity Numbers on Notes ("96")

    bool operator== (const NoteLabels& other) const noexcept
    {
        return names == other.names && velocities == other.velocities;
    }
    bool operator!= (const NoteLabels& other) const noexcept { return ! (*this == other); }
};

// The MIDI editor's View > Color Note by: what a note's colour shows, in the
// editor roll and the main page's note bars. Saved in AppSettings.
enum class NoteColourMode
{
    track,    // the track's colour
    velocity, // a blue-to-red heat map over 0..127
    channel,  // one colour per MIDI channel 1..16
    pitch     // one colour per pitch class
};

} // namespace lotro

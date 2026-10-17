#pragma once

namespace lotro
{

// The MIDI editor's View > Piano Notes / MIDI Numbers: what the roll's keyboard
// gutter shows. Saved in AppSettings.
enum class KeyboardLabels
{
    pianoNotes,  // piano keys, C labels and the hovered key's name
    midiNumbers  // the roll's row bands with each row's MIDI note number
};

} // namespace lotro

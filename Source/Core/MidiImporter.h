#pragma once

#include "Diagnostics.h"
#include "Song.h"

#include <stdexcept>

namespace juce { class File; }

namespace lotro
{

class MidiImportError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

// Load a MIDI file. For now the file argument is still a juce::File because
// juce::MidiFile is the underlying parser; a future refactor can take
// std::istream instead.
Song importMidi (const juce::File& file, Diagnostics& diagnostics);

} // namespace lotro

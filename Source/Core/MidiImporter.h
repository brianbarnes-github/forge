#pragma once

#include "Diagnostics.h"
#include "Song.h"

#include <iosfwd>
#include <stdexcept>
#include <string_view>

namespace lotro
{

class MidiImportError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

// Load a MIDI file from any byte stream. `sourceName` is used as the Song
// title and in error messages — callers pass the filename (sans extension)
// for file-backed streams, or a display label for in-memory MIDI.
Song importMidi (std::istream& input, std::string_view sourceName, Diagnostics& diagnostics);

} // namespace lotro

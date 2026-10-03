#pragma once

#include "SongDocument.h"
#include "SongFileError.h"

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <cstdint>

// The binary .songsmith container (Song file spec, "File layer").
//
//   offset  size  field
//   0       4     magic "SGSM"
//   4       4     formatVersion (uint32, little-endian; one number covers container + tree schema)
//   8       8     uncompressedLength (uint64)
//   16      4     crc32 of the uncompressed payload
//   20      8     payloadLength (uint64)
//   28      n     payload: gzip of ValueTree::writeToStream(SONG)
namespace lotro
{

constexpr std::uint32_t songFileFormatVersion = 1;

juce::MemoryBlock writeSongBytes (const juce::ValueTree& song);

// Throws SongFileError. Nothing is parsed until the bytes are proven intact.
juce::ValueTree readSongBytes (const juce::MemoryBlock& bytes);

// Builds the whole buffer first, then File::replaceWithData (sibling temp
// file + atomic replace). Returns false on a write failure; the destination
// is then untouched.
bool saveSongFile (const SongDocument& doc, const juce::File& file);

// Throws SongFileError; an unreadable file is NotASongFile ("Could not read ...").
juce::ValueTree loadSongFile (const juce::File& file);

} // namespace lotro

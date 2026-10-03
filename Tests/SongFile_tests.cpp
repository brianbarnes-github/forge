// SongFile: the binary .songsmith container — round trip, and every way a
// file can be wrong is refused with its specific SongFileError kind.

#include "UI/SongDocument.h"
#include "UI/SongFile.h"
#include "UI/SongModelBridge.h"
#include "UI/MidiExport.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <juce_core/juce_core.h>

#include <cstring>
#include <string>

using namespace lotro;

namespace
{
    constexpr std::size_t headerSize = 28;   // magic 4, version 4, uncompressedLength 8, crc 4, payloadLength 8

    void fillSample (SongDocument& doc)
    {
        auto track = doc.addTrack ("Lead", 0xFF112233, 1, doc.mintImportBatch());
        juce::ValueTree note (SongIDs::NOTE);
        note.setProperty (SongIDs::pitch, 60, nullptr);
        note.setProperty (SongIDs::startTick, 0, nullptr);
        note.setProperty (SongIDs::durationTicks, 120, nullptr);
        note.setProperty (SongIDs::velocity, 90, nullptr);
        SongDocument::getNotesNode (track).addChild (note, -1, nullptr);
        juce::ValueTree event (SongIDs::EVENT);
        const std::uint8_t raw[] = { 0xFF, 0x03, 0x01, 'x' };
        event.setProperty (SongIDs::data, juce::var (juce::MemoryBlock (raw, sizeof (raw))), nullptr);
        event.setProperty (SongIDs::order, 0, nullptr);
        SongDocument::getEventsNode (track).addChild (event, -1, nullptr);
        auto part = doc.addPart ("Lute of Ages", "Part 1");
        doc.addAssignment (part, (juce::int64) track.getProperty (SongIDs::trackId), 0, 0, "octaveShift");
        doc.setProperty (doc.getTree(), SongIDs::title, "T");
    }

    // Returns the error kind readSongBytes throws; fails the test if it does not throw.
    SongFileErrorKind kindOf (const juce::MemoryBlock& bytes)
    {
        try { readSongBytes (bytes); }
        catch (const SongFileError& e) { return e.kind(); }
        FAIL ("readSongBytes did not throw");
        return SongFileErrorKind::Corrupt;
    }

    std::uint8_t* at (juce::MemoryBlock& b, std::size_t i) { return static_cast<std::uint8_t*> (b.getData()) + i; }

    // Re-wraps `good` with the byte at `rawIndex` of the UNCOMPRESSED payload
    // changed, recompressed into a perfectly valid gzip stream, header
    // (lengths, crc32) left as it was.
    juce::MemoryBlock withPayloadByteChanged (const juce::MemoryBlock& good, std::size_t rawIndex)
    {
        juce::MemoryInputStream compressed (static_cast<const char*> (good.getData()) + headerSize,
                                            good.getSize() - headerSize, false);
        juce::GZIPDecompressorInputStream gunzip (&compressed, false, juce::GZIPDecompressorInputStream::gzipFormat);
        juce::MemoryOutputStream raw;
        raw.writeFromInputStream (gunzip, -1);
        static_cast<std::uint8_t*> (const_cast<void*> (raw.getData()))[rawIndex] ^= 0x5A;

        juce::MemoryOutputStream recompressed;
        {
            juce::GZIPCompressorOutputStream gzip (recompressed, 9, juce::GZIPCompressorOutputStream::windowBitsGZIP);
            gzip.write (raw.getData(), raw.getDataSize());
            gzip.flush();
        }

        juce::MemoryBlock out (good.getData(), headerSize);
        const auto len = (juce::int64) recompressed.getDataSize();
        for (int i = 0; i < 8; ++i)
            *at (out, 20 + (std::size_t) i) = (std::uint8_t) ((len >> (8 * i)) & 0xFF);
        out.append (recompressed.getData(), recompressed.getDataSize());
        return out;
    }
}

TEST_CASE ("SongFile: bytes round-trip to an equivalent tree", "[songfile]")
{
    SongDocument doc;
    fillSample (doc);
    const auto bytes = writeSongBytes (doc.getTree());

    CHECK (bytes.getSize() > headerSize);
    CHECK (std::memcmp (bytes.getData(), "SGSM", 4) == 0);
    CHECK (readSongBytes (bytes).isEquivalentTo (doc.getTree()));
}

TEST_CASE ("SongFile: a file that is not a Song file is refused", "[songfile]")
{
    CHECK (kindOf (juce::MemoryBlock()) == SongFileErrorKind::NotASongFile);
    CHECK (kindOf (juce::MemoryBlock ("abc", 3)) == SongFileErrorKind::NotASongFile);
    CHECK (kindOf (juce::MemoryBlock ("MThd\0\0\0\6\0\1\0\1\1\xe0 and plenty more bytes here", 40)) == SongFileErrorKind::NotASongFile);
}

TEST_CASE ("SongFile: a newer format version is refused as unsupported", "[songfile]")
{
    SongDocument doc;
    fillSample (doc);
    auto bytes = writeSongBytes (doc.getTree());
    *at (bytes, 4) = 2;
    CHECK (kindOf (bytes) == SongFileErrorKind::UnsupportedVersion);
}

TEST_CASE ("SongFile: version 0 is corrupt, not a real version", "[songfile]")
{
    SongDocument doc;
    fillSample (doc);
    auto bytes = writeSongBytes (doc.getTree());
    *at (bytes, 4) = 0;
    CHECK (kindOf (bytes) == SongFileErrorKind::Corrupt);
}

TEST_CASE ("SongFile: truncation and trailing bytes are detected", "[songfile]")
{
    SongDocument doc;
    fillSample (doc);
    const auto good = writeSongBytes (doc.getTree());

    auto shortHeader = good;  shortHeader.setSize (headerSize - 1);
    CHECK (kindOf (shortHeader) == SongFileErrorKind::Truncated);

    auto shortPayload = good; shortPayload.setSize (good.getSize() - 5);
    CHECK (kindOf (shortPayload) == SongFileErrorKind::Truncated);

    auto trailing = good;     trailing.append ("x", 1);
    CHECK (kindOf (trailing) == SongFileErrorKind::Corrupt);
}

TEST_CASE ("SongFile: a damaged payload or wrong stored length is a checksum mismatch", "[songfile]")
{
    SongDocument doc;
    fillSample (doc);
    const auto good = writeSongBytes (doc.getTree());

    auto flipped = good;
    *at (flipped, headerSize + (good.getSize() - headerSize) / 2) ^= 0x01;
    CHECK (kindOf (flipped) == SongFileErrorKind::ChecksumMismatch);

    // A valid gzip stream of the right length whose content differs from what
    // the stored crc32 describes: only the crc32 comparison can catch this
    // (a deflate bit flip usually fails inflation first, or decodes unchanged).
    CHECK (kindOf (withPayloadByteChanged (good, 40)) == SongFileErrorKind::ChecksumMismatch);

    auto wrongLength = good;
    *at (wrongLength, 8) += 1;                 // uncompressedLength low byte
    CHECK (kindOf (wrongLength) == SongFileErrorKind::ChecksumMismatch);

    auto wrongCrc = good;
    *at (wrongCrc, 16) ^= 0xFF;                // crc32 low byte
    CHECK (kindOf (wrongCrc) == SongFileErrorKind::ChecksumMismatch);
}

TEST_CASE ("SongFile: an intact container holding an invalid tree is InvalidStructure", "[songfile]")
{
    // Built from a VALID serialization of a bad tree (never from garbage bytes,
    // which would trip ValueTree::readFromStream's Debug asserts).
    SongDocument doc;
    fillSample (doc);
    auto tree = doc.getTree().createCopy();
    tree.getChildWithName (SongIDs::SOURCE_MIDI).getChild (1).setProperty (SongIDs::isConductor, true, nullptr);
    CHECK (kindOf (writeSongBytes (tree)) == SongFileErrorKind::InvalidStructure);
}

TEST_CASE ("SongFile: save then load via a real file", "[songfile]")
{
    SongDocument doc;
    fillSample (doc);
    auto file = juce::File::createTempFile (".songsmith");

    REQUIRE (saveSongFile (doc, file));
    CHECK (loadSongFile (file).isEquivalentTo (doc.getTree()));

    file.deleteFile();
}

TEST_CASE ("SongFile: loading a missing file throws NotASongFile with a readable message", "[songfile]")
{
    const auto missing = juce::File::createTempFile (".songsmith");   // created then deleted -> absent
    missing.deleteFile();
    try { loadSongFile (missing); FAIL ("did not throw"); }
    catch (const SongFileError& e)
    {
        CHECK (e.kind() == SongFileErrorKind::NotASongFile);
        CHECK (std::string (e.what()).find ("Could not read") == 0);
    }
}

TEST_CASE ("SongFile: a failed save leaves the existing file intact", "[songfile]")
{
    SongDocument doc;
    fillSample (doc);
    auto file = juce::File::createTempFile (".songsmith");
    REQUIRE (saveSongFile (doc, file));
    const auto before = file.loadFileAsString();

    // A destination inside a directory that does not exist cannot be written.
    const auto impossible = file.getSiblingFile ("no-such-dir-songsmith").getChildFile ("x.songsmith");
    CHECK_FALSE (saveSongFile (doc, impossible));
    CHECK (file.loadFileAsString() == before);

    file.deleteFile();
}

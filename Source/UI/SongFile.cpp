#include "SongFile.h"

#include <array>
#include <cstring>

namespace lotro
{

namespace
{
    constexpr char magic[4] = { 'S', 'G', 'S', 'M' };
    constexpr std::size_t headerSize = 28;
    constexpr juce::int64 maxUncompressedBytes = 256LL * 1024 * 1024;   // guards against a decompression bomb

    std::uint32_t crc32 (const void* data, std::size_t size)
    {
        static const auto table = []
        {
            std::array<std::uint32_t, 256> t {};
            for (std::uint32_t i = 0; i < 256; ++i)
            {
                std::uint32_t c = i;
                for (int k = 0; k < 8; ++k)
                    c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                t[i] = c;
            }
            return t;
        }();

        std::uint32_t crc = 0xFFFFFFFFu;
        const auto* p = static_cast<const std::uint8_t*> (data);
        for (std::size_t i = 0; i < size; ++i)
            crc = table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
        return crc ^ 0xFFFFFFFFu;
    }
}

juce::MemoryBlock writeSongBytes (const juce::ValueTree& song)
{
    juce::MemoryOutputStream raw;
    song.writeToStream (raw);

    juce::MemoryOutputStream compressed;
    {
        juce::GZIPCompressorOutputStream gzip (compressed, 9, juce::GZIPCompressorOutputStream::windowBitsGZIP);
        gzip.write (raw.getData(), raw.getDataSize());
        gzip.flush();
    }   // gzip destroyed here: trailer written

    juce::MemoryOutputStream out;
    out.write (magic, sizeof (magic));
    out.writeInt ((int) songFileFormatVersion);
    out.writeInt64 ((juce::int64) raw.getDataSize());
    out.writeInt ((int) crc32 (raw.getData(), raw.getDataSize()));
    out.writeInt64 ((juce::int64) compressed.getDataSize());
    out.write (compressed.getData(), compressed.getDataSize());
    return out.getMemoryBlock();
}

juce::ValueTree readSongBytes (const juce::MemoryBlock& bytes)
{
    if (bytes.getSize() < sizeof (magic) || std::memcmp (bytes.getData(), magic, sizeof (magic)) != 0)
        throw SongFileError (SongFileErrorKind::NotASongFile, "This is not a Songsmith Song file.");
    if (bytes.getSize() < headerSize)
        throw SongFileError (SongFileErrorKind::Truncated, "This Song file is incomplete (it ends inside its header).");

    juce::MemoryInputStream in (bytes, false);
    in.setPosition (sizeof (magic));
    const auto version = (std::uint32_t) in.readInt();
    const auto uncompressedLength = in.readInt64();
    const auto expectedCrc = (std::uint32_t) in.readInt();
    const auto payloadLength = in.readInt64();

    if (version == 0)
        throw SongFileError (SongFileErrorKind::Corrupt, "This Song file is damaged (invalid version).");
    if (version > songFileFormatVersion)
        throw SongFileError (SongFileErrorKind::UnsupportedVersion,
                             "This Song was saved by a newer version of Songsmith. Update Songsmith to open it.");

    const auto remaining = (juce::int64) (bytes.getSize() - headerSize);
    if (payloadLength < 0 || uncompressedLength < 0 || uncompressedLength > maxUncompressedBytes)
        throw SongFileError (SongFileErrorKind::Corrupt, "This Song file is damaged (invalid lengths).");
    if (remaining < payloadLength)
        throw SongFileError (SongFileErrorKind::Truncated, "This Song file is incomplete (it was cut short).");
    if (remaining > payloadLength)
        throw SongFileError (SongFileErrorKind::Corrupt, "This Song file is damaged (unexpected extra data).");

    // No older versions exist yet, so there is no migration chain to run here.
    juce::MemoryInputStream compressed (static_cast<const char*> (bytes.getData()) + headerSize,
                                        (std::size_t) payloadLength, false);
    juce::GZIPDecompressorInputStream gunzip (&compressed, false, juce::GZIPDecompressorInputStream::gzipFormat);
    juce::MemoryOutputStream raw;
    const auto written = raw.writeFromInputStream (gunzip, uncompressedLength + 1);

    if (written != uncompressedLength
        || crc32 (raw.getData(), raw.getDataSize()) != expectedCrc)
        throw SongFileError (SongFileErrorKind::ChecksumMismatch,
                             "This Song file is damaged (its contents failed an integrity check).");

    juce::MemoryInputStream treeStream (raw.getData(), raw.getDataSize(), false);
    auto tree = juce::ValueTree::readFromStream (treeStream);
    if (! tree.isValid())
        throw SongFileError (SongFileErrorKind::Corrupt, "This Song file is damaged (its contents could not be read).");

    if (auto error = SongDocument::validateLoaded (tree))
        throw *error;

    return tree;
}

bool saveSongFile (const SongDocument& doc, const juce::File& file)
{
    const auto bytes = writeSongBytes (doc.getTree());
    return file.replaceWithData (bytes.getData(), bytes.getSize());
}

juce::ValueTree loadSongFile (const juce::File& file)
{
    juce::MemoryBlock bytes;
    if (! file.loadFileAsData (bytes))
        throw SongFileError (SongFileErrorKind::NotASongFile, "Could not read " + file.getFullPathName().toStdString());
    return readSongBytes (bytes);
}

} // namespace lotro

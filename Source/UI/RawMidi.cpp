#include "RawMidi.h"

#include <algorithm>
#include <istream>
#include <iterator>
#include <limits>
#include <ostream>
#include <string>

namespace lotro
{

bool RawMidiEvent::operator== (const RawMidiEvent& other) const
{
    return tick == other.tick && bytes == other.bytes;
}

bool RawMidiTrack::operator== (const RawMidiTrack& other) const
{
    return endTick == other.endTick && events == other.events;
}

bool RawMidiFile::operator== (const RawMidiFile& other) const
{
    return format == other.format && ticksPerQuarter == other.ticksPerQuarter && tracks == other.tracks;
}

namespace
{
    constexpr std::uint32_t kMThd = 0x4D546864; // "MThd"
    constexpr std::uint32_t kMTrk = 0x4D54726B; // "MTrk"
    constexpr std::uint32_t kRIFF = 0x52494646; // "RIFF"

    class Reader
    {
    public:
        Reader (const std::uint8_t* d, size_t n, std::string_view sourceName)
            : data (d), size (n), name (sourceName) {}

        [[noreturn]] void fail (const std::string& why) const
        {
            throw MidiImportError ("Malformed MIDI data (" + why + "): " + std::string (name));
        }

        size_t remaining() const { return size - pos; }

        void need (size_t n) const
        {
            if (n > remaining())
                fail ("unexpected end of data");
        }

        Reader sub (size_t n)
        {
            need (n);
            Reader r (data + pos, n, name);
            pos += n;
            return r;
        }

        std::uint8_t peek() const { need (1); return data[pos]; }
        std::uint8_t u8()         { need (1); return data[pos++]; }
        std::uint32_t u16()       { const std::uint32_t hi = u8(); return (hi << 8) | u8(); }
        std::uint32_t u32()       { const std::uint32_t hi = u16(); return (hi << 16) | u16(); }

        std::uint32_t vlq()
        {
            std::uint32_t value = 0;
            for (int i = 0; i < 4; ++i)
            {
                const auto b = u8();
                value = (value << 7) | (b & 0x7Fu);
                if ((b & 0x80u) == 0)
                    return value;
            }
            fail ("unterminated variable-length number");
        }

        std::vector<std::uint8_t> bytes (size_t n)
        {
            need (n);
            std::vector<std::uint8_t> out (data + pos, data + pos + n);
            pos += n;
            return out;
        }

    private:
        const std::uint8_t* data;
        size_t              size;
        size_t              pos = 0;
        std::string_view    name;
    };

    int channelDataLength (std::uint8_t status)
    {
        const int type = status & 0xF0;
        return (type == 0xC0 || type == 0xD0) ? 1 : 2;
    }

    RawMidiTrack readTrack (Reader r)
    {
        RawMidiTrack track;
        bool sawEndOfTrack = false;
        std::uint8_t runningStatus = 0;
        long long tick = 0;

        while (r.remaining() > 0)
        {
            tick += r.vlq();
            if (tick > std::numeric_limits<int>::max())
                r.fail ("tick overflow");

            RawMidiEvent event;
            event.tick = (int) tick;
            std::uint8_t status = r.peek();

            if (status == 0xFF)
            {
                r.u8();
                const auto type    = r.u8();
                const auto length  = r.vlq();
                const auto payload = r.bytes (length);

                if (type == 0x2F)
                {
                    if (! sawEndOfTrack)
                    {
                        track.endTick = event.tick;
                        sawEndOfTrack = true;
                    }
                    continue;
                }

                event.bytes.reserve (2 + payload.size());
                event.bytes.push_back (0xFF);
                event.bytes.push_back (type);
                event.bytes.insert (event.bytes.end(), payload.begin(), payload.end());
            }
            else if (status == 0xF0 || status == 0xF7)
            {
                r.u8();
                const auto length  = r.vlq();
                const auto payload = r.bytes (length);
                event.bytes.reserve (1 + payload.size());
                event.bytes.push_back (status);
                event.bytes.insert (event.bytes.end(), payload.begin(), payload.end());
            }
            else if (status >= 0xF0)
            {
                r.fail ("unsupported system message in a track");
            }
            else
            {
                // Meta and SysEx deliberately leave runningStatus alone:
                // juce::MidiFile's readTrack does the same, and matching it
                // keeps both parsers agreeing on every file importMidi accepts.
                if ((status & 0x80) != 0)
                {
                    r.u8();
                    runningStatus = status;
                }
                else if (runningStatus == 0)
                {
                    r.fail ("data byte with no running status");
                }
                else
                {
                    status = runningStatus;
                }

                event.bytes.push_back (status);
                for (int i = 0; i < channelDataLength (status); ++i)
                    event.bytes.push_back (r.u8());
            }

            if (! sawEndOfTrack)
                track.endTick = event.tick;

            track.events.push_back (std::move (event));
        }

        return track;
    }

    void putVlq (std::vector<std::uint8_t>& out, std::uint32_t v)
    {
        std::uint8_t buffer[5];
        int n = 0;
        buffer[n++] = (std::uint8_t) (v & 0x7F);
        while ((v >>= 7) != 0)
            buffer[n++] = (std::uint8_t) ((v & 0x7F) | 0x80);
        while (n > 0)
            out.push_back (buffer[--n]);
    }

    void put32 (std::vector<std::uint8_t>& out, std::uint32_t v)
    {
        out.push_back ((std::uint8_t) (v >> 24));
        out.push_back ((std::uint8_t) (v >> 16));
        out.push_back ((std::uint8_t) (v >> 8));
        out.push_back ((std::uint8_t) v);
    }

    void put16 (std::vector<std::uint8_t>& out, std::uint32_t v)
    {
        out.push_back ((std::uint8_t) (v >> 8));
        out.push_back ((std::uint8_t) v);
    }
}

RawMidiFile readMidiBytes (const std::vector<std::uint8_t>& bytes, std::string_view sourceName)
{
    const std::string name (sourceName);
    if (bytes.empty())
        throw MidiImportError ("MIDI input is empty: " + name);

    Reader r (bytes.data(), bytes.size(), sourceName);

    // Mirrors juce::MidiFile: a RIFF (RMID) wrapper is searched for "MThd"
    // within its next eight words.
    auto id = r.u32();
    if (id == kRIFF)
    {
        bool found = false;
        for (int i = 0; i < 8 && ! found; ++i)
            found = (r.u32() == kMThd);
        if (! found)
            r.fail ("no MThd header");
    }
    else if (id != kMThd)
    {
        r.fail ("no MThd header");
    }

    // Like JUCE, the declared header length is only validated, never used to
    // skip extra header bytes.
    if (r.u32() > r.remaining())
        r.fail ("header length past end of data");

    RawMidiFile file;
    file.format = (int) r.u16();
    if (file.format > 2)
        r.fail ("unknown format " + std::to_string (file.format));

    const int numTracks = (int) r.u16();
    if (file.format == 0 && numTracks != 1)
        r.fail ("format 0 must have exactly one track");

    const auto division = r.u16();
    if (division == 0 || (division & 0x8000) != 0)
        throw MidiImportError ("SMPTE time format is not supported (time format "
                               + std::to_string ((short) division) + "): " + name);
    file.ticksPerQuarter = (int) division;

    for (int t = 0; t < numTracks; ++t)
    {
        const auto chunkId = r.u32();
        const auto length  = r.u32();
        auto body = r.sub (length);
        if (chunkId == kMTrk)
            file.tracks.push_back (readTrack (body));
    }

    if (r.remaining() != 0)
        r.fail ("trailing bytes after the last chunk");

    return file;
}

RawMidiFile readMidiFile (std::istream& input, std::string_view sourceName)
{
    if (! input.good())
        throw MidiImportError ("MIDI input stream is not readable: " + std::string (sourceName));

    const std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (input)),
                                           std::istreambuf_iterator<char>());
    return readMidiBytes (bytes, sourceName);
}

std::vector<std::uint8_t> writeMidiBytes (const RawMidiFile& file)
{
    std::vector<std::uint8_t> out;
    put32 (out, kMThd);
    put32 (out, 6);
    put16 (out, (std::uint32_t) file.format);
    put16 (out, (std::uint32_t) file.tracks.size());
    put16 (out, (std::uint32_t) file.ticksPerQuarter);

    for (const auto& track : file.tracks)
    {
        std::vector<std::uint8_t> body;
        int lastTick = 0;

        for (const auto& event : track.events)
        {
            if (event.bytes.empty())
                throw MidiExportError ("Cannot write an empty MIDI event");
            if (event.tick < lastTick)
                throw MidiExportError ("MIDI events are out of tick order");

            putVlq (body, (std::uint32_t) (event.tick - lastTick));
            lastTick = event.tick;

            const auto status = event.bytes[0];
            if (status == 0xFF)
            {
                if (event.bytes.size() < 2)
                    throw MidiExportError ("Meta event without a type byte");
                body.push_back (0xFF);
                body.push_back (event.bytes[1]);
                putVlq (body, (std::uint32_t) (event.bytes.size() - 2));
                body.insert (body.end(), event.bytes.begin() + 2, event.bytes.end());
            }
            else if (status == 0xF0 || status == 0xF7)
            {
                body.push_back (status);
                putVlq (body, (std::uint32_t) (event.bytes.size() - 1));
                body.insert (body.end(), event.bytes.begin() + 1, event.bytes.end());
            }
            else
            {
                body.insert (body.end(), event.bytes.begin(), event.bytes.end());
            }
        }

        const int endTick = std::max (track.endTick, lastTick);
        putVlq (body, (std::uint32_t) (endTick - lastTick));
        body.push_back (0xFF);
        body.push_back (0x2F);
        body.push_back (0x00);

        put32 (out, kMTrk);
        put32 (out, (std::uint32_t) body.size());
        out.insert (out.end(), body.begin(), body.end());
    }

    return out;
}

void writeMidiFile (const RawMidiFile& file, std::ostream& output)
{
    const auto bytes = writeMidiBytes (file);
    output.write (reinterpret_cast<const char*> (bytes.data()), (std::streamsize) bytes.size());
    output.flush();
    if (! output)
        throw MidiExportError ("Could not write MIDI data");
}

} // namespace lotro

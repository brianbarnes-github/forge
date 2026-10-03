#pragma once

// Byte-level Standard MIDI File builders for tests. TrackBody::ev writes the
// delta time and then the raw bytes exactly as given, so a test can omit a
// status byte to exercise running status.

#include <cstdint>
#include <initializer_list>
#include <vector>

namespace miditest
{
    using Bytes = std::vector<std::uint8_t>;

    inline void append (Bytes& out, const Bytes& more)
    {
        out.insert (out.end(), more.begin(), more.end());
    }

    inline Bytes vlq (std::uint32_t v)
    {
        Bytes reversed { (std::uint8_t) (v & 0x7F) };
        while ((v >>= 7) != 0)
            reversed.push_back ((std::uint8_t) ((v & 0x7F) | 0x80));
        return Bytes (reversed.rbegin(), reversed.rend());
    }

    inline Bytes be32 (std::uint32_t v)
    {
        return { (std::uint8_t) (v >> 24), (std::uint8_t) (v >> 16), (std::uint8_t) (v >> 8), (std::uint8_t) v };
    }

    inline Bytes be16 (std::uint32_t v)
    {
        return { (std::uint8_t) (v >> 8), (std::uint8_t) v };
    }

    inline Bytes chunk (const char* id, const Bytes& body)
    {
        Bytes out (id, id + 4);
        append (out, be32 ((std::uint32_t) body.size()));
        append (out, body);
        return out;
    }

    struct TrackBody
    {
        Bytes body;

        TrackBody& ev (std::uint32_t delta, std::initializer_list<std::uint8_t> raw)
        {
            append (body, vlq (delta));
            body.insert (body.end(), raw);
            return *this;
        }

        TrackBody& eot (std::uint32_t delta = 0) { return ev (delta, { 0xFF, 0x2F, 0x00 }); }
    };

    inline Bytes header (int format, int numTracks, int ppq)
    {
        Bytes body;
        append (body, be16 ((std::uint32_t) format));
        append (body, be16 ((std::uint32_t) numTracks));
        append (body, be16 ((std::uint32_t) ppq));
        return chunk ("MThd", body);
    }

    inline Bytes smf (int format, int ppq, const std::vector<TrackBody>& tracks)
    {
        Bytes out = header (format, (int) tracks.size(), ppq);
        for (const auto& t : tracks)
            append (out, chunk ("MTrk", t.body));
        return out;
    }
}

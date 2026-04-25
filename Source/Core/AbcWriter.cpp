// AbcWriter — MIDI Song → LOTRO-ABC text.
//
// Emission model: "cluster-at-boundary with z-pulse".
// ---------------------------------------------------
// Standard ABC is monophonic-with-chords: each token advances a single shared
// playback clock. LOTRO doesn't honor `V:` voices, so representing polyphony
// within one part requires encoding overlap some other way. The technique
// used here:
//
//   * Walk the MIDI notes by start-tick cluster (all notes starting at the
//     same tick become one emission).
//   * For each cluster, emit one chord token `[note1dur1 note2dur2 ...]`
//     where each note carries its own duration. The chord's stream-advance
//     is the *shortest* inner element; the longer notes keep ringing as
//     subsequent tokens play on top. This sidesteps ties entirely.
//   * When the cluster's longest note extends past the *next* cluster's
//     start, we add a `z` rest inside the chord brackets as the pulse
//     element. With the z-pulse as the shortest inner, the stream advances
//     by exactly the gap to the next cluster, while the long note keeps
//     ringing ambiently. This is the only place bare rests appear inside
//     `[]` brackets — non-standard ABC, but LOTRO accepts it. Compatibility
//     with the 2011-era reference output `rideintochetwood.abc` pins this
//     invariant in `BarAlignment_tests.cpp`.
//
// Bar alignment:
// --------------
// The z-pulse's length is capped at the remainder of the current bar. If a
// cluster's gap to the next cluster extends past a bar line, the chord's
// z-pulse advances to the bar line; the leftover silence emits as a plain
// `z` rest on the *next* bar's line. This keeps `% bar N` comments aligned
// to absolute MIDI ticks — every bar block's token durations sum to exactly
// one bar. Held notes still ring for their full MIDI duration because the
// cap is applied only to the chord's z-pulse, not to the cluster notes'
// inner durations.
//
// No ties, no bar-line-splitting of note tokens, no explicit outer chord
// duration suffix. Dynamics emit as inline `+xx+` markers per §2.7.

#include "AbcWriter.h"

#include "DrumMap.h"
#include "Constraints/DynamicMapper.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <numeric>
#include <string>

namespace lotro
{

namespace
{
    const char* letterFor (int pitchClass, bool& needsSharp) noexcept
    {
        static constexpr const char* natural[12] = {
            "C", "C", "D", "D", "E", "F", "F", "G", "G", "A", "A", "B"
        };
        static constexpr bool sharp[12] = {
            false, true, false, true, false, false, true, false, true, false, true, false
        };
        needsSharp = sharp[pitchClass];
        return natural[pitchClass];
    }

    int clampMeterDenominator (int d) noexcept
    {
        if (d <= 1) return 1;
        if (d <= 2) return 2;
        if (d <= 4) return 4;
        return 8;
    }

    std::string emitHeader (const Song& song, const Track& track, int partIndex)
    {
        const double bpm = song.tempoMap.empty() ? 120.0 : song.tempoMap.front().bpm;

        int meterNum = 4, meterDen = 4;
        if (! song.meterMap.empty())
        {
            meterNum = song.meterMap.front().numerator;
            meterDen = clampMeterDenominator (song.meterMap.front().denominator);
        }

        std::string header;
        header += "X:" + std::to_string (partIndex) + "\n";
        header += "T:" + song.title + " - " + track.name + "\n";
        header += "Z:" + song.transcriber + "\n";
        header += "L:1/8\n";
        header += "Q:" + std::to_string ((int) std::lround (bpm)) + "\n";
        header += "M:" + std::to_string (meterNum) + "/" + std::to_string (meterDen) + "\n";
        header += "K:C\n";
        return header;
    }

    struct NoteGroup
    {
        int               startTick     = 0;
        int               durationTicks = 0;
        std::vector<Note> notes;
    };

    std::vector<NoteGroup> groupByStartTick (const std::vector<Note>& notes)
    {
        std::vector<NoteGroup> groups;
        for (const auto& n : notes)
        {
            if (! groups.empty() && groups.back().startTick == n.startTick)
            {
                groups.back().notes.push_back (n);
                groups.back().durationTicks = std::min (groups.back().durationTicks, n.durationTicks);
            }
            else
            {
                NoteGroup g;
                g.startTick     = n.startTick;
                g.durationTicks = n.durationTicks;
                g.notes.push_back (n);
                groups.push_back (g);
            }
        }
        return groups;
    }

    // LOTRO ABC letters are always in the 3-octave range C, .. c' (no ,,
    // or '' notation). Each instrument's native MIDI range maps to those
    // same letters — LOTRO applies its per-instrument pitch shift on
    // playback. So before converting to a letter, we shift the absolute
    // MIDI pitch so that the instrument's `midiLow` becomes MIDI 48
    // (standard ABC's "C,"). The letter/octave math downstream then
    // always lands in [C, .. c']. Clarinet (midiLow=48) is the identity
    // shift; Lute (36) shifts +12; Theorbo (24) shifts +24; Flute (60)
    // shifts -12; etc.
    std::string emitNotePart (const Note& note,
                              LotroInstrument instrument,
                              bool isDrum,
                              const DrumMap& drumMap)
    {
        if (isDrum)
        {
            auto mapped = drumMap.lookup (note.pitch);
            if (! mapped.has_value())
                return {};
            return std::string (mapped->data(), mapped->size());
        }

        const int  midiLow      = rangeFor (instrument).midiLow;
        const int  shiftedPitch = note.pitch - midiLow + 48;

        bool needsSharp = false;
        const char* base = letterFor (shiftedPitch % 12, needsSharp);

        const int octaveIndex = (int) std::floor ((shiftedPitch - 60) / 12.0);

        std::string token;
        if (needsSharp) token += '^';

        if (octaveIndex >= 1)
        {
            token += (char) std::tolower ((unsigned char) base[0]);
            for (int i = 1; i < octaveIndex; ++i)
                token += '\'';
        }
        else
        {
            token += base;
            for (int i = 0; i < -octaveIndex; ++i)
                token += ',';
        }

        return token;
    }

    // Result of building a cluster's chord token. fillerTicks is the leftover
    // silence between chord-end and next-cluster-start that the caller must
    // emit (possibly split across bar lines). Zero when the cluster is empty.
    struct ClusterEmission
    {
        std::string chord;             // the `[...]` token, empty if all notes unmapped
        int         chordAdvance  = 0; // how many ticks the chord consumes
        int         fillerTicks   = 0; // rest to emit after the chord
        int         totalAdvance  = 0; // chordAdvance + fillerTicks
    };

    ClusterEmission buildCluster (const NoteGroup& group,
                                  const Track&     track,
                                  int              advanceNeeded,
                                  int              ticksPerQuarter,
                                  const DrumMap&   drumMap)
    {
        const bool isDrum = (track.instrument == LotroInstrument::Drums);

        std::vector<std::string> parts;
        parts.reserve (group.notes.size());
        int clusterMin = std::numeric_limits<int>::max();
        int clusterMax = 0;

        for (const auto& n : group.notes)
        {
            const auto np = emitNotePart (n, track.instrument, isDrum, drumMap);
            if (np.empty()) continue;
            parts.push_back (np + abcDurationToken (n.durationTicks, ticksPerQuarter));
            clusterMin = std::min (clusterMin, n.durationTicks);
            clusterMax = std::max (clusterMax, n.durationTicks);
        }

        ClusterEmission result;

        if (parts.empty())
        {
            result.fillerTicks  = std::max (0, advanceNeeded);
            result.totalAdvance = result.fillerTicks;
            return result;
        }

        const bool hasHeldNote = advanceNeeded > 0 && clusterMax > advanceNeeded;

        std::string chord;
        chord += '[';
        for (const auto& p : parts) chord += p;
        if (hasHeldNote)
            chord += "z" + abcDurationToken (advanceNeeded, ticksPerQuarter);
        chord += "] ";
        result.chord = chord;

        result.chordAdvance = clusterMin;
        if (hasHeldNote)
            result.chordAdvance = std::min (result.chordAdvance, advanceNeeded);

        if (advanceNeeded > 0 && result.chordAdvance < advanceNeeded)
            result.fillerTicks = advanceNeeded - result.chordAdvance;

        result.totalAdvance = result.chordAdvance + result.fillerTicks;
        return result;
    }

    int barTicksForMeter (const MeterChange& m, int ticksPerQuarter) noexcept
    {
        if (m.denominator <= 0) return ticksPerQuarter * 4;
        return m.numerator * ticksPerQuarter * 4 / m.denominator;
    }

    // Returns the length (in ticks) of the bar containing `atTick`, using
    // the most recent meter change at or before that tick. When meterMap is
    // empty or has a single entry, the result is constant.
    int barTicksAt (const std::vector<MeterChange>& meterMap,
                    int ticksPerQuarter, int atTick) noexcept
    {
        if (meterMap.empty())
            return ticksPerQuarter * 4;

        const MeterChange* current = &meterMap.front();
        for (const auto& m : meterMap)
        {
            if (m.tick <= atTick) current = &m;
            else                  break;
        }
        return barTicksForMeter (*current, ticksPerQuarter);
    }

    // Accumulates the ABC body for one part. Owns the stream-tick cursor,
    // bar-label state, and the text buffer. Callers feed it rests, clusters,
    // and dynamic markings in tick order; emitter handles bar-break emission
    // and rest splitting internally.
    class ChordEmitter
    {
    public:
        ChordEmitter (const std::vector<TempoChange>& tempoMap,
                      const std::vector<MeterChange>& meterMap,
                      int ticksPerQuarter)
            : tempos (tempoMap),
              meters (meterMap),
              ppq (ticksPerQuarter)
        {
            // The first entry of each map is already reflected in the part
            // header (Q:, M:); skip it so we don't re-report.
            if (! tempos.empty()) reportedTempoIdx = 1;
            if (! meters.empty()) reportedMeterIdx = 1;

            const int firstBar = barTicksAt (meters, ppq, 0);
            nextBarTick = firstBar;
            if (firstBar > 0)
            {
                body += "% bar " + std::to_string (barNumber) + "\n";
                emitChangesInRange (0, firstBar);
            }
        }

        int currentTick() const noexcept { return streamTick; }

        // Ticks remaining in the bar that contains `fromTick`. Used to cap
        // a cluster's z-pulse so a chord token never spans a bar boundary.
        int barRemainderFrom (int fromTick) const noexcept
        {
            return (nextBarTick > 0) ? std::max (0, nextBarTick - fromTick) : 0;
        }

        void emitDynamic (const char* marking)
        {
            body += '+';
            body += marking;
            body += '+';
        }

        // Emit a rest of `ticks`, splitting at bar boundaries so each bar
        // gets its own line.
        void emitRest (int ticks)
        {
            while (ticks > 0)
            {
                const int untilBar = (nextBarTick > 0) ? (nextBarTick - streamTick) : ticks;
                const int chunk    = (untilBar > 0) ? std::min (ticks, untilBar) : ticks;
                body += "z" + abcDurationToken (chunk, ppq) + " ";
                streamTick += chunk;
                ticks      -= chunk;
                flushBarBreaks();
            }
        }

        // Emit a cluster's chord token followed by its trailing filler rest.
        // The chord itself is never split at bar boundaries — a chord may
        // contain held notes whose duration legitimately spans past the bar,
        // and splitting them would force re-articulation.
        void emitCluster (const ClusterEmission& emission)
        {
            if (! emission.chord.empty())
            {
                body += emission.chord;
                streamTick += emission.chordAdvance;
                flushBarBreaks();
            }

            if (emission.fillerTicks > 0)
                emitRest (emission.fillerTicks);
        }

        // Finalise and return the accumulated ABC body.
        std::string finish()
        {
            if (body.empty() || body.back() != '\n')
                body += '\n';
            return std::move (body);
        }

    private:
        void flushBarBreaks()
        {
            while (nextBarTick > 0 && streamTick >= nextBarTick)
            {
                body += "\n";
                ++barNumber;
                body += "% bar " + std::to_string (barNumber) + "\n";
                const int newBarStart = nextBarTick;
                const int thisBarLen  = barTicksAt (meters, ppq, newBarStart);
                if (thisBarLen <= 0) break;
                nextBarTick = newBarStart + thisBarLen;
                emitChangesInRange (newBarStart, nextBarTick);
            }
        }

        // Emit `% tempo: N bpm` / `% meter: n/d` comments for any unreported
        // entries whose tick lies in [lo, hi). Consumes the reported indices
        // so each change is surfaced at most once. Non-standard ABC; purely
        // informational for readers.
        void emitChangesInRange (int lo, int hi)
        {
            while (reportedTempoIdx < tempos.size() && tempos[reportedTempoIdx].tick < hi)
            {
                if (tempos[reportedTempoIdx].tick >= lo)
                {
                    body += "% tempo: ";
                    body += std::to_string ((int) std::lround (tempos[reportedTempoIdx].bpm));
                    body += " bpm\n";
                }
                ++reportedTempoIdx;
            }
            while (reportedMeterIdx < meters.size() && meters[reportedMeterIdx].tick < hi)
            {
                if (meters[reportedMeterIdx].tick >= lo)
                {
                    body += "% meter: ";
                    body += std::to_string (meters[reportedMeterIdx].numerator);
                    body += '/';
                    body += std::to_string (meters[reportedMeterIdx].denominator);
                    body += '\n';
                }
                ++reportedMeterIdx;
            }
        }

        std::string                     body;
        int                             streamTick       = 0;
        int                             nextBarTick      = 0;
        int                             barNumber        = 1;
        size_t                          reportedTempoIdx = 0;
        size_t                          reportedMeterIdx = 0;
        const std::vector<TempoChange>& tempos;
        const std::vector<MeterChange>& meters;
        const int                       ppq;
    };

    std::string emitBody (const Song& song, const Track& track)
    {
        const auto groups = groupByStartTick (track.notes);
        if (groups.empty())
            return {};

        ChordEmitter emitter (song.tempoMap, song.meterMap, song.ticksPerQuarter);

        std::vector<DynamicChangeRef> changes = track.dynamicChanges;
        size_t changeIdx = 0;

        for (size_t i = 0; i < groups.size(); ++i)
        {
            const auto& group = groups[i];

            while (changeIdx < changes.size() && changes[changeIdx].startTick <= group.startTick)
            {
                emitter.emitDynamic (abcMarkingFor ((DynamicMarking) changes[changeIdx].marking));
                ++changeIdx;
            }

            if (emitter.currentTick() < group.startTick)
                emitter.emitRest (group.startTick - emitter.currentTick());

            const int nextStart     = (i + 1 < groups.size()) ? groups[i + 1].startTick : 0;
            const int advanceNeeded = (nextStart > group.startTick) ? (nextStart - group.startTick) : 0;

            // Cap the chord's z-pulse at the remaining space in the current
            // bar so the chord token never spans a bar boundary.
            const int barRemainder      = emitter.barRemainderFrom (group.startTick);
            const int advanceForChord   = (advanceNeeded > 0 && barRemainder > 0)
                                             ? std::min (advanceNeeded, barRemainder)
                                             : advanceNeeded;
            const int advanceAfterChord = (advanceNeeded > 0)
                                             ? advanceNeeded - advanceForChord
                                             : 0;

            const DrumMap& effectiveMap = track.drumMap.has_value() ? *track.drumMap : song.drumMap;
            const auto emission = buildCluster (group, track, advanceForChord,
                                                song.ticksPerQuarter, effectiveMap);
            emitter.emitCluster (emission);

            if (advanceAfterChord > 0)
                emitter.emitRest (advanceAfterChord);
        }

        return emitter.finish();
    }
}

std::string abcPitchToken (int midiPitch)
{
    // Clarinet's midiLow=48 makes the shift a no-op — output matches the
    // standard-ABC letter mapping (MIDI 48→"C,", 60→"C", 72→"c", 84→"c'").
    return abcPitchToken (midiPitch, LotroInstrument::Clarinet);
}

std::string abcPitchToken (int midiPitch, LotroInstrument instrument)
{
    Note n;
    n.pitch = midiPitch;
    static const DrumMap unused;   // non-drum path, the map is never consulted
    return emitNotePart (n, instrument, false, unused);
}

std::string abcDurationToken (int durationTicks, int ticksPerQuarter)
{
    int num = 2 * durationTicks;
    int den = ticksPerQuarter;

    if (num <= 0) return {};

    const int g = std::gcd (num, den);
    num /= g;
    den /= g;

    if (den == 1)
        return (num == 1) ? std::string() : std::to_string (num);

    if (num == 1)
        return "/" + std::to_string (den);

    return std::to_string (num) + "/" + std::to_string (den);
}

std::string writeAbc (const Song& song)
{
    std::string out;
    out += "% Generated by Forge v0.1.0\n";
    out += "% Source: " + song.title + "\n\n";

    // Emit in ascending x order for tracks with an explicit x value; tracks
    // with x == 0 fall back to sequential auto-numbering AFTER all explicit-x
    // tracks. Stable sort preserves declaration order among ties.
    std::vector<const Track*> ordered;
    ordered.reserve (song.tracks.size());
    for (const auto& t : song.tracks)
        if (t.enabled && ! t.notes.empty())
            ordered.push_back (&t);

    std::stable_sort (ordered.begin(), ordered.end(),
                      [] (const Track* a, const Track* b)
                      {
                          if (a->x == 0 && b->x == 0) return false;
                          if (a->x == 0) return false;
                          if (b->x == 0) return true;
                          return a->x < b->x;
                      });

    int autoIndex = 0;
    for (const Track* pt : ordered)
        if (pt->x > 0) autoIndex = std::max (autoIndex, pt->x);

    for (const Track* pt : ordered)
    {
        const int x = (pt->x > 0) ? pt->x : ++autoIndex;
        out += emitHeader (song, *pt, x);
        out += emitBody (song, *pt);
        out += '\n';
    }

    return out;
}

} // namespace lotro

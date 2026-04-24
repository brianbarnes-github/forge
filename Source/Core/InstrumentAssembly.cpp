#include "InstrumentAssembly.h"

#include "DrumMapLoader.h"
#include "LotroInstrument.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>

namespace lotro
{

namespace
{
    int clampVelocity (double scaled) noexcept
    {
        const long v = std::lround (scaled);
        if (v < 1)   return 1;
        if (v > 127) return 127;
        return (int) v;
    }

    void emitUnreferencedDiags (const Song&         raw,
                                const Config&       config,
                                Diagnostics&        diagnostics)
    {
        std::set<int> referenced;
        for (const auto& inst : config.instruments)
            for (const auto& s : inst.sources)
                referenced.insert (s.midiTrackIndex);

        for (int i = 0; i < (int) raw.tracks.size(); ++i)
        {
            if (referenced.count (i) > 0) continue;
            Diagnostic d;
            d.severity         = Severity::Info;
            d.source           = "InstrumentAssembly";
            d.message          = "MIDI track " + std::to_string (i)
                               + " not referenced by any instrument; skipped";
            d.trackIndex       = -1;
            d.sourceTrackIndex = i;
            diagnostics.push_back (std::move (d));
        }
    }
}

Song assembleInstruments (const Song&         raw,
                          const Config&       config,
                          Diagnostics&        diagnostics)
{
    Song out;
    out.ticksPerQuarter = raw.ticksPerQuarter;
    out.tempoMap        = raw.tempoMap;
    out.meterMap        = raw.meterMap;
    out.title           = config.title.value_or (raw.title);
    if (config.transcriber.has_value())
        out.transcriber = *config.transcriber;
    else
        out.transcriber = raw.transcriber;
    out.drumMap         = raw.drumMap;

    if (config.tempo.has_value() && ! out.tempoMap.empty())
        out.tempoMap.front().bpm = *config.tempo;

    emitUnreferencedDiags (raw, config, diagnostics);

    for (const auto& inst : config.instruments)
    {
        Track t;
        t.x = inst.x;
        LotroInstrument parsed = LotroInstrument::LuteOfAges;
        parseName (inst.name, parsed);
        t.instrument = parsed;

        // Label fallback: explicit → first source's MIDI track name → LOTRO enum name.
        if (inst.label.has_value())
        {
            t.name = *inst.label;
        }
        else if (! inst.sources.empty()
                 && inst.sources.front().midiTrackIndex >= 0
                 && inst.sources.front().midiTrackIndex < (int) raw.tracks.size()
                 && ! raw.tracks[(size_t) inst.sources.front().midiTrackIndex].name.empty())
        {
            t.name = raw.tracks[(size_t) inst.sources.front().midiTrackIndex].name;
        }
        else
        {
            t.name = inst.name;
        }

        // Pitch handling per the MIDI-is-source-of-truth principle:
        //
        //   1. Octave-fold the original source pitch into the instrument's
        //      envelope ("songwriter fit" — the melody may have been authored
        //      for a different instrument's range; we shift it in whole
        //      octaves to fit without transposing pitch class).
        //   2. Add user transpose literally (Config.transpose + ConfigSource
        //      .transposeSemitones). This is user intent — we do not fold
        //      it away. If it pushes the note out of the envelope, we drop
        //      the note with a `TransposeOutOfRange` Warning diagnostic.
        //
        // Drums skip both steps (no envelope, no useful transpose).
        const bool isDrums = (parsed == LotroInstrument::Drums);
        int effectiveLow  = 0;
        int effectiveHigh = 0;
        if (! isDrums)
        {
            constexpr int abcLowMidi  = 48;
            constexpr int abcHighMidi = 84;
            const auto r = rangeFor (parsed);
            effectiveLow  = std::max (r.midiLow,  abcLowMidi);
            effectiveHigh = std::min (r.midiHigh, abcHighMidi);
        }

        for (const auto& src : inst.sources)
        {
            const int sIdx = src.midiTrackIndex;
            if (sIdx < 0 || sIdx >= (int) raw.tracks.size()) continue;
            const auto& srcTrack = raw.tracks[(size_t) sIdx];

            if (srcTrack.sourceMidiChannel == 10)
                t.sourceMidiChannel = 10;

            const int    totalTranspose = src.transposeSemitones + config.transpose;
            const double volumeScale    = 1.0 + ((double) src.volumePercent / 100.0);

            for (const auto& srcNote : srcTrack.notes)
            {
                Note n = srcNote;

                if (isDrums)
                {
                    n.pitch = srcNote.pitch + totalTranspose;
                }
                else
                {
                    int folded = srcNote.pitch;
                    while (folded < effectiveLow  && folded + 12 <= effectiveHigh) folded += 12;
                    while (folded > effectiveHigh && folded - 12 >= effectiveLow)  folded -= 12;

                    const int target = folded + totalTranspose;
                    if (target < effectiveLow || target > effectiveHigh)
                    {
                        Diagnostic d;
                        d.severity = Severity::Warning;
                        d.source   = "TransposeOutOfRange";
                        d.message  = "note dropped: transpose shifted MIDI "
                                   + std::to_string (srcNote.pitch)
                                   + " to " + std::to_string (target)
                                   + " outside ["
                                   + std::to_string (effectiveLow) + ","
                                   + std::to_string (effectiveHigh) + "] on '"
                                   + t.name + "' source MIDI-" + std::to_string (sIdx);
                        d.tick             = srcNote.startTick;
                        d.pitch            = srcNote.pitch;
                        d.sourceTrackIndex = srcNote.sourceTrackIndex;
                        d.sourceEventIndex = srcNote.sourceEventIndex;
                        diagnostics.push_back (std::move (d));
                        continue;
                    }
                    n.pitch = target;
                }

                if (std::abs (volumeScale - 1.0) > 1e-9)
                {
                    const double scaledD     = (double) srcNote.velocity * volumeScale;
                    const int    scaled      = clampVelocity (scaledD);
                    const bool   clampedHigh = scaledD > 127.0;
                    const bool   clampedLow  = scaledD < 1.0;

                    if (clampedHigh || clampedLow)
                    {
                        Diagnostic d;
                        d.severity         = Severity::Warning;
                        d.source           = "VolumeScale";
                        d.message          = "velocity clamped on '" + t.name
                                           + "' source MIDI-" + std::to_string (sIdx);
                        d.tick             = srcNote.startTick;
                        d.pitch            = srcNote.pitch;
                        d.sourceTrackIndex = srcNote.sourceTrackIndex;
                        d.sourceEventIndex = srcNote.sourceEventIndex;
                        diagnostics.push_back (std::move (d));
                    }
                    n.velocity = scaled;
                }

                t.notes.push_back (n);
            }
        }

        std::stable_sort (t.notes.begin(), t.notes.end(),
                          [] (const Note& a, const Note& b) { return a.startTick < b.startTick; });

        if (parsed == LotroInstrument::Drums && inst.drumMap.has_value())
        {
            DrumMap dm = defaultDrumMap();
            const auto err = loadDrumMapFromFile (*inst.drumMap, dm);
            if (! err.empty())
            {
                Diagnostic d;
                d.severity   = Severity::Warning;
                d.source     = "InstrumentAssembly";
                d.message    = "failed to load drumMap '" + *inst.drumMap + "': " + err;
                d.trackIndex = -1;
                diagnostics.push_back (std::move (d));
            }
            else
            {
                t.drumMap = std::move (dm);
            }
        }

        out.tracks.push_back (std::move (t));
    }

    return out;
}

} // namespace lotro

#include "MidiImporter.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <istream>
#include <iterator>
#include <string>
#include <vector>

namespace lotro
{

namespace
{
    constexpr int drumChannel = 10;

    Track importTrack (const juce::MidiMessageSequence& source,
                       int                              midiTrackIndex,
                       Diagnostics&                     diagnostics)
    {
        Track track;
        track.name = "Track " + std::to_string (midiTrackIndex);

        // We tag each Note with the ordinal of its source note-on event
        // within this MIDI track (note events only, non-notes don't count)
        // so an editor can find the exact source event later.
        int noteOnOrdinal = 0;

        for (int eventIndex = 0; eventIndex < source.getNumEvents(); ++eventIndex)
        {
            const auto* holder = source.getEventPointer (eventIndex);
            const auto& message = holder->message;
            const int   tick    = (int) message.getTimeStamp();

            if (message.isTrackNameEvent())
            {
                track.name = message.getTextFromTextMetaEvent().toStdString();
                continue;
            }

            if (message.isNoteOn())
            {
                const int thisNoteOrdinal = noteOnOrdinal++;
                const int channel = message.getChannel();

                if (channel == drumChannel)
                {
                    track.sourceMidiChannel = drumChannel;
                    track.instrument = LotroInstrument::Drums;
                }

                if (holder->noteOffObject == nullptr)
                {
                    Diagnostic d;
                    d.severity = Severity::Warning;
                    d.source   = "MidiImporter";
                    d.message  = "Unmatched note-on; skipping";
                    d.tick     = tick;
                    d.pitch    = message.getNoteNumber();
                    diagnostics.push_back (std::move (d));
                    continue;
                }

                const int offTick  = (int) holder->noteOffObject->message.getTimeStamp();
                const int duration = offTick - tick;

                if (duration <= 0)
                {
                    Diagnostic d;
                    d.severity = Severity::Warning;
                    d.source   = "MidiImporter";
                    d.message  = "Zero-length note; skipping";
                    d.tick     = tick;
                    d.pitch    = message.getNoteNumber();
                    diagnostics.push_back (std::move (d));
                    continue;
                }

                Note note;
                note.pitch            = message.getNoteNumber();
                note.startTick        = tick;
                note.durationTicks    = duration;
                note.velocity         = message.getVelocity();
                note.isDrum           = (channel == drumChannel);
                note.sourceTrackIndex = midiTrackIndex;
                note.sourceEventIndex = thisNoteOrdinal;
                track.notes.push_back (note);
            }
        }

        return track;
    }

    void importTempoAndMeter (const juce::MidiMessageSequence& source, Song& song)
    {
        for (int i = 0; i < source.getNumEvents(); ++i)
        {
            const auto& message = source.getEventPointer (i)->message;
            const int   tick    = (int) message.getTimeStamp();

            if (message.isTempoMetaEvent())
            {
                const double secondsPerQuarter = message.getTempoSecondsPerQuarterNote();
                if (secondsPerQuarter > 0.0)
                {
                    const double bpm = 60.0 / secondsPerQuarter;
                    song.tempoMap.push_back ({ tick, bpm });
                }
            }
            else if (message.isTimeSignatureMetaEvent())
            {
                int numerator = 4, denominator = 4;
                message.getTimeSignatureInfo (numerator, denominator);
                song.meterMap.push_back ({ tick, numerator, denominator });
            }
        }
    }
}

Song importMidi (std::istream& input, std::string_view sourceName, Diagnostics& diagnostics)
{
    const std::string name (sourceName);

    if (! input.good())
        throw MidiImportError ("MIDI input stream is not readable: " + name);

    // Slurp the stream into a byte buffer. juce::MidiFile::readFrom wants a
    // juce::InputStream; the simplest adapter is to hand it a
    // juce::MemoryInputStream over the buffer. MIDI files are small enough
    // that full-buffering is fine (megabytes at most).
    std::vector<char> buffer ((std::istreambuf_iterator<char> (input)),
                              std::istreambuf_iterator<char>());
    if (buffer.empty())
        throw MidiImportError ("MIDI input is empty: " + name);

    juce::MemoryInputStream stream (buffer.data(), buffer.size(), false);

    juce::MidiFile midi;
    if (! midi.readFrom (stream))
        throw MidiImportError ("Malformed MIDI data: " + name);

    const short timeFormat = midi.getTimeFormat();
    if (timeFormat <= 0)
        throw MidiImportError ("SMPTE time format is not supported (time format "
                               + std::to_string (timeFormat) + "): " + name);

    Song song;
    song.ticksPerQuarter = timeFormat;
    song.title           = name;

    for (int trackIndex = 0; trackIndex < midi.getNumTracks(); ++trackIndex)
    {
        const auto* sourceTrack = midi.getTrack (trackIndex);
        if (sourceTrack == nullptr)
            continue;

        importTempoAndMeter (*sourceTrack, song);

        const size_t diagsBefore = diagnostics.size();
        auto track = importTrack (*sourceTrack, trackIndex, diagnostics);

        if (track.notes.empty())
            continue;

        // Tag only this track's fresh diagnostics with its final index in
        // song.tracks. An editor uses this to jump to the affected track.
        const int finalIndex = (int) song.tracks.size();
        for (size_t i = diagsBefore; i < diagnostics.size(); ++i)
            if (diagnostics[i].trackIndex < 0)
                diagnostics[i].trackIndex = finalIndex;

        song.tracks.push_back (std::move (track));
    }

    if (song.tempoMap.empty())
        song.tempoMap.push_back ({ 0, 120.0 });

    if (song.meterMap.empty())
        song.meterMap.push_back ({ 0, 4, 4 });

    return song;
}

} // namespace lotro

#include "MidiImportPlan.h"
#include "JuceNoteReplica.h"

#include <juce_core/juce_core.h> // jassertfalse only

#include <map>

namespace lotro
{

namespace
{
    bool isChannelMessage (const RawMidiEvent& e)
    {
        return ! e.bytes.empty() && e.bytes[0] >= 0x80 && e.bytes[0] < 0xF0;
    }

    int channelOf (const RawMidiEvent& e) { return (e.bytes[0] & 0x0F) + 1; }

    bool isVelocityNoteOn (const RawMidiEvent& e)
    {
        return isChannelMessage (e) && (e.bytes[0] & 0xF0) == 0x90 && e.bytes.size() > 2 && e.bytes[2] > 0;
    }

    int firstChannel (const RawMidiTrack& track, int fallback)
    {
        for (const auto& e : track.events)
            if (isChannelMessage (e))
                return channelOf (e);
        return fallback;
    }

    int firstProgram (const RawMidiTrack& track)
    {
        for (const auto& e : track.events)
            if (isChannelMessage (e) && (e.bytes[0] & 0xF0) == 0xC0 && e.bytes.size() > 1)
                return e.bytes[1];
        return 0;
    }

    // importMidi overwrites the name on every 0x03, so the last one wins.
    std::string lastTrackName (const RawMidiTrack& track, std::string fallback)
    {
        for (const auto& e : track.events)
            if (e.bytes.size() >= 2 && e.bytes[0] == 0xFF && e.bytes[1] == 0x03)
                fallback.assign (e.bytes.begin() + 2, e.bytes.end());
        return fallback;
    }

    bool hasEligibleNote (const std::vector<ReplicaNoteOn>& replica)
    {
        for (const auto& on : replica)
            if (on.hasOff && on.offTick > on.onTick)
                return true;
        return false;
    }

    void linkNotes (const Track& songTrack, const RawMidiTrack& rawTrack, PlannedTrack& planned,
                    std::vector<bool>& claimed, Diagnostics& diagnostics)
    {
        const auto replica = replicateJuceNoteOns (rawTrack);

        // importMidi keeps exactly the note-ons JUCE paired with a later off.
        std::vector<int> eligible;
        for (int k = 0; k < (int) replica.size(); ++k)
            if (replica[(size_t) k].hasOff && replica[(size_t) k].offTick > replica[(size_t) k].onTick)
                eligible.push_back (k);

        const auto where = " on MIDI track " + std::to_string (planned.rawTrackIndex);
        if (eligible.size() != songTrack.notes.size())
            throw MidiImportPlanError ("the two MIDI parsers disagree on the note count" + where);

        for (size_t n = 0; n < songTrack.notes.size(); ++n)
        {
            const auto& note = songTrack.notes[n];
            if (note.sourceTrackIndex != planned.rawTrackIndex || note.sourceEventIndex != eligible[n])
                throw MidiImportPlanError ("the two MIDI parsers disagree on note order" + where);

            const auto& on = replica[(size_t) eligible[n]];

            PlannedNoteLink link;
            link.channel        = on.channel;
            link.onOrder        = on.onRawIndex;
            link.offSynthesized = on.offSynthesized;
            claimed[(size_t) on.onRawIndex] = true;

            if (! on.offSynthesized)
            {
                const auto& off = rawTrack.events[(size_t) on.offRawIndex].bytes;
                link.offOrder        = on.offRawIndex;
                link.offIsNoteOnZero = (off[0] & 0xF0) == 0x90;
                link.offVelocity     = off.size() > 2 ? off[2] : 0;
                claimed[(size_t) on.offRawIndex] = true;
            }

            if (note.pitch != on.pitch || note.velocity != on.velocity || note.startTick != on.onTick
                || note.durationTicks != on.offTick - on.onTick || note.isDrum != (on.channel == 10))
            {
                jassertfalse; // the replica disagrees with JUCE: a bug in JuceNoteReplica
                Diagnostic d;
                d.source           = "SongModelBridge";
                d.severity         = Severity::Warning;
                d.message          = "Note link mismatch" + where + "; exported MIDI may differ from the original";
                d.sourceTrackIndex = planned.rawTrackIndex;
                d.sourceEventIndex = eligible[n];
                diagnostics.push_back (std::move (d));
            }

            planned.noteLinks.push_back (link);
        }
    }

    void info (Diagnostics& diagnostics, std::string message)
    {
        Diagnostic d;
        d.source   = "SongModelBridge";
        d.severity = Severity::Info;
        d.message  = std::move (message);
        diagnostics.push_back (std::move (d));
    }
}

bool isSongWideMetaEvent (const RawMidiEvent& event)
{
    if (event.bytes.size() < 2 || event.bytes[0] != 0xFF)
        return false;

    switch (event.bytes[1])
    {
        case 0x51: case 0x58: case 0x59: case 0x54: case 0x06: case 0x02: return true;
        default: return false;
    }
}

bool hasConductorTrack (const RawMidiFile& raw)
{
    if (raw.format != 1 || raw.tracks.empty())
        return false;

    for (const auto& e : raw.tracks.front().events)
        if (isVelocityNoteOn (e))
            return false;
    return true;
}

MidiImportPlan planMidiImport (const Song& song, const RawMidiFile& raw, bool isFirstImport,
                               Diagnostics& diagnostics, const ImportOptions& options)
{
    if (raw.format == 2)
        throw MidiImportPlanError ("MIDI format 2 is not supported");

    Diagnostics local; // only reaches `diagnostics` if planning succeeds
    MidiImportPlan plan;
    // A first import always writes the conductor; so does a replace.
    const bool writeConductor = isFirstImport || options.tempo == TempoMode::replace;
    plan.writesConductor = writeConductor;
    const bool fileHasConductor = hasConductorTrack (raw);

    std::map<int, int> songTrackForRaw;
    for (int s = 0; s < (int) song.tracks.size(); ++s)
    {
        const auto& notes = song.tracks[(size_t) s].notes;
        const int rawIndex = notes.empty() ? -1 : notes.front().sourceTrackIndex;
        if (rawIndex < 0 || rawIndex >= (int) raw.tracks.size() || (fileHasConductor && rawIndex == 0)
            || ! songTrackForRaw.emplace (rawIndex, s).second)
            throw MidiImportPlanError ("the two MIDI parsers disagree on the track count");
    }

    for (int r = 0; r < (int) raw.tracks.size(); ++r)
    {
        const auto& rawTrack = raw.tracks[(size_t) r];

        if (fileHasConductor && r == 0)
        {
            if (writeConductor)
            {
                for (int i = 0; i < (int) rawTrack.events.size(); ++i)
                    plan.conductorEvents.push_back ({ rawTrack.events[(size_t) i].tick, i, -1, rawTrack.events[(size_t) i].bytes });
                plan.conductorEndTick = rawTrack.endTick;
            }
            else
            {
                plan.droppedEventCount += (int) rawTrack.events.size();
            }
            continue;
        }

        PlannedTrack planned;
        planned.rawTrackIndex  = r;
        planned.endTick        = rawTrack.endTick;
        planned.defaultChannel = firstChannel (rawTrack, 1);
        std::vector<bool> claimed (rawTrack.events.size(), false);

        const auto found = songTrackForRaw.find (r);
        if (found != songTrackForRaw.end())
        {
            planned.songTrackIndex = found->second;
            linkNotes (song.tracks[(size_t) found->second], rawTrack, planned, claimed, local);
        }
        else
        {
            if (hasEligibleNote (replicateJuceNoteOns (rawTrack)))
                throw MidiImportPlanError ("the two MIDI parsers disagree about notes on MIDI track " + std::to_string (r));

            planned.name              = lastTrackName (rawTrack, "Track " + std::to_string (r));
            planned.sourceMidiChannel = firstChannel (rawTrack, 0);
            planned.sourceProgram     = firstProgram (rawTrack);
        }

        for (int i = 0; i < (int) rawTrack.events.size(); ++i)
        {
            if (claimed[(size_t) i])
                continue;

            const auto& e = rawTrack.events[(size_t) i];
            if (! fileHasConductor && isSongWideMetaEvent (e))
            {
                if (writeConductor)
                {
                    plan.conductorEvents.push_back ({ e.tick, i, r, e.bytes });
                    ++plan.relocatedEventCount;
                }
                else
                {
                    ++plan.droppedEventCount;
                }
                continue;
            }

            planned.events.push_back ({ e.tick, i, -1, e.bytes });
        }

        plan.tracks.push_back (std::move (planned));
    }

    if (plan.relocatedEventCount > 0)
        info (local, "Moved " + std::to_string (plan.relocatedEventCount)
                     + " song-wide event(s) (tempo, meter, key, SMPTE, marker, copyright) into the conductor track");
    if (plan.droppedEventCount > 0)
        info (local, "Dropped " + std::to_string (plan.droppedEventCount)
                     + " song-wide event(s) from a later import; the first import's conductor track is kept");

    if (! isFirstImport && options.tempo == TempoMode::replace)
        info (local, "Replaced the Song's tempo map and conductor events with the imported file's");
    diagnostics.insert (diagnostics.end(), local.begin(), local.end());
    return plan;
}

} // namespace lotro

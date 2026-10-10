#include "UI/NoteMerge.h"

#include <algorithm>

namespace lotro
{

namespace
{
    bool isMergeTrack (const juce::ValueTree& track)
    {
        return track.hasType (SongIDs::MIDI_TRACK) && ! (bool) track.getProperty (SongIDs::isConductor, false);
    }

    struct Carried
    {
        juce::ValueTree note;
        juce::ValueTree sourceTrack;
    };

    // The notes of every ref'd section once each (id 0 reads as the track's first
    // section), skipping refs on the target, the conductor and unknown tracks/sections.
    std::vector<Carried> carriedNotes (const SongDocument& doc, const std::vector<SectionRef>& refs,
                                       juce::int64 targetTrackId)
    {
        std::vector<Carried> out;
        std::vector<SectionRef> seen;
        for (const auto& r : refs)
        {
            const auto track = doc.findTrackById (r.trackId);
            if (! isMergeTrack (track) || r.trackId == targetTrackId)
                continue;
            const auto sections = sectionsOf (track);
            if (sections.empty())
                continue;
            const auto id = r.sectionId == 0 ? sections.front().id : r.sectionId;
            if (std::none_of (sections.begin(), sections.end(), [id] (const SectionRange& s) { return s.id == id; }))
                continue;
            const SectionRef key { r.trackId, id };
            if (std::find (seen.begin(), seen.end(), key) != seen.end())
                continue;
            seen.push_back (key);
            for (auto note : notesInSection (track, id))
                out.push_back ({ note, track });
        }
        return out;
    }

    int startOf (const juce::ValueTree& n) { return (int) n.getProperty (SongIDs::startTick); }
    int endOf (const juce::ValueTree& n) { return startOf (n) + (int) n.getProperty (SongIDs::durationTicks); }

    // A detached copy of `src` as new material for `target`.
    juce::ValueTree makeNote (const juce::ValueTree& src, int start, int end, const juce::ValueTree& target)
    {
        auto n = src.createCopy();
        for (const auto* id : { &SongIDs::onOrder, &SongIDs::offOrder, &SongIDs::sectionId })
            n.removeProperty (*id, nullptr);
        const int channel = (int) target.getProperty (SongIDs::defaultChannel, 1);
        n.setProperty (SongIDs::startTick, start, nullptr);
        n.setProperty (SongIDs::durationTicks, end - start, nullptr);
        n.setProperty (SongIDs::channel, channel, nullptr);
        n.setProperty (SongIDs::isDrum, channel == 10, nullptr);
        n.setProperty (SongIDs::sourceTrackIndex, -1, nullptr);
        n.setProperty (SongIDs::sourceEventIndex, -1, nullptr);
        n.setProperty (SongIDs::offSynthesized, false, nullptr);

        const auto stored = target.getChildWithName (SongIDs::SECTIONS);
        if (stored.getNumChildren() > 0)
        {
            const auto id = sectionIdOfNote (n, sectionsOf (target));
            if (id != 0)
                n.setProperty (SongIDs::sectionId, id, nullptr);
        }
        return n;
    }
}

bool canMergeInto (const SongDocument& doc, const std::vector<SectionRef>& refs, juce::int64 targetTrackId)
{
    if (! isMergeTrack (doc.findTrackById (targetTrackId)))
        return false;
    return std::any_of (refs.begin(), refs.end(), [&] (const SectionRef& r)
    {
        return r.trackId != targetTrackId && isMergeTrack (doc.findTrackById (r.trackId));
    });
}

MergeResult mergeSections (SongDocument& doc, const std::vector<SectionRef>& refs,
                           juce::int64 targetTrackId, bool copy)
{
    MergeResult result;
    const auto target = doc.findTrackById (targetTrackId);
    if (! isMergeTrack (target))
        return result;

    auto carried = carriedNotes (doc, refs, targetTrackId);
    if (carried.empty())
        return result;
    std::stable_sort (carried.begin(), carried.end(),
                      [] (const Carried& a, const Carried& b) { return startOf (a.note) < startOf (b.note); });

    // Plan on plain data first, so a call that changes nothing never opens a transaction.
    result.inserted = (int) carried.size();
    result.changed = true;

    doc.getUndoManager().beginNewTransaction();
    auto targetNotes = SongDocument::getNotesNode (target);
    for (const auto& c : carried)
        doc.addChild (targetNotes, makeNote (c.note, startOf (c.note), endOf (c.note), target), false);
    if (! copy)
        for (const auto& c : carried)
            doc.removeChild (SongDocument::getNotesNode (c.sourceTrack), c.note, false);
    return result;
}

} // namespace lotro

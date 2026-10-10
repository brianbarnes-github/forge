#include "UI/NoteMerge.h"

#include <algorithm>
#include <map>

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

    // One span of a given pitch in the target while planning: an existing NOTE
    // (node valid) or a note to be created (node invalid).
    struct Item
    {
        juce::ValueTree node;
        juce::ValueTree source;   // the carried note a new item is built from
        int start = 0;
        int end = 0;
        bool removed = false;
        bool grown = false;       // an existing item whose span changed
    };

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
    auto targetNotes = SongDocument::getNotesNode (target);
    std::map<int, std::vector<Item>> byPitch;
    for (int i = 0; i < targetNotes.getNumChildren(); ++i)
    {
        const auto n = targetNotes.getChild (i);
        byPitch[(int) n.getProperty (SongIDs::pitch)].push_back ({ n, {}, startOf (n), endOf (n) });
    }

    for (const auto& c : carried)
    {
        const int s = startOf (c.note), e = endOf (c.note);
        auto& items = byPitch[(int) c.note.getProperty (SongIDs::pitch)];

        std::vector<size_t> hits;   // live spans this note overlaps or touches
        for (size_t i = 0; i < items.size(); ++i)
            if (! items[i].removed && items[i].start <= e && s <= items[i].end)
                hits.push_back (i);

        if (hits.empty())
        {
            items.push_back ({ {}, c.note, s, e });
            ++result.inserted;
            continue;
        }

        if (std::any_of (hits.begin(), hits.end(), [&] (size_t i) { return items[i].start <= s && e <= items[i].end; }))
        {
            ++result.dropped;   // already inside one
            continue;
        }

        int unionStart = s, unionEnd = e;
        size_t earliest = hits.front();
        for (const auto i : hits)
        {
            unionStart = std::min (unionStart, items[i].start);
            unionEnd = std::max (unionEnd, items[i].end);
            if (items[i].start < items[earliest].start)
                earliest = i;
        }

        if (s < items[earliest].start)
        {
            // The carried note starts first: its properties win, so it replaces what it joined.
            for (const auto i : hits)
                items[i].removed = true;
            items.push_back ({ {}, c.note, unionStart, unionEnd });
        }
        else
        {
            // The earliest existing span keeps its properties and absorbs the rest.
            for (const auto i : hits)
                if (i != earliest)
                    items[i].removed = true;
            items[earliest].start = unionStart;
            items[earliest].end = unionEnd;
            items[earliest].grown = true;
        }
        ++result.extended;
    }

    result.changed = result.inserted > 0 || result.extended > 0 || (! copy && ! carried.empty());
    if (! result.changed)
        return result;

    doc.getUndoManager().beginNewTransaction();
    for (auto& [pitch, items] : byPitch)
    {
        for (auto& item : items)
        {
            if (item.removed)
            {
                if (item.node.isValid())
                    doc.removeChild (targetNotes, item.node, false);
            }
            else if (! item.node.isValid())
            {
                doc.addChild (targetNotes, makeNote (item.source, item.start, item.end, target), false);
            }
            else if (item.grown)
            {
                doc.setProperty (item.node, SongIDs::startTick, item.start, false);
                doc.setProperty (item.node, SongIDs::durationTicks, item.end - item.start, false);
                markNoteTimingEdited (doc, item.node);
            }
        }
    }
    if (! copy)
        for (const auto& c : carried)
            doc.removeChild (SongDocument::getNotesNode (c.sourceTrack), c.note, false);
    return result;
}

} // namespace lotro

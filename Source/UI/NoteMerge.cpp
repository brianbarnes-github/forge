#include "UI/NoteMerge.h"

#include <algorithm>
#include <cstdint>
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

    struct CarriedEvent
    {
        juce::ValueTree event;
        juce::ValueTree sourceTrack;
    };

    std::vector<std::uint8_t> bytesOf (const juce::ValueTree& event)
    {
        if (const auto* block = event.getProperty (SongIDs::data).getBinaryData())
        {
            const auto* d = static_cast<const std::uint8_t*> (block->getData());
            return std::vector<std::uint8_t> (d, d + block->getSize());
        }
        return {};
    }

    // Notes (and the importer's stray note pairs), End-of-Track and track-name metas stay put.
    bool travels (const std::vector<std::uint8_t>& b)
    {
        if (b.empty())
            return false;
        const int kind = b[0] & 0xF0;
        if (kind == 0x80 || kind == 0x90)
            return false;
        if (b[0] == 0xFF && b.size() >= 2 && (b[1] == 0x2F || b[1] == 0x03))
            return false;
        return true;
    }

    // The travelling events inside every ref'd section's [start, end), once each,
    // with the same ref rules as carriedNotes.
    std::vector<CarriedEvent> carriedEvents (const SongDocument& doc, const std::vector<SectionRef>& refs,
                                             juce::int64 targetTrackId)
    {
        std::vector<CarriedEvent> out;
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
            const auto found = std::find_if (sections.begin(), sections.end(), [id] (const SectionRange& s) { return s.id == id; });
            if (found == sections.end())
                continue;
            const SectionRef key { r.trackId, id };
            if (std::find (seen.begin(), seen.end(), key) != seen.end())
                continue;
            seen.push_back (key);

            const auto events = SongDocument::getEventsNode (track);
            for (int i = 0; i < events.getNumChildren(); ++i)
            {
                const auto event = events.getChild (i);
                const int tick = (int) event.getProperty (SongIDs::tick);
                if (tick < found->startTick || tick >= found->endTick || ! travels (bytesOf (event)))
                    continue;
                if (std::any_of (out.begin(), out.end(), [&] (const CarriedEvent& c) { return c.event == event; }))
                    continue;   // two overlapping sections hold it
                out.push_back ({ event, track });
            }
        }
        return out;
    }

    int nextEventOrder (const juce::ValueTree& track)
    {
        int next = 0;
        const auto events = SongDocument::getEventsNode (track);
        for (int i = 0; i < events.getNumChildren(); ++i)
            next = std::max (next, (int) events.getChild (i).getProperty (SongIDs::order, 0) + 1);
        return next;
    }

    // A detached copy of `src` for `target`: a channel message is re-addressed to the
    // target's channel (the notes inserted by the merge take it too).
    juce::ValueTree makeEvent (const juce::ValueTree& src, const juce::ValueTree& target, int order)
    {
        auto e = src.createCopy();
        e.removeProperty (SongIDs::relocatedFrom, nullptr);
        e.setProperty (SongIDs::order, order, nullptr);
        auto bytes = bytesOf (src);
        if ((bytes[0] & 0xF0) >= 0x80 && (bytes[0] & 0xF0) <= 0xE0)
        {
            const int channel = (int) target.getProperty (SongIDs::defaultChannel, 1);
            bytes[0] = (std::uint8_t) ((bytes[0] & 0xF0) | ((channel - 1) & 0x0F));
        }
        e.setProperty (SongIDs::data, juce::var (juce::MemoryBlock (bytes.data(), bytes.size())), nullptr);
        return e;
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
                           juce::int64 targetTrackId, bool copy, MergeScope scope)
{
    MergeResult result;
    const auto target = doc.findTrackById (targetTrackId);
    if (! isMergeTrack (target))
        return result;

    auto carried = carriedNotes (doc, refs, targetTrackId);
    const auto events = scope == MergeScope::allEvents ? carriedEvents (doc, refs, targetTrackId)
                                                       : std::vector<CarriedEvent> {};
    if (carried.empty() && events.empty())
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

    result.eventsCarried = (int) events.size();
    result.changed = result.inserted > 0 || result.extended > 0 || (! copy && ! carried.empty()) || ! events.empty();
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

    if (! events.empty())
    {
        auto targetEvents = SongDocument::getEventsNode (target);
        int order = nextEventOrder (target);
        for (const auto& c : events)
            doc.addChild (targetEvents, makeEvent (c.event, target, order++), false);
        if (! copy)
            for (const auto& c : events)
                doc.removeChild (SongDocument::getEventsNode (c.sourceTrack), c.event, false);
    }
    return result;
}

} // namespace lotro

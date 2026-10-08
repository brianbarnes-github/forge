#include "UI/SectionEdit.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace lotro
{

namespace
{
    std::vector<SectionRange> storedSections (const juce::ValueTree& track)
    {
        std::vector<SectionRange> out;
        const auto node = track.getChildWithName (SongIDs::SECTIONS);
        for (int i = 0; i < node.getNumChildren(); ++i)
        {
            const auto s = node.getChild (i);
            out.push_back ({ (juce::int64) s.getProperty (SongIDs::sectionId, 0),
                             (int) s.getProperty (SongIDs::startTick, 0),
                             (int) s.getProperty (SongIDs::endTick, 0) });
        }
        return out;
    }

    juce::ValueTree findSectionNode (const juce::ValueTree& track, juce::int64 id)
    {
        const auto node = track.getChildWithName (SongIDs::SECTIONS);
        for (int i = 0; i < node.getNumChildren(); ++i)
            if ((juce::int64) node.getChild (i).getProperty (SongIDs::sectionId, 0) == id)
                return node.getChild (i);
        return {};
    }

    // Non-undoable: gives a track real stored sections and tags every note.
    void materialise (SongDocument& doc, juce::ValueTree track)
    {
        if (! track.isValid() || (bool) track.getProperty (SongIDs::isConductor, false))
            return;

        auto sectionsNode = track.getChildWithName (SongIDs::SECTIONS);
        if (! sectionsNode.isValid())
        {
            sectionsNode = juce::ValueTree (SongIDs::SECTIONS);
            track.addChild (sectionsNode, -1, nullptr);
        }

        if (sectionsNode.getNumChildren() == 0)
        {
            const auto virtualSections = sectionsOf (track);   // empty when there are no notes
            for (const auto& v : virtualSections)
            {
                juce::ValueTree s (SongIDs::SECTION);
                s.setProperty (SongIDs::sectionId, doc.mintSectionId(), nullptr);
                s.setProperty (SongIDs::startTick, v.startTick, nullptr);
                s.setProperty (SongIDs::endTick, v.endTick, nullptr);
                sectionsNode.addChild (s, -1, nullptr);
            }
        }

        const auto sections = storedSections (track);
        auto notes = SongDocument::getNotesNode (track);
        for (int i = 0; i < notes.getNumChildren(); ++i)
        {
            auto note = notes.getChild (i);
            const auto id = sectionIdOfNote (note, sections);
            if (id != 0 && (juce::int64) note.getProperty (SongIDs::sectionId, 0) != id)
                note.setProperty (SongIDs::sectionId, id, nullptr);
        }
    }

    struct Target
    {
        juce::ValueTree track;
        SectionRange range;
    };

    // Read-only: the sections `refs` name, once each, with id 0 read as the
    // track's first section (the virtual one on an unmaterialised track).
    // Skips the conductor, unknown tracks and unknown sections.
    std::vector<Target> targetsOf (const SongDocument& doc, const std::vector<SectionRef>& refs)
    {
        std::vector<Target> out;
        for (const auto& r : refs)
        {
            const auto track = doc.findTrackById (r.trackId);
            const auto sections = sectionsOf (track);   // empty for the conductor and an invalid track
            if (sections.empty())
                continue;

            const auto id = r.sectionId == 0 ? sections.front().id : r.sectionId;
            const auto found = std::find_if (sections.begin(), sections.end(), [id] (const SectionRange& s) { return s.id == id; });
            if (found == sections.end())
                continue;
            if (std::any_of (out.begin(), out.end(), [&] (const Target& o) { return o.track == track && o.range.id == id; }))
                continue;

            out.push_back ({ track, *found });
        }
        return out;
    }

    // Materialises the tracks of `targets` (and no others), then swaps a virtual
    // section's id 0 for the id it was minted. The set of sections is exactly the
    // one the read-only check saw: a stale ref id that materialising happens to
    // mint cannot join it.
    std::vector<Target> resolve (SongDocument& doc, std::vector<Target> targets)
    {
        for (auto& t : targets)
        {
            materialise (doc, t.track);
            if (t.range.id == 0)
                t.range.id = storedSections (t.track).front().id;
        }
        return targets;
    }

    std::vector<juce::ValueTree> membersOf (const juce::ValueTree& track, juce::int64 sectionId)
    {
        const auto sections = sectionsOf (track);
        std::vector<juce::ValueTree> out;
        const auto notes = SongDocument::getNotesNode (track);
        for (int i = 0; i < notes.getNumChildren(); ++i)
            if (sectionIdOfNote (notes.getChild (i), sections) == sectionId)
                out.push_back (notes.getChild (i));
        return out;
    }

    bool containsStrictly (const std::vector<SectionRange>& sections, int tick)
    {
        return std::any_of (sections.begin(), sections.end(),
                            [tick] (const SectionRange& s) { return s.startTick < tick && tick < s.endTick; });
    }
}

std::vector<SectionRange> sectionsOf (const juce::ValueTree& track)
{
    if (! track.isValid() || (bool) track.getProperty (SongIDs::isConductor, false))
        return {};

    auto stored = storedSections (track);
    if (! stored.empty())
        return stored;

    const auto notes = SongDocument::getNotesNode (track);
    if (notes.getNumChildren() == 0)
        return {};

    int lastEnd = 1;
    for (int i = 0; i < notes.getNumChildren(); ++i)
    {
        const auto n = notes.getChild (i);
        lastEnd = std::max (lastEnd, (int) n.getProperty (SongIDs::startTick) + (int) n.getProperty (SongIDs::durationTicks));
    }
    return { { 0, 0, lastEnd } };
}

juce::int64 sectionIdOfNote (const juce::ValueTree& note, const std::vector<SectionRange>& sections)
{
    if (sections.empty())
        return 0;

    const auto tag = (juce::int64) note.getProperty (SongIDs::sectionId, 0);
    if (std::any_of (sections.begin(), sections.end(), [tag] (const SectionRange& s) { return tag != 0 && s.id == tag; }))
        return tag;

    // Nearest section by distance from the note's start to [startTick, endTick)
    // (0 inside); ties go to the earlier startTick, then stored order.
    const int start = (int) note.getProperty (SongIDs::startTick, 0);
    auto distance = [start] (const SectionRange& s)
    {
        return start < s.startTick ? s.startTick - start : (start >= s.endTick ? start - s.endTick + 1 : 0);
    };
    const SectionRange* best = &sections.front();
    for (const auto& s : sections)
        if (distance (s) < distance (*best) || (distance (s) == distance (*best) && s.startTick < best->startTick))
            best = &s;
    return best->id;
}

SectionHit hitTestSection (const std::vector<SectionRange>& sections, int tick, double pixelsPerTick, int edgeSlopPixels)
{
    SectionHit best;
    double bestDistance = 1.0e300;
    bool bestContains = false;

    // Nearer wins; at equal distance (abutting sections share a boundary) the edge
    // of the section whose half-open body holds the tick wins, else the first.
    auto consider = [&] (const SectionRange& s, double distance, SectionZone zone)
    {
        const bool contains = s.startTick <= tick && tick < s.endTick;
        if (distance < bestDistance || (distance == bestDistance && contains && ! bestContains))
        {
            best = { s.id, zone };
            bestDistance = distance;
            bestContains = contains;
        }
    };

    for (const auto& s : sections)
    {
        const double toStart = std::abs ((double) (tick - s.startTick)) * pixelsPerTick;
        const double toEnd = std::abs ((double) (tick - s.endTick)) * pixelsPerTick;
        if (toStart <= edgeSlopPixels && tick <= s.endTick)
            consider (s, toStart, SectionZone::LeftEdge);
        if (toEnd <= edgeSlopPixels && tick >= s.startTick)
            consider (s, toEnd, SectionZone::RightEdge);
    }
    if (best.zone != SectionZone::None)
        return best;

    int narrowest = std::numeric_limits<int>::max();
    for (const auto& s : sections)
        if (s.startTick <= tick && tick < s.endTick && s.endTick - s.startTick < narrowest)
        {
            narrowest = s.endTick - s.startTick;
            best = { s.id, SectionZone::Body };
        }
    return best;
}

void markNoteTimingEdited (SongDocument& doc, juce::ValueTree note)
{
    if (note.hasProperty (SongIDs::onOrder))
        doc.removeProperty (note, SongIDs::onOrder, false);
    if (note.hasProperty (SongIDs::offOrder))
        doc.removeProperty (note, SongIDs::offOrder, false);
    if ((bool) note.getProperty (SongIDs::offSynthesized, false))
        doc.setProperty (note, SongIDs::offSynthesized, false, false);
}

void splitAt (SongDocument& doc, const std::vector<juce::int64>& trackIds, int tick)
{
    std::vector<juce::ValueTree> tracks;
    for (const auto id : trackIds)
    {
        auto track = doc.findTrackById (id);
        if (track.isValid() && ! (bool) track.getProperty (SongIDs::isConductor, false)
            && containsStrictly (sectionsOf (track), tick))
            tracks.push_back (track);
    }
    if (tracks.empty())
        return;

    for (auto track : tracks)
        materialise (doc, track);

    doc.getUndoManager().beginNewTransaction();

    for (auto track : tracks)
    {
        auto sectionsNode = track.getChildWithName (SongIDs::SECTIONS);
        auto notesNode = SongDocument::getNotesNode (track);

        for (const auto& s : storedSections (track))
        {
            if (! (s.startTick < tick && tick < s.endTick))
                continue;

            const auto members = membersOf (track, s.id);
            const auto newId = doc.mintSectionId();

            juce::ValueTree right (SongIDs::SECTION);
            right.setProperty (SongIDs::sectionId, newId, nullptr);
            right.setProperty (SongIDs::startTick, tick, nullptr);
            right.setProperty (SongIDs::endTick, s.endTick, nullptr);
            doc.setProperty (findSectionNode (track, s.id), SongIDs::endTick, tick, false);
            doc.addChild (sectionsNode, right, false);

            for (auto note : members)
            {
                const int start = (int) note.getProperty (SongIDs::startTick);
                const int end = start + (int) note.getProperty (SongIDs::durationTicks);
                if (start >= tick)
                {
                    doc.setProperty (note, SongIDs::sectionId, newId, false);
                }
                else if (end > tick)
                {
                    auto tail = note.createCopy();
                    tail.setProperty (SongIDs::startTick, tick, nullptr);
                    tail.setProperty (SongIDs::durationTicks, end - tick, nullptr);
                    tail.setProperty (SongIDs::sectionId, newId, nullptr);
                    for (const auto& p : { SongIDs::onOrder, SongIDs::offOrder })
                        tail.removeProperty (p, nullptr);
                    tail.setProperty (SongIDs::offSynthesized, false, nullptr);
                    doc.addChild (notesNode, tail, false);

                    doc.setProperty (note, SongIDs::durationTicks, tick - start, false);
                    markNoteTimingEdited (doc, note);
                }
            }
        }
    }
}

void moveSections (SongDocument& doc, const std::vector<SectionRef>& refs, int deltaTicks)
{
    // The clamp covers member notes too: a note outside every section belongs to the nearest.
    const auto before = targetsOf (doc, refs);
    int lowest = std::numeric_limits<int>::max();
    for (const auto& t : before)
    {
        lowest = std::min (lowest, t.range.startTick);
        for (const auto& note : membersOf (t.track, t.range.id))
            lowest = std::min (lowest, (int) note.getProperty (SongIDs::startTick));
    }
    if (lowest == std::numeric_limits<int>::max())
        return;

    const int delta = std::max (deltaTicks, -std::max (0, lowest));
    if (delta == 0)
        return;

    const auto targets = resolve (doc, before);
    doc.getUndoManager().beginNewTransaction();
    for (const auto& t : targets)
    {
        const auto members = membersOf (t.track, t.range.id);   // before the range moves
        auto node = findSectionNode (t.track, t.range.id);
        doc.setProperty (node, SongIDs::startTick, t.range.startTick + delta, false);
        doc.setProperty (node, SongIDs::endTick, t.range.endTick + delta, false);
        for (auto note : members)
        {
            doc.setProperty (note, SongIDs::startTick, (int) note.getProperty (SongIDs::startTick) + delta, false);
            markNoteTimingEdited (doc, note);
        }
    }
}

namespace
{
    // Moves `edge` of each section to targetTick (its range), at least 1 tick wide.
    void resizeEachTo (SongDocument& doc, const std::vector<SectionRef>& refs, SectionEdge edge,
                       const std::function<int (const SectionRange&)>& targetTick)
    {
        auto resized = [edge, &targetTick] (const SectionRange& r)
        {
            SectionRange out = r;
            if (edge == SectionEdge::Left)
                out.startTick = std::clamp (targetTick (r), 0, r.endTick - 1);
            else
                out.endTick = std::max (targetTick (r), r.startTick + 1);
            return out;
        };
        std::vector<Target> changed;
        for (const auto& t : targetsOf (doc, refs))
        {
            const auto r = resized (t.range);
            if (r.startTick != t.range.startTick || r.endTick != t.range.endTick)
                changed.push_back (t);
        }
        if (changed.empty())
            return;

        const auto targets = resolve (doc, changed);
        doc.getUndoManager().beginNewTransaction();
        for (const auto& t : targets)
        {
            const auto old = t.range;
            const auto r = resized (old);
            const auto members = membersOf (t.track, old.id);
            auto notesNode = SongDocument::getNotesNode (t.track);
            auto node = findSectionNode (t.track, old.id);
            doc.setProperty (node, SongIDs::startTick, r.startTick, false);
            doc.setProperty (node, SongIDs::endTick, r.endTick, false);

            // Only a shrinking edge touches notes, and only those starting in the band
            // it gives up (or, on the right, crossing into it). Growing only extends the
            // range: a member lying outside it (drawn in a gap) is never touched.
            for (auto note : members)
            {
                const int start = (int) note.getProperty (SongIDs::startTick);
                const int end = start + (int) note.getProperty (SongIDs::durationTicks);

                if (r.startTick > old.startTick && old.startTick <= start && start < r.startTick)
                {
                    if (end <= r.startTick)
                    {
                        doc.removeChild (notesNode, note, false);
                    }
                    else
                    {
                        doc.setProperty (note, SongIDs::startTick, r.startTick, false);
                        doc.setProperty (note, SongIDs::durationTicks, end - r.startTick, false);
                        markNoteTimingEdited (doc, note);
                    }
                }
                else if (r.endTick < old.endTick && r.endTick <= start && start < old.endTick)
                {
                    doc.removeChild (notesNode, note, false);
                }
                else if (r.endTick < old.endTick && old.startTick <= start && start < r.endTick && end > r.endTick)
                {
                    doc.setProperty (note, SongIDs::durationTicks, r.endTick - start, false);
                    markNoteTimingEdited (doc, note);
                }
            }
        }
    }
}

void resizeSections (SongDocument& doc, const std::vector<SectionRef>& refs, SectionEdge edge, int tick)
{
    resizeEachTo (doc, refs, edge, [tick] (const SectionRange&) { return tick; });
}

void resizeSectionsBy (SongDocument& doc, const std::vector<SectionRef>& refs, SectionEdge edge, int deltaTicks)
{
    resizeEachTo (doc, refs, edge, [edge, deltaTicks] (const SectionRange& r)
    {
        return (edge == SectionEdge::Left ? r.startTick : r.endTick) + deltaTicks;
    });
}

void deleteSections (SongDocument& doc, const std::vector<SectionRef>& refs)
{
    const auto before = targetsOf (doc, refs);
    if (before.empty())
        return;

    const auto targets = resolve (doc, before);
    doc.getUndoManager().beginNewTransaction();
    for (const auto& t : targets)
    {
        auto notesNode = SongDocument::getNotesNode (t.track);
        for (auto note : membersOf (t.track, t.range.id))
            doc.removeChild (notesNode, note, false);
        auto sectionsNode = t.track.getChildWithName (SongIDs::SECTIONS);
        doc.removeChild (sectionsNode, findSectionNode (t.track, t.range.id), false);
    }
}

} // namespace lotro

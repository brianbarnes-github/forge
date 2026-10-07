#include "UI/SectionEdit.h"

#include <algorithm>
#include <cmath>
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

    const int start = (int) note.getProperty (SongIDs::startTick, 0);
    for (const auto& s : sections)
        if (s.startTick <= start && start < s.endTick)
            return s.id;
    return sections.front().id;
}

SectionHit hitTestSection (const std::vector<SectionRange>& sections, int tick, double pixelsPerTick, int edgeSlopPixels)
{
    SectionHit best;
    double bestDistance = 1.0e300;

    for (const auto& s : sections)
    {
        const double toStart = std::abs ((double) (tick - s.startTick)) * pixelsPerTick;
        const double toEnd = std::abs ((double) (tick - s.endTick)) * pixelsPerTick;
        if (toStart <= edgeSlopPixels && toStart < bestDistance && tick <= s.endTick)
        {
            best = { s.id, SectionZone::LeftEdge };
            bestDistance = toStart;
        }
        if (toEnd <= edgeSlopPixels && toEnd < bestDistance && tick >= s.startTick)
        {
            best = { s.id, SectionZone::RightEdge };
            bestDistance = toEnd;
        }
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

std::vector<SectionRef> withCompanions (const SongDocument& doc, const std::set<juce::int64>& selectedTrackIds,
                                        SectionRef clicked)
{
    std::vector<SectionRef> out { clicked };
    if (selectedTrackIds.count (clicked.trackId) == 0)
        return out;

    const auto clickedTrack = doc.findTrackById (clicked.trackId);
    int clickedStart = -1;
    for (const auto& s : sectionsOf (clickedTrack))
        if (s.id == clicked.sectionId)
            clickedStart = s.startTick;
    if (clickedStart < 0)
        return out;

    for (const auto trackId : selectedTrackIds)
    {
        if (trackId == clicked.trackId)
            continue;
        for (const auto& s : sectionsOf (doc.findTrackById (trackId)))
            if (s.startTick == clickedStart)
                out.push_back ({ trackId, s.id });
    }
    return out;
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

} // namespace lotro

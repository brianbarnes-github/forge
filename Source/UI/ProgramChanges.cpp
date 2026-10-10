#include "UI/ProgramChanges.h"

#include <algorithm>
#include <cstdint>

namespace lotro
{

std::vector<ProgramChange> programChangesOf (const juce::ValueTree& track)
{
    std::vector<ProgramChange> out;
    if (! track.isValid() || (bool) track.getProperty (SongIDs::isConductor, false))
        return out;

    const auto events = SongDocument::getEventsNode (track);
    for (int i = 0; i < events.getNumChildren(); ++i)
    {
        const auto event = events.getChild (i);
        const auto* block = event.getProperty (SongIDs::data).getBinaryData();
        if (block == nullptr || block->getSize() != 2)
            continue;
        const auto* d = static_cast<const std::uint8_t*> (block->getData());
        if ((d[0] & 0xF0) != 0xC0)
            continue;
        out.push_back ({ (int) event.getProperty (SongIDs::tick), (d[0] & 0x0F) + 1, d[1] & 0x7F, event });
    }
    std::stable_sort (out.begin(), out.end(), [] (const ProgramChange& a, const ProgramChange& b)
    {
        if (a.tick != b.tick)
            return a.tick < b.tick;
        return (int) a.event.getProperty (SongIDs::order, 0) < (int) b.event.getProperty (SongIDs::order, 0);
    });
    return out;
}

std::vector<InstrumentSegment> instrumentSegmentsOf (const juce::ValueTree& track)
{
    if (! track.isValid() || (bool) track.getProperty (SongIDs::isConductor, false))
        return {};

    const auto changes = programChangesOf (track);
    std::vector<InstrumentSegment> out;
    const auto push = [&out] (int start, int program)
    {
        if (! out.empty() && out.back().startTick == start)
            out.pop_back();
        if (! out.empty() && out.back().program == program)
            return;
        out.push_back ({ start, 0, program });
    };

    if (changes.empty() || changes.front().tick > 0)
        push (0, (int) track.getProperty (SongIDs::sourceProgram, 0));
    for (const auto& c : changes)
        push (c.tick, c.program);

    int endTick = (int) track.getProperty (SongIDs::endTick, 0);
    const auto notes = SongDocument::getNotesNode (track);
    for (int i = 0; i < notes.getNumChildren(); ++i)
    {
        const auto n = notes.getChild (i);
        endTick = std::max (endTick, (int) n.getProperty (SongIDs::startTick) + (int) n.getProperty (SongIDs::durationTicks));
    }

    for (size_t i = 0; i < out.size(); ++i)
        out[i].endTick = i + 1 < out.size() ? out[i + 1].startTick : std::max (endTick, out[i].startTick + 1);
    return out;
}

std::vector<BandSegment> bandSegments (const std::vector<InstrumentSegment>& segments, const TimelineViewState& view)
{
    std::vector<BandSegment> out;
    for (const auto& s : segments)
    {
        const int x0 = view.xForTick (s.startTick);
        out.push_back ({ x0, std::max (x0 + 1, view.xForTick (s.endTick)), s.program });
    }
    return out;
}

} // namespace lotro

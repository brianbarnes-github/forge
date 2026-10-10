#include "UI/InstrumentEdit.h"

#include "UI/ProgramChanges.h"
#include "UI/SectionEdit.h"

#include <algorithm>
#include <cstdint>
#include <set>

namespace lotro
{

namespace
{
    juce::var programBytes (int channel, int program)
    {
        const std::uint8_t bytes[2] { (std::uint8_t) (0xC0 | ((channel - 1) & 0x0F)), (std::uint8_t) program };
        return juce::var (juce::MemoryBlock (bytes, 2));
    }

    int nextEventOrder (const juce::ValueTree& track)
    {
        int next = 0;
        const auto events = SongDocument::getEventsNode (track);
        for (int i = 0; i < events.getNumChildren(); ++i)
            next = std::max (next, (int) events.getChild (i).getProperty (SongIDs::order, 0) + 1);
        return next;
    }
}

bool canAutoSplit (const juce::ValueTree& track)
{
    return instrumentSegmentsOf (track).size() >= 2;
}

void autoSplitOnInstrumentChange (SongDocument& doc, juce::int64 trackId)
{
    const auto segments = instrumentSegmentsOf (doc.findTrackById (trackId));
    std::vector<int> ticks;
    for (size_t i = 1; i < segments.size(); ++i)
        ticks.push_back (segments[i].startTick);
    splitAtTicks (doc, trackId, ticks);
}

void setTrackInstrument (SongDocument& doc, juce::int64 trackId, int program)
{
    auto track = doc.findTrackById (trackId);
    if (! track.isValid() || (bool) track.getProperty (SongIDs::isConductor, false))
        return;
    program = std::clamp (program, 0, 127);

    const auto changes = programChangesOf (track);
    std::set<int> seenChannels;
    std::vector<ProgramChange> keep, drop;
    for (const auto& c : changes)
        (seenChannels.insert (c.channel).second ? keep : drop).push_back (c);

    const bool sourceDiffers = (int) track.getProperty (SongIDs::sourceProgram, 0) != program;
    const bool keptDiffers = std::any_of (keep.begin(), keep.end(), [program] (const ProgramChange& c) { return c.program != program; });
    if (! sourceDiffers && ! keptDiffers && drop.empty())
        return;

    doc.getUndoManager().beginNewTransaction();
    auto events = SongDocument::getEventsNode (track);
    for (const auto& c : keep)
        if (c.program != program)
            doc.setProperty (c.event, SongIDs::data, programBytes (c.channel, program), false);
    for (const auto& c : drop)
        doc.removeChild (events, c.event, false);

    if (changes.empty())
    {
        juce::ValueTree event (SongIDs::EVENT);
        event.setProperty (SongIDs::tick, 0, nullptr);
        event.setProperty (SongIDs::order, nextEventOrder (track), nullptr);
        event.setProperty (SongIDs::data, programBytes ((int) track.getProperty (SongIDs::defaultChannel, 1), program), nullptr);
        doc.addChild (events, event, false);
    }
    if (sourceDiffers)
        doc.setProperty (track, SongIDs::sourceProgram, program, false);
}

} // namespace lotro

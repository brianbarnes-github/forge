#include "MidiExport.h"

#include <algorithm>
#include <set>
#include <tuple>

namespace lotro
{

namespace
{
    // Sort key within one track: tick, then group (0 = new note-off,
    // 1 = original material, 2 = new note-on), then relocatedFrom, then
    // original order. Stable sort keeps document order for exact ties.
    struct Item
    {
        int tick = 0;
        int group = 1;
        int relocatedFrom = -1;
        int order = 0;
        std::vector<std::uint8_t> bytes;

        auto key() const { return std::make_tuple (tick, group, relocatedFrom, order); }
    };

    std::vector<std::uint8_t> blockBytes (const juce::var& v)
    {
        if (const auto* block = v.getBinaryData())
        {
            const auto* d = static_cast<const std::uint8_t*> (block->getData());
            return std::vector<std::uint8_t> (d, d + block->getSize());
        }
        return {};
    }

    bool isNoteOn (const std::vector<std::uint8_t>& bytes)
    {
        return bytes.size() >= 3 && (bytes[0] & 0xF0) == 0x90 && bytes[2] > 0;
    }

    RawMidiTrack buildTrack (const juce::ValueTree& track)
    {
        std::vector<Item> items;

        for (auto event : SongDocument::getEventsNode (track))
        {
            Item item;
            item.tick          = (int) event.getProperty (SongIDs::tick);
            item.order         = (int) event.getProperty (SongIDs::order, 0);
            item.relocatedFrom = (int) event.getProperty (SongIDs::relocatedFrom, -1);
            item.bytes         = blockBytes (event.getProperty (SongIDs::data));
            if (! item.bytes.empty())
                items.push_back (std::move (item));
        }

        for (auto note : SongDocument::getNotesNode (track))
        {
            const int channel  = juce::jlimit (1, 16, (int) note.getProperty (SongIDs::channel, 1));
            const int pitch    = juce::jlimit (0, 127, (int) note.getProperty (SongIDs::pitch));
            const int velocity = juce::jlimit (1, 127, (int) note.getProperty (SongIDs::velocity));
            const int start    = (int) note.getProperty (SongIDs::startTick);

            Item on;
            on.tick  = start;
            on.bytes = { (std::uint8_t) (0x90 | (channel - 1)), (std::uint8_t) pitch, (std::uint8_t) velocity };
            if (note.hasProperty (SongIDs::onOrder)) { on.group = 1; on.order = (int) note.getProperty (SongIDs::onOrder); }
            else                                     { on.group = 2; }
            items.push_back (std::move (on));
        }

        // Every emitted note-on, so an invented off is only left out while the
        // note-on that ended it on import still sits at its tick.
        std::set<std::tuple<int, int, int>> noteOns; // (tick, channel 0-15, pitch)
        for (const auto& item : items)
            if (isNoteOn (item.bytes))
                noteOns.emplace (item.tick, item.bytes[0] & 0x0F, item.bytes[1]);

        for (auto note : SongDocument::getNotesNode (track))
        {
            const int channel = juce::jlimit (1, 16, (int) note.getProperty (SongIDs::channel, 1));
            const int pitch   = juce::jlimit (0, 127, (int) note.getProperty (SongIDs::pitch));
            const int start   = (int) note.getProperty (SongIDs::startTick);
            const int end     = start + juce::jmax (0, (int) note.getProperty (SongIDs::durationTicks));

            if ((bool) note.getProperty (SongIDs::offSynthesized, false)
                && noteOns.count ({ end, channel - 1, pitch }) > 0)
                continue;

            Item off;
            off.tick = end;
            if ((bool) note.getProperty (SongIDs::offIsNoteOnZero, false))
                off.bytes = { (std::uint8_t) (0x90 | (channel - 1)), (std::uint8_t) pitch, 0 };
            else
                off.bytes = { (std::uint8_t) (0x80 | (channel - 1)), (std::uint8_t) pitch,
                              (std::uint8_t) juce::jlimit (0, 127, (int) note.getProperty (SongIDs::offVelocity, 64)) };
            if (note.hasProperty (SongIDs::offOrder)) { off.group = 1; off.order = (int) note.getProperty (SongIDs::offOrder); }
            else                                      { off.group = 0; } // also every off that was invented on import
            items.push_back (std::move (off));
        }

        std::stable_sort (items.begin(), items.end(),
                          [] (const Item& a, const Item& b) { return a.key() < b.key(); });

        RawMidiTrack out;
        out.endTick = (int) track.getProperty (SongIDs::endTick, 0);
        for (auto& item : items)
        {
            out.endTick = std::max (out.endTick, item.tick);
            out.events.push_back ({ item.tick, std::move (item.bytes) });
        }
        return out;
    }
}

RawMidiFile buildRawMidiFile (const SongDocument& doc)
{
    RawMidiFile file;
    file.format          = 1;
    file.ticksPerQuarter = (int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter, 480);

    file.tracks.push_back (buildTrack (doc.getConductorTrack()));
    for (auto track : doc.getSourceMidiNode())
        if (! (bool) track.getProperty (SongIDs::isConductor, false))
            file.tracks.push_back (buildTrack (track));

    return file;
}

} // namespace lotro

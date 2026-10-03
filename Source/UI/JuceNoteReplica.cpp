#include "JuceNoteReplica.h"

#include <algorithm>
#include <utility>

namespace lotro
{

namespace
{
    struct Item
    {
        int  tick       = 0;
        int  rawIndex   = -1; // -1 = a note-off JUCE invented
        int  channel    = 0;  // 0 for non-channel messages, like MidiMessage::getChannel()
        int  noteNumber = -1;
        bool isOn       = false;
        bool isOff      = false;
    };

    Item makeItem (const RawMidiEvent& e, int rawIndex)
    {
        Item item;
        item.tick     = e.tick;
        item.rawIndex = rawIndex;

        const int status = e.bytes.empty() ? 0 : e.bytes[0];
        if (status >= 0x80 && status < 0xF0)
        {
            const int type     = status & 0xF0;
            const int velocity = e.bytes.size() > 2 ? e.bytes[2] : 0;
            item.channel    = (status & 0x0F) + 1;
            item.noteNumber = e.bytes.size() > 1 ? e.bytes[1] : 0;
            item.isOn       = type == 0x90 && velocity > 0;
            item.isOff      = type == 0x80 || (type == 0x90 && velocity == 0);
        }
        return item;
    }

    // juce_MidiFile.cpp reorderNoteOnsAfterNoteOffs, on [begin, end) of one tick group.
    void reorderGroup (std::vector<Item>& list, size_t begin, size_t end)
    {
        size_t it = begin;
        while (it < end)
        {
            size_t firstOn = it;
            while (firstOn < end && ! list[firstOn].isOn)
                ++firstOn;
            if (firstOn == end)
                return;

            size_t lastOff = end;
            for (size_t j = end; j-- > firstOn + 1;)
            {
                if (list[j].isOff
                    && list[j].channel == list[firstOn].channel
                    && list[j].noteNumber == list[firstOn].noteNumber)
                {
                    lastOff = j;
                    break;
                }
            }
            if (lastOff == end)
                return;

            std::swap (list[firstOn], list[lastOff]);
            it = firstOn + 1;
        }
    }
}

std::vector<ReplicaNoteOn> replicateJuceNoteOns (const RawMidiTrack& track)
{
    std::vector<Item> list;
    list.reserve (track.events.size());
    for (int i = 0; i < (int) track.events.size(); ++i)
        list.push_back (makeItem (track.events[(size_t) i], i));

    // MidiMessageSequence::sort is a stable sort by timestamp.
    std::stable_sort (list.begin(), list.end(),
                      [] (const Item& a, const Item& b) { return a.tick < b.tick; });

    for (size_t begin = 0; begin < list.size();)
    {
        size_t end = begin;
        while (end < list.size() && list[end].tick == list[begin].tick)
            ++end;
        reorderGroup (list, begin, end);
        begin = end;
    }

    // juce_MidiMessageSequence.cpp updateMatchedPairs.
    std::vector<ReplicaNoteOn> result;
    for (size_t i = 0; i < list.size(); ++i)
    {
        const Item on = list[i]; // copy: the insert below may reallocate
        if (! on.isOn)
            continue;

        const auto& onBytes = track.events[(size_t) on.rawIndex].bytes;

        ReplicaNoteOn r;
        r.onRawIndex = on.rawIndex;
        r.onTick     = on.tick;
        r.channel    = on.channel;
        r.pitch      = on.noteNumber;
        r.velocity   = onBytes.size() > 2 ? onBytes[2] : 0;

        for (size_t j = i + 1; j < list.size(); ++j)
        {
            const Item m = list[j];
            if (m.noteNumber != on.noteNumber || m.channel != on.channel)
                continue;

            if (m.isOff)
            {
                r.hasOff      = true;
                r.offRawIndex = m.rawIndex;
                r.offTick     = m.tick;
                break;
            }

            if (m.isOn)
            {
                Item invented;
                invented.tick       = m.tick;
                invented.channel    = on.channel;
                invented.noteNumber = on.noteNumber;
                invented.isOff      = true;
                list.insert (list.begin() + (std::ptrdiff_t) j, invented);

                r.hasOff         = true;
                r.offSynthesized = true;
                r.offTick        = invented.tick;
                break;
            }
        }

        result.push_back (r);
    }

    return result;
}

} // namespace lotro

#pragma once

#include "UI/SongDocument.h"

#include <juce_data_structures/juce_data_structures.h>

#include <cstdint>
#include <vector>

// Builders for hand-made Songs used by the playback tests.
namespace lotro::playbacktest
{

inline void addNote (juce::ValueTree track, int pitch, int startTick, int durationTicks,
                     int velocity = 100, int channel = 1)
{
    juce::ValueTree note (SongIDs::NOTE);
    note.setProperty (SongIDs::pitch, pitch, nullptr);
    note.setProperty (SongIDs::startTick, startTick, nullptr);
    note.setProperty (SongIDs::durationTicks, durationTicks, nullptr);
    note.setProperty (SongIDs::velocity, velocity, nullptr);
    note.setProperty (SongIDs::channel, channel, nullptr);
    SongDocument::appendChildBulk (SongDocument::getNotesNode (track), note);
}

inline void addEvent (juce::ValueTree track, int tick, std::vector<std::uint8_t> bytes)
{
    juce::ValueTree event (SongIDs::EVENT);
    event.setProperty (SongIDs::tick, tick, nullptr);
    event.setProperty (SongIDs::data, juce::var (juce::MemoryBlock (bytes.data(), bytes.size())), nullptr);
    SongDocument::appendChildBulk (SongDocument::getEventsNode (track), event);
}

inline void addTempo (SongDocument& doc, int tick, double bpm)
{
    juce::ValueTree change (SongIDs::TEMPO_CHANGE);
    change.setProperty (SongIDs::tick, tick, nullptr);
    change.setProperty (SongIDs::bpm, bpm, nullptr);
    SongDocument::appendChildBulk (doc.getTempoMapNode(), change);
}

inline juce::ValueTree addTrack (SongDocument& doc, const char* name = "T", int channel = 1)
{
    return doc.addTrackBulk (name, static_cast<int> (0xff336699u), channel, 1);
}

} // namespace lotro::playbacktest

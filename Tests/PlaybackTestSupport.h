#pragma once

#include "UI/Playback/EventSink.h"
#include "UI/SongDocument.h"

#include <juce_data_structures/juce_data_structures.h>

#include <cmath>
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

// A conductor FF 51 event; the TEMPO_MAP is derived from it. Sets the time base so
// the maps are kept (callers use BPMs that are whole microseconds per quarter).
inline void addTempo (SongDocument& doc, int tick, double bpm)
{
    doc.setTimeBase (true);
    const auto us = (std::uint32_t) std::llround (60000000.0 / bpm);
    auto events = SongDocument::getEventsNode (doc.getConductorTrack());
    juce::ValueTree event (SongIDs::EVENT);
    event.setProperty (SongIDs::tick, tick, nullptr);
    event.setProperty (SongIDs::order, events.getNumChildren(), nullptr);
    const std::uint8_t bytes[] = { 0xFF, 0x51, (std::uint8_t) (us >> 16), (std::uint8_t) (us >> 8), (std::uint8_t) us };
    event.setProperty (SongIDs::data, juce::var (juce::MemoryBlock (bytes, sizeof bytes)), nullptr);
    SongDocument::appendChildBulk (events, event);
}

inline juce::ValueTree addTrack (SongDocument& doc, const char* name = "T", int channel = 1)
{
    return doc.addTrackBulk (name, static_cast<int> (0xff336699u), channel, 1);
}

// Test sink that records every event and its frame position.
struct RecordingSink : EventSink
{
    struct Record { long frame; PlaybackEvent event; };
    struct Reset { size_t recordsBefore; int channel; };   // records.size() when resetChannel() was called
    std::vector<Record> records;
    std::vector<Reset> resets;
    std::vector<int> releasedChannels;
    long framesRendered = 0;
    int releaseAllCount = 0;
    bool replaced = false;      // set true to make the next beginBlock() report a replaced sink

    void prepare (double, int) override {}
    bool beginBlock() noexcept override { const bool r = replaced; replaced = false; return r; }
    void handle (const PlaybackEvent& e) noexcept override { records.push_back ({ framesRendered, e }); }
    void releaseChannel (int c) noexcept override { releasedChannels.push_back (c); }
    void releaseAll() noexcept override { ++releaseAllCount; }
    void resetChannel (int c) noexcept override { resets.push_back ({ records.size(), c }); }
    void render (float*, float*, int n) noexcept override { framesRendered += n; }

    int count (PlaybackEventKind k) const
    {
        int n = 0;
        for (const auto& r : records) if (r.event.kind == k) ++n;
        return n;
    }
};

} // namespace lotro::playbacktest

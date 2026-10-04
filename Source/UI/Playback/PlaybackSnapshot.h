#pragma once

#include "UI/Playback/TempoMap.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace lotro
{

class SongDocument;

// The enumerator order IS the same-tick firing order. Control precedes Program
// so a CC0/CC32 bank select lands before the program change that tsf resolves
// against the channel's current bank; NoteOff precedes NoteOn so a retrigger
// at a note's own end sounds again.
enum class PlaybackEventKind : std::uint8_t { Control, Program, PitchBend, NoteOff, NoteOn };

struct PlaybackEvent
{
    int tick = 0;
    double seconds = 0.0;
    PlaybackEventKind kind = PlaybackEventKind::NoteOn;
    int virtualChannel = 0;
    int trackIndex = 0;
    int data1 = 0;
    int data2 = 0;
};

struct VirtualChannel
{
    juce::int64 trackId = -1;
    int trackIndex = 0;
    int midiChannel = 1;
    bool isDrum = false;
};

constexpr int kMaxVirtualChannels = 256;

// Immutable (apart from the audible flags) flattened view of a Song for the
// audio thread. Built on the message thread by buildSnapshot().
class PlaybackSnapshot
{
public:
    PlaybackSnapshot (TempoMap tempoIn, std::vector<PlaybackEvent> eventsIn,
                      std::vector<VirtualChannel> channelsIn, std::vector<juce::int64> trackIdsIn);

    const TempoMap& tempo() const noexcept { return tempoMap; }
    const std::vector<PlaybackEvent>& events() const noexcept { return eventList; }
    const std::vector<VirtualChannel>& channels() const noexcept { return channelList; }
    const std::vector<juce::int64>& trackIds() const noexcept { return trackIdList; }
    int numTracks() const noexcept { return (int) trackIdList.size(); }
    int trackIndexForId (juce::int64 trackId) const noexcept;
    double endSeconds() const noexcept { return endTime; }
    size_t firstEventAtOrAfter (double seconds) const noexcept;
    const std::vector<int>& channelsOfTrack (int trackIndex) const { return byTrack[(size_t) trackIndex]; }

    bool isAudible (int trackIndex) const noexcept { return audible[(size_t) trackIndex].load (std::memory_order_acquire) != 0; }
    void setAudible (int trackIndex, bool shouldBeAudible) noexcept { audible[(size_t) trackIndex].store (shouldBeAudible ? 1 : 0, std::memory_order_release); }

    // Audio-thread-only: which tracks the engine has already released voices for.
    bool appliedAudible (int trackIndex) const noexcept { return applied[(size_t) trackIndex] != 0; }
    void setAppliedAudible (int trackIndex, bool value) const noexcept { applied[(size_t) trackIndex] = value ? 1 : 0; }

private:
    TempoMap tempoMap;
    std::vector<PlaybackEvent> eventList;
    std::vector<VirtualChannel> channelList;
    std::vector<juce::int64> trackIdList;
    std::vector<std::vector<int>> byTrack;
    std::unique_ptr<std::atomic<std::uint8_t>[]> audible;
    mutable std::vector<std::uint8_t> applied;
    double endTime = 0.0;
};

TempoMap tempoMapFromDocument (const SongDocument& doc);
std::shared_ptr<PlaybackSnapshot> buildSnapshot (const SongDocument& doc);

} // namespace lotro

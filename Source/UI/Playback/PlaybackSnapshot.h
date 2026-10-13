#pragma once

#include "UI/Playback/TempoMap.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
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

    int gainPercent (int trackIndex) const noexcept { return gain[(size_t) trackIndex].load (std::memory_order_acquire); }
    void setGainPercent (int trackIndex, int percent) noexcept
    {
        gain[(size_t) trackIndex].store ((std::uint8_t) juce::jlimit (0, 100, percent), std::memory_order_release);
    }

    // The track's mixer pan override (-100..100, 0 = centre); none means the
    // file's own CC10 pan plays. Set on the message thread, read on the audio thread.
    static constexpr int kNoPan = -128;   // storage sentinel for "no override"
    std::optional<int> panOverride (int trackIndex) const noexcept
    {
        const int v = pan[(size_t) trackIndex].load (std::memory_order_acquire);
        return v == kNoPan ? std::nullopt : std::optional<int> (v);
    }
    void setPanOverride (int trackIndex, std::optional<int> value) noexcept
    {
        pan[(size_t) trackIndex].store ((std::int8_t) (value.has_value() ? juce::jlimit (-100, 100, *value) : kNoPan),
                                        std::memory_order_release);
    }

    // Audio-thread-only: which tracks the engine has already released voices for.
    bool appliedAudible (int trackIndex) const noexcept { return applied[(size_t) trackIndex] != 0; }
    void setAppliedAudible (int trackIndex, bool value) const noexcept { applied[(size_t) trackIndex] = value ? 1 : 0; }

    // Audio-thread-only: the pan override the engine last acted on (kNoPan = none).
    int appliedPan (int trackIndex) const noexcept { return appliedPanValues[(size_t) trackIndex]; }
    void setAppliedPan (int trackIndex, int value) const noexcept { appliedPanValues[(size_t) trackIndex] = (std::int8_t) value; }

private:
    TempoMap tempoMap;
    std::vector<PlaybackEvent> eventList;
    std::vector<VirtualChannel> channelList;
    std::vector<juce::int64> trackIdList;
    std::vector<std::vector<int>> byTrack;
    std::unique_ptr<std::atomic<std::uint8_t>[]> audible;
    std::unique_ptr<std::atomic<std::uint8_t>[]> gain;
    std::unique_ptr<std::atomic<std::int8_t>[]> pan;
    mutable std::vector<std::uint8_t> applied;
    mutable std::vector<std::int8_t> appliedPanValues;
    double endTime = 0.0;
};

// The audition gain applied to a NoteOn velocity: identity at 100, silence at 0,
// otherwise never below 1 so a quiet track still sounds.
int scaleVelocity (int velocity, int gainPercent) noexcept;

TempoMap tempoMapFromDocument (const SongDocument& doc);
std::shared_ptr<PlaybackSnapshot> buildSnapshot (const SongDocument& doc);

} // namespace lotro

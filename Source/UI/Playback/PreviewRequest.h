#pragma once

#include "UI/Playback/PlaybackSnapshot.h"

#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <optional>
#include <vector>

namespace lotro
{

// A note preview sounds at most LOTRO's chord cap of notes, each on its own
// synth channel past the snapshot's virtual channels, so playback's
// releaseAll / re-chase never touches it.
constexpr int kMaxPreviewNotes = 6;
constexpr int kPreviewChannelBase = kMaxVirtualChannels;
constexpr int kSynthChannels = kMaxVirtualChannels + kMaxPreviewNotes;
constexpr double kMaxPreviewSeconds = 1.0;

struct PreviewedNote
{
    juce::int64 trackId = -1;
    int midiChannel = 1;
    int pitch = 60;
    int velocity = 100;
    int startTick = 0;
    int endTick = 0;
};

// Fixed-size so it can cross to the audio thread through a FIFO.
struct PreviewRequest
{
    std::array<PreviewedNote, kMaxPreviewNotes> notes {};
    int count = 0;
};

// The request for previewing `notes` (NOTE trees) of the track `trackId`, or
// nullopt when there are none, more than kMaxPreviewNotes, or they do not all
// start at the same tick (only a single note or a chord previews).
std::optional<PreviewRequest> makePreviewRequest (juce::int64 trackId, const std::vector<juce::ValueTree>& notes);

} // namespace lotro

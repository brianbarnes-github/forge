#pragma once

#include "UI/Playback/EventSink.h"
#include "UI/Playback/HandOff.h"

#include <juce_core/juce_core.h>

#include <memory>
#include <vector>

struct tsf;   // TinySoundFont

namespace lotro
{

// EventSink backed by TinySoundFont. SoundFont instances are built and fully
// initialised on the message thread (voice limit set, all virtual channels
// allocated) and handed to the audio thread through a HandOff, so the audio
// thread never allocates or frees.
class SynthVoice : public EventSink
{
public:
    static constexpr int kMaxVoices = 192;

    SynthVoice();
    ~SynthVoice() override;

    void loadSoundFont (const juce::File& file);                    // message thread; throws PlaybackError
    void loadSoundFontFromMemory (const void* data, size_t size);   // message thread; throws PlaybackError
    bool hasSoundFont() const noexcept { return fontLoaded; }
    void collectGarbage() { instances.collectRetired(); }           // message thread

    void prepare (double sampleRate, int maxBlockSize) override;
    bool beginBlock() noexcept override;
    void handle (const PlaybackEvent& event) noexcept override;
    void releaseChannel (int virtualChannel) noexcept override;
    void releaseAll() noexcept override;
    void resetChannel (int virtualChannel) noexcept override;
    void render (float* left, float* right, int numFrames) noexcept override;

private:
    struct Instance
    {
        tsf* synth = nullptr;
        Instance() = default;
        Instance (const Instance&) = delete;
        Instance& operator= (const Instance&) = delete;
        ~Instance();
    };

    std::shared_ptr<Instance> build() const;   // throws PlaybackError

    HandOff<Instance> instances;
    Instance* current = nullptr;                // audio thread only
    juce::MemoryBlock fontBytes;                // message thread; kept to rebuild on a sample-rate change
    std::vector<float> scratch;                 // interleaved stereo, sized in prepare()
    double sampleRate = 44100.0;
    int maxBlock = 512;
    bool fontLoaded = false;
};

} // namespace lotro

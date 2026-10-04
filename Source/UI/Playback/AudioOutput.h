#pragma once

#include "UI/Playback/PlaybackEngine.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <memory>

namespace lotro
{

// Opens the system default output device and pulls PlaybackEngine::renderBlock
// from its callback. Compiled into forge_ui only.
//
// THREADING CONTRACT: PlaybackEngine::prepare (and so SynthVoice::prepare /
// publish) is message-thread-only. Construct this object on the message thread:
// the device opens, and AudioSourcePlayer calls EngineSource::prepareToPlay (the
// only caller of engine.prepare), synchronously inside the constructor on that
// thread. JUCE re-prepares later only from the message thread (device restart
// handling), never from the audio callback. Destroy it on the message thread
// BEFORE the engine and the synth: the destructor stops the audio thread.
class AudioOutput
{
public:
    explicit AudioOutput (PlaybackEngine& engine);   // throws PlaybackError (AudioDeviceUnavailable)
    ~AudioOutput();

    AudioOutput (const AudioOutput&) = delete;
    AudioOutput& operator= (const AudioOutput&) = delete;

private:
    class EngineSource;
    juce::AudioDeviceManager deviceManager;
    std::unique_ptr<EngineSource> source;
    juce::AudioSourcePlayer player;
};

} // namespace lotro

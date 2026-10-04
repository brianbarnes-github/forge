#include "UI/Playback/AudioOutput.h"

#include "UI/Playback/PlaybackError.h"

#include <atomic>
#include <exception>
#include <string>

namespace lotro
{

class AudioOutput::EngineSource : public juce::AudioSource
{
public:
    explicit EngineSource (PlaybackEngine& e) : engine (e) {}

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override
    {
        // Runs on the message thread (see AudioOutput.h). An exception must not
        // escape into JUCE; on failure the engine stays silent (renderBlock
        // is not driven until prepared).
        try
        {
            engine.prepare (sampleRate, samplesPerBlockExpected);
            prepared = true;
        }
        catch (const std::exception&)
        {
            prepared = false;
        }
    }

    void releaseResources() override {}

    void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override
    {
        if (! prepared)
        {
            info.clearActiveBufferRegion();
            return;
        }
        engine.renderBlock (*info.buffer, info.startSample, info.numSamples);
    }

private:
    PlaybackEngine& engine;
    std::atomic<bool> prepared { false };   // set on the message thread, read on the audio thread
};

AudioOutput::AudioOutput (PlaybackEngine& engine) : source (std::make_unique<EngineSource> (engine))
{
    const auto error = deviceManager.initialiseWithDefaultDevices (0, 2);
    if (error.isNotEmpty() || deviceManager.getCurrentAudioDevice() == nullptr)
        throw PlaybackError (PlaybackErrorKind::AudioDeviceUnavailable,
                             "No audio output device is available"
                                 + (error.isNotEmpty() ? ": " + error.toStdString() : std::string()));
    // setSource first: the player has no sample rate yet, so this does not prepare.
    player.setSource (source.get());
    // Calls audioDeviceAboutToStart -> prepareToPlay on this (message) thread.
    deviceManager.addAudioCallback (&player);
}

AudioOutput::~AudioOutput()
{
    deviceManager.removeAudioCallback (&player);
    player.setSource (nullptr);
}

} // namespace lotro

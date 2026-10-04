#include "UI/Playback/SynthVoice.h"

#include "UI/Playback/PlaybackError.h"
#include "UI/Playback/PlaybackSnapshot.h"

#define TSF_IMPLEMENTATION   // the one translation unit that compiles TinySoundFont
#include <tsf.h>

#include <algorithm>
#include <cmath>

namespace lotro
{

SynthVoice::Instance::~Instance()
{
    if (synth != nullptr)
        tsf_close (synth);
}

SynthVoice::SynthVoice() = default;
SynthVoice::~SynthVoice() = default;

std::shared_ptr<SynthVoice::Instance> SynthVoice::build() const
{
    auto instance = std::make_shared<Instance>();
    instance->synth = tsf_load_memory (fontBytes.getData(), (int) fontBytes.getSize());
    if (instance->synth == nullptr)
        throw PlaybackError (PlaybackErrorKind::SoundFontInvalid, "That file is not a usable SoundFont (.sf2).");

    tsf_set_output (instance->synth, TSF_STEREO_INTERLEAVED, (int) sampleRate, 0.0f);
    // Pre-sizes the voice pool: with a fixed maximum, note-on steals a releasing
    // voice instead of reallocating the array.
    if (tsf_set_max_voices (instance->synth, kMaxVoices) == 0)
        throw PlaybackError (PlaybackErrorKind::SoundFontInvalid, "Out of memory preparing the SoundFont.");
    // Touching the last channel allocates every channel up front, so the audio
    // thread never grows the channel array.
    if (tsf_channel_set_volume (instance->synth, kMaxVirtualChannels - 1, 1.0f) == 0)
        throw PlaybackError (PlaybackErrorKind::SoundFontInvalid, "Out of memory preparing the SoundFont.");
    return instance;
}

void SynthVoice::loadSoundFontFromMemory (const void* data, size_t size)
{
    const auto previousBytes = fontBytes;
    fontBytes = juce::MemoryBlock (data, size);
    try
    {
        instances.publish (build());
        fontLoaded = true;
    }
    catch (...)
    {
        fontBytes = previousBytes;   // keep whatever was working before
        throw;
    }
}

void SynthVoice::loadSoundFont (const juce::File& file)
{
    if (! file.existsAsFile())
        throw PlaybackError (PlaybackErrorKind::SoundFontMissing,
                             "The SoundFont file was not found: " + file.getFullPathName().toStdString());
    juce::MemoryBlock bytes;
    if (! file.loadFileAsData (bytes))
        throw PlaybackError (PlaybackErrorKind::SoundFontMissing,
                             "The SoundFont file could not be read: " + file.getFullPathName().toStdString());
    loadSoundFontFromMemory (bytes.getData(), bytes.getSize());
}

void SynthVoice::prepare (double sampleRateIn, int maxBlockSize)
{
    const double newRate = sampleRateIn > 0.0 ? sampleRateIn : 44100.0;
    const bool rateChanged = std::abs (newRate - sampleRate) > 0.5;   // rates are whole Hz
    sampleRate = newRate;
    maxBlock = std::max (1, maxBlockSize);
    scratch.assign ((size_t) maxBlock * 2, 0.0f);
    if (rateChanged && fontLoaded)
        instances.publish (build());
}

bool SynthVoice::beginBlock() noexcept
{
    bool swapped = false;
    current = instances.acquire (swapped);
    return swapped;
}

void SynthVoice::handle (const PlaybackEvent& e) noexcept
{
    if (current == nullptr || current->synth == nullptr
        || e.virtualChannel < 0 || e.virtualChannel >= kMaxVirtualChannels)
        return;

    tsf* f = current->synth;
    const int ch = e.virtualChannel;
    switch (e.kind)
    {
        case PlaybackEventKind::NoteOn:    tsf_channel_note_on (f, ch, e.data1, (float) e.data2 / 127.0f); break;
        case PlaybackEventKind::NoteOff:   tsf_channel_note_off (f, ch, e.data1); break;
        case PlaybackEventKind::Program:
            if (e.data2 != 0) tsf_channel_set_bank_preset (f, ch, 128, e.data1);   // drum kit
            else              tsf_channel_set_presetnumber (f, ch, e.data1, 0);
            break;
        case PlaybackEventKind::Control:   tsf_channel_midi_control (f, ch, e.data1, e.data2); break;
        case PlaybackEventKind::PitchBend: tsf_channel_set_pitchwheel (f, ch, e.data1); break;
    }
}

void SynthVoice::releaseChannel (int virtualChannel) noexcept
{
    if (current != nullptr && current->synth != nullptr && virtualChannel >= 0 && virtualChannel < kMaxVirtualChannels)
        tsf_channel_note_off_all (current->synth, virtualChannel);
}

void SynthVoice::releaseAll() noexcept
{
    if (current != nullptr && current->synth != nullptr)
        tsf_note_off_all (current->synth);
}

// Returns the channel to tsf's own initial state (see tsf_channel_init): volume
// and expression full, pan centred, pitch wheel centred, no sustain, pitch range
// 2 semitones, no tuning, no RPN selected. CC121 covers volume, expression, pan,
// RPN, data entry, pitch range and tuning (and sets bank 0), but not the pitch
// wheel or sustain, so those are set explicitly. The preset is left alone: the
// chase replays the Program events. All calls are allocation-free because every
// channel is allocated when the instance is built.
void SynthVoice::resetChannel (int virtualChannel) noexcept
{
    if (current == nullptr || current->synth == nullptr || virtualChannel < 0 || virtualChannel >= kMaxVirtualChannels)
        return;

    tsf* f = current->synth;
    tsf_channel_midi_control (f, virtualChannel, 121, 0);
    tsf_channel_set_pitchwheel (f, virtualChannel, 8192);
    tsf_channel_midi_control (f, virtualChannel, 64, 0);   // sustain off (also ends sustain-held voices)
}

void SynthVoice::render (float* left, float* right, int numFrames) noexcept
{
    if (current == nullptr || current->synth == nullptr)
    {
        std::fill (left, left + numFrames, 0.0f);
        if (right != left)
            std::fill (right, right + numFrames, 0.0f);
        return;
    }

    int done = 0;
    while (done < numFrames)
    {
        const int n = std::min (maxBlock, numFrames - done);
        tsf_render_float (current->synth, scratch.data(), n, 0);   // mixing = 0: overwrites
        for (int i = 0; i < n; ++i)
        {
            left[done + i] = scratch[(size_t) i * 2];
            if (right != left)
                right[done + i] = scratch[(size_t) i * 2 + 1];
        }
        done += n;
    }
}

} // namespace lotro

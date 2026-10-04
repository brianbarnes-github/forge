#include "UI/Playback/PlaybackError.h"
#include "UI/Playback/SynthVoice.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace lotro;

namespace
{
    juce::File localSoundFont()
    {
        return juce::File (__FILE__).getParentDirectory().getParentDirectory()
                   .getChildFile ("resources/soundfonts/TimGM6mb.sf2");
    }

    PlaybackEvent ev (PlaybackEventKind k, int vch, int d1, int d2)
    {
        PlaybackEvent e;
        e.kind = k; e.virtualChannel = vch; e.data1 = d1; e.data2 = d2;
        return e;
    }
}

TEST_CASE ("SynthVoice: a missing SoundFont file throws SoundFontMissing", "[playback][synth]")
{
    SynthVoice synth;
    try
    {
        synth.loadSoundFont (juce::File ("/nonexistent/none.sf2"));
        FAIL ("expected PlaybackError");
    }
    catch (const PlaybackError& e)
    {
        CHECK (e.kind() == PlaybackErrorKind::SoundFontMissing);
    }
    CHECK (! synth.hasSoundFont());
}

TEST_CASE ("SynthVoice: garbage bytes throw SoundFontInvalid", "[playback][synth]")
{
    SynthVoice synth;
    const char junk[] = "this is not a soundfont";
    try
    {
        synth.loadSoundFontFromMemory (junk, sizeof (junk));
        FAIL ("expected PlaybackError");
    }
    catch (const PlaybackError& e)
    {
        CHECK (e.kind() == PlaybackErrorKind::SoundFontInvalid);
    }
    CHECK (! synth.hasSoundFont());
}

TEST_CASE ("SynthVoice: with no SoundFont, render outputs silence and events are harmless", "[playback][synth]")
{
    SynthVoice synth;
    synth.prepare (48000.0, 256);
    synth.beginBlock();
    synth.handle (ev (PlaybackEventKind::NoteOn, 0, 60, 100));
    std::vector<float> l (256, 1.0f), r (256, 1.0f);
    synth.render (l.data(), r.data(), 256);
    for (float s : l) CHECK (std::fabs (s) < 1e-9f);   // exact silence (was prefilled with 1.0)
}

TEST_CASE ("SynthVoice: a real SoundFont sounds a note and falls silent after release (smoke)", "[playback][synth]")
{
    const auto font = localSoundFont();
    if (! font.existsAsFile())
    {
        WARN ("skipped: " << font.getFullPathName().toStdString() << " not present (the GPL-2 SoundFont is local-only)");
        return;
    }

    SynthVoice synth;
    synth.prepare (48000.0, 512);
    synth.loadSoundFont (font);
    REQUIRE (synth.hasSoundFont());

    synth.beginBlock();
    synth.handle (ev (PlaybackEventKind::Program, 0, 0, 0));
    synth.handle (ev (PlaybackEventKind::NoteOn, 0, 60, 110));
    std::vector<float> l (512), r (512);
    float peak = 0.0f;
    for (int i = 0; i < 10; ++i)
    {
        synth.render (l.data(), r.data(), 512);
        for (float s : l) peak = std::max (peak, std::fabs (s));
    }
    CHECK (peak > 0.001f);

    synth.releaseAll();
    for (int i = 0; i < 400; ++i)           // let the release tail finish
        synth.render (l.data(), r.data(), 512);
    float tail = 0.0f;
    for (float s : l) tail = std::max (tail, std::fabs (s));
    CHECK (tail < 0.001f);
}

TEST_CASE ("SynthVoice: loading a SoundFont mid-stream makes beginBlock report the replacement", "[playback][synth]")
{
    const auto font = localSoundFont();
    if (! font.existsAsFile()) { WARN ("skipped: local SoundFont not present"); return; }

    SynthVoice synth;
    synth.prepare (48000.0, 512);
    CHECK (! synth.beginBlock());
    synth.loadSoundFont (font);
    CHECK (synth.beginBlock());
    CHECK (! synth.beginBlock());
}

TEST_CASE ("SynthVoice: the highest virtual channel sounds, and a drum Program flag is accepted", "[playback][synth]")
{
    const auto font = localSoundFont();
    if (! font.existsAsFile()) { WARN ("skipped: local SoundFont not present"); return; }

    SynthVoice synth;
    synth.prepare (48000.0, 512);
    synth.loadSoundFont (font);
    synth.beginBlock();
    synth.handle (ev (PlaybackEventKind::Program, kMaxVirtualChannels - 1, 24, 0));
    synth.handle (ev (PlaybackEventKind::NoteOn, kMaxVirtualChannels - 1, 64, 120));
    std::vector<float> l (512), r (512);
    float peak = 0.0f;
    for (int i = 0; i < 4; ++i)
    {
        synth.render (l.data(), r.data(), 512);
        for (float s : l) peak = std::max (peak, std::fabs (s));
    }
    CHECK (peak > 0.001f);

    synth.releaseChannel (kMaxVirtualChannels - 1);
    for (int i = 0; i < 400; ++i)
        synth.render (l.data(), r.data(), 512);
    float tail = 0.0f;
    for (float s : l) tail = std::max (tail, std::fabs (s));
    CHECK (tail < 0.001f);

    // Drum kit on channel 3 (bank 128): a kick should sound.
    synth.handle (ev (PlaybackEventKind::Program, 3, 0, 1));
    synth.handle (ev (PlaybackEventKind::NoteOn, 3, 36, 120));
    float drumPeak = 0.0f;
    for (int i = 0; i < 4; ++i)
    {
        synth.render (l.data(), r.data(), 512);
        for (float s : l) drumPeak = std::max (drumPeak, std::fabs (s));
    }
    CHECK (drumPeak > 0.001f);
}

TEST_CASE ("SynthVoice: resetChannel with no SoundFont, or an out-of-range channel, is harmless", "[playback][synth][reset]")
{
    SynthVoice none;
    none.prepare (48000.0, 256);
    none.beginBlock();
    none.resetChannel (0);
    none.resetChannel (-1);
    none.resetChannel (kMaxVirtualChannels);

    const auto font = localSoundFont();
    if (! font.existsAsFile()) { WARN ("skipped the loaded-font half: local SoundFont not present"); return; }
    SynthVoice synth;
    synth.prepare (48000.0, 256);
    synth.loadSoundFont (font);
    synth.beginBlock();
    synth.resetChannel (-1);
    synth.resetChannel (kMaxVirtualChannels);
    synth.resetChannel (0);
    SUCCEED();
}

TEST_CASE ("SynthVoice: resetChannel restores a channel left silent by CC7 = 0", "[playback][synth][reset]")
{
    const auto font = localSoundFont();
    if (! font.existsAsFile()) { WARN ("skipped: local SoundFont not present"); return; }

    SynthVoice synth;
    synth.prepare (48000.0, 512);
    synth.loadSoundFont (font);
    synth.beginBlock();

    std::vector<float> l (512), r (512);
    const auto peakOfNote = [&] (int vch)
    {
        synth.handle (ev (PlaybackEventKind::NoteOn, vch, 60, 110));
        float peak = 0.0f;
        for (int i = 0; i < 6; ++i)
        {
            synth.render (l.data(), r.data(), 512);
            for (float s : l) peak = std::max (peak, std::fabs (s));
        }
        synth.releaseAll();
        for (int i = 0; i < 400; ++i)
            synth.render (l.data(), r.data(), 512);   // let the tail finish
        return peak;
    };

    synth.handle (ev (PlaybackEventKind::Program, 5, 0, 0));
    synth.handle (ev (PlaybackEventKind::Control, 5, 7, 0));   // channel volume to zero
    CHECK (peakOfNote (5) < 0.001f);                           // the leftover state really silences it

    synth.resetChannel (5);
    CHECK (peakOfNote (5) > 0.001f);                           // reset restored tsf's full-volume default
}

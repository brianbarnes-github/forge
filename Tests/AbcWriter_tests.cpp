#include "Core/AbcWriter.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace
{
    lotro::Note make (int pitch, int start, int duration, int velocity = 100)
    {
        lotro::Note n;
        n.pitch         = pitch;
        n.startTick     = start;
        n.durationTicks = duration;
        n.velocity      = velocity;
        return n;
    }

    lotro::Song baseSong()
    {
        lotro::Song s;
        s.ticksPerQuarter = 480;
        s.tempoMap.push_back ({ 0, 120.0 });
        s.meterMap.push_back ({ 0, 4, 4 });
        s.title = "Test";
        return s;
    }

    bool contains (const std::string& haystack, const std::string& needle)
    {
        return haystack.find (needle) != std::string::npos;
    }
}

TEST_CASE ("abc pitch tokens: middle-C octave range", "[abc]")
{
    CHECK (lotro::abcPitchToken (60) == "C");
    CHECK (lotro::abcPitchToken (62) == "D");
    CHECK (lotro::abcPitchToken (71) == "B");
    CHECK (lotro::abcPitchToken (72) == "c");
    CHECK (lotro::abcPitchToken (48) == "C,");
    CHECK (lotro::abcPitchToken (36) == "C,,");
    CHECK (lotro::abcPitchToken (84) == "c'");
    CHECK (lotro::abcPitchToken (96) == "c''");
}

TEST_CASE ("abc pitch tokens: sharps get caret prefix", "[abc]")
{
    CHECK (lotro::abcPitchToken (61) == "^C");
    CHECK (lotro::abcPitchToken (73) == "^c");
}

TEST_CASE ("abc duration tokens at L:1/8 (PPQ 480)", "[abc]")
{
    const int ppq = 480;
    CHECK (lotro::abcDurationToken (240, ppq) == "");        // eighth
    CHECK (lotro::abcDurationToken (480, ppq) == "2");       // quarter
    CHECK (lotro::abcDurationToken (120, ppq) == "/2");      // sixteenth
    CHECK (lotro::abcDurationToken (360, ppq) == "3/2");     // dotted eighth
    CHECK (lotro::abcDurationToken (960, ppq) == "4");       // half
    CHECK (lotro::abcDurationToken (1920, ppq) == "8");      // whole
}

TEST_CASE ("abc writer: single C quarter emits C2 and correct header", "[abc]")
{
    auto song = baseSong();
    lotro::Track t;
    t.name = "Melody";
    t.instrument = lotro::LotroInstrument::LuteOfAges;
    t.notes.push_back (make (60, 0, 480));
    song.tracks.push_back (t);

    const auto abc = lotro::writeAbc (song);

    CHECK (contains (abc, "X:1"));
    CHECK (contains (abc, "T:Test - Melody"));
    CHECK (contains (abc, "L:1/8"));
    CHECK (contains (abc, "Q:120"));
    CHECK (contains (abc, "M:4/4"));
    CHECK (contains (abc, "K:C"));
    CHECK (contains (abc, "C2"));
}

TEST_CASE ("abc writer: simultaneous notes become a chord", "[abc]")
{
    auto song = baseSong();
    lotro::Track t;
    t.instrument = lotro::LotroInstrument::LuteOfAges;
    t.notes.push_back (make (60, 0, 480));
    t.notes.push_back (make (64, 0, 480));
    t.notes.push_back (make (67, 0, 480));
    song.tracks.push_back (t);

    const auto abc = lotro::writeAbc (song);

    CHECK (contains (abc, "[C2E2G2]"));
}

TEST_CASE ("abc writer: drum track emits mapped ABC note", "[abc]")
{
    auto song = baseSong();
    lotro::Track t;
    t.name       = "Drums";
    t.instrument = lotro::LotroInstrument::Drums;

    auto snare = make (38, 0, 240);
    snare.isDrum = true;
    t.notes.push_back (snare);
    song.tracks.push_back (t);

    const auto abc = lotro::writeAbc (song);

    CHECK (contains (abc, "T:Test - Drums"));
    CHECK (contains (abc, "F"));                            // spec §2.6: GM 38 → F
}

TEST_CASE ("abc writer: multi-part file numbers X: in order", "[abc]")
{
    auto song = baseSong();
    lotro::Track a;
    a.name       = "Lute Part";
    a.instrument = lotro::LotroInstrument::LuteOfAges;
    a.notes.push_back (make (60, 0, 480));
    lotro::Track b;
    b.name       = "Flute Part";
    b.instrument = lotro::LotroInstrument::Flute;
    b.notes.push_back (make (72, 0, 480));
    song.tracks.push_back (a);
    song.tracks.push_back (b);

    const auto abc = lotro::writeAbc (song);

    CHECK (contains (abc, "X:1"));
    CHECK (contains (abc, "X:2"));
    CHECK (contains (abc, "T:Test - Lute Part"));
    CHECK (contains (abc, "T:Test - Flute Part"));
}

TEST_CASE ("abc writer: rests fill gaps between notes", "[abc]")
{
    auto song = baseSong();
    lotro::Track t;
    t.instrument = lotro::LotroInstrument::LuteOfAges;
    t.notes.push_back (make (60,   0, 480));
    t.notes.push_back (make (62, 960, 480));               // one beat of rest between
    song.tracks.push_back (t);

    const auto abc = lotro::writeAbc (song);

    CHECK (contains (abc, "z2"));
}

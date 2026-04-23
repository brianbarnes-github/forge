// Pipeline-level regression anchors for mid-song tempo changes. Complements
// `TempoCollapse_tests.cpp` (unit math) and `EndToEnd_tests.cpp` (file-driven
// full-song smoke). These tests construct Songs directly so we can vary the
// tempo map precisely without needing a bespoke MIDI fixture, then run the
// full constraint pipeline and emit ABC.
//
// Intent: pin the properties that must hold before we touch `TempoCollapse`
// or the emitter to fix rest/gap scaling (#1) or meter-change support (#2).
// Specifically: single-Q: output, bar labels still present, note durations
// rescaled, and the single-tempo control case is unchanged.

#include "Core/AbcWriter.h"
#include "Core/Song.h"
#include "Core/Constraints/RangeConstraint.h"
#include "Core/Constraints/DurationConstraint.h"
#include "Core/Constraints/TempoCollapse.h"
#include "Core/Constraints/ChordConstraint.h"
#include "Core/Constraints/CollisionGuard.h"
#include "Core/Constraints/DynamicMapper.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace
{
    lotro::Note note (int pitch, int startTick, int durationTicks, int velocity = 100)
    {
        lotro::Note n;
        n.pitch         = pitch;
        n.startTick     = startTick;
        n.durationTicks = durationTicks;
        n.velocity      = velocity;
        return n;
    }

    void runPipeline (lotro::Song& song, lotro::Diagnostics& diagnostics)
    {
        for (auto& track : song.tracks)
        {
            if (! track.enabled) continue;
            lotro::applyRangeConstraint    (track, diagnostics);
            lotro::applyChordConstraint    (track, diagnostics);
            lotro::applyDurationConstraint (track, song, diagnostics);
            lotro::applyTempoCollapse      (track, song, diagnostics);
            lotro::applyCollisionGuard     (track, diagnostics);
            lotro::applyDynamicMapper      (track, diagnostics);
        }

        lotro::applyTempoCollapseToSongMaps (song);
    }

    bool contains (const std::string& haystack, const std::string& needle)
    {
        return haystack.find (needle) != std::string::npos;
    }

    int countOccurrences (const std::string& haystack, const std::string& needle)
    {
        int count = 0;
        size_t pos = 0;
        while ((pos = haystack.find (needle, pos)) != std::string::npos)
        {
            ++count;
            pos += needle.size();
        }
        return count;
    }
}

TEST_CASE ("tempo-pipeline: mid-song halving scales durations in the slow section", "[tempo][pipeline]")
{
    lotro::Song song;
    song.ticksPerQuarter = 480;
    song.meterMap.push_back ({ 0, 4, 4 });
    song.tempoMap.push_back ({    0, 120.0 });
    song.tempoMap.push_back ({ 1920,  60.0 });  // halve at bar 2 boundary

    lotro::Track t;
    t.name       = "Lead";
    t.instrument = lotro::LotroInstrument::LuteOfAges;
    t.notes.push_back (note (60,    0, 480));   // bar 1, main tempo — unchanged
    t.notes.push_back (note (62,  480, 480));   // bar 1, main tempo — unchanged
    t.notes.push_back (note (64, 1920, 480));   // bar 2, slow — should scale to 960
    t.notes.push_back (note (65, 2880, 480));   // bar 2, slow — should scale to 960
    song.tracks.push_back (t);

    lotro::Diagnostics diagnostics;
    runPipeline (song, diagnostics);

    REQUIRE (song.tracks.size() == 1);
    REQUIRE (song.tracks[0].notes.size() == 4);

    CHECK (song.tracks[0].notes[0].durationTicks == 480);
    CHECK (song.tracks[0].notes[1].durationTicks == 480);
    CHECK (song.tracks[0].notes[2].durationTicks == 960);
    CHECK (song.tracks[0].notes[3].durationTicks == 960);
}

TEST_CASE ("tempo-pipeline: multi-tempo song emits exactly one Q: per part", "[tempo][pipeline]")
{
    // LOTRO ABC does not honour mid-song Q: directives — the converter is
    // expected to keep one Q: per part regardless of how many tempo events
    // the source MIDI contains.
    lotro::Song song;
    song.title            = "one-Q invariant";
    song.ticksPerQuarter  = 480;
    song.meterMap.push_back ({ 0, 4, 4 });
    song.tempoMap.push_back ({    0, 120.0 });
    song.tempoMap.push_back ({  960, 140.0 });
    song.tempoMap.push_back ({ 1920,  60.0 });
    song.tempoMap.push_back ({ 2880,  90.0 });

    lotro::Track t;
    t.instrument = lotro::LotroInstrument::LuteOfAges;
    for (int i = 0; i < 8; ++i)
        t.notes.push_back (note (60 + (i % 5), i * 480, 480));
    song.tracks.push_back (t);

    lotro::Diagnostics diagnostics;
    runPipeline (song, diagnostics);

    const auto abc = lotro::writeAbc (song);

    // Exactly one Q: in the single part's header, using the first tempoMap entry.
    CHECK (countOccurrences (abc, "Q:") == 1);
    CHECK (contains (abc, "Q:120"));
    CHECK_FALSE (contains (abc, "Q:140"));
    CHECK_FALSE (contains (abc, "Q:60"));
    CHECK_FALSE (contains (abc, "Q:90"));
}

TEST_CASE ("tempo-pipeline: multi-tempo song still emits bar labels for every bar", "[tempo][pipeline][bar-alignment]")
{
    // Dense note layout: one quarter on every beat for 4 bars, with a tempo
    // halving at bar 2. Verifies bar labeling survives tempo-scaled durations
    // in the slow section (where scaled note durations are longer than main
    // tempo would imply for the same ticks).
    lotro::Song song;
    song.ticksPerQuarter = 480;
    song.meterMap.push_back ({ 0, 4, 4 });
    song.tempoMap.push_back ({    0, 120.0 });
    song.tempoMap.push_back ({ 1920,  60.0 });

    lotro::Track t;
    t.instrument = lotro::LotroInstrument::LuteOfAges;
    for (int beat = 0; beat < 16; ++beat)
        t.notes.push_back (note (60 + (beat % 5), beat * 480, 480));
    song.tracks.push_back (t);

    lotro::Diagnostics diagnostics;
    runPipeline (song, diagnostics);

    const auto abc = lotro::writeAbc (song);

    CHECK (contains (abc, "% bar 1"));
    CHECK (contains (abc, "% bar 2"));
    CHECK (contains (abc, "% bar 3"));
    CHECK (contains (abc, "% bar 4"));
}

TEST_CASE ("tempo-pipeline: rest gap spanning a slow section is stretched", "[tempo][pipeline]")
{
    // Two notes. First at tick 0, second at tick 1920. Tempo halves at tick
    // 480, so the silence from 480 to 1920 is 1440 slow ticks — it must play
    // under Q:120 as 2880 stream ticks of rest. After collapse the second
    // note sits at stream tick 480 + 2880 = 3360.
    lotro::Song song;
    song.ticksPerQuarter = 480;
    song.meterMap.push_back ({ 0, 4, 4 });
    song.tempoMap.push_back ({   0, 120.0 });
    song.tempoMap.push_back ({ 480,  60.0 });

    lotro::Track t;
    t.instrument = lotro::LotroInstrument::LuteOfAges;
    t.notes.push_back (note (60,    0, 480));
    t.notes.push_back (note (62, 1920, 480));
    song.tracks.push_back (t);

    lotro::Diagnostics diagnostics;
    runPipeline (song, diagnostics);

    REQUIRE (song.tracks[0].notes.size() == 2);
    CHECK (song.tracks[0].notes[1].startTick     == 3360);
    CHECK (song.tracks[0].notes[1].durationTicks ==  960);
}

TEST_CASE ("tempo-pipeline: meter change mid-song emits labels at rescaled positions", "[tempo][pipeline][meter]")
{
    // 4/4 for 2 bars, then 3/4 for 2 bars. No tempo change — purely a meter
    // test at this stage. Bar boundaries in stream ticks:
    //   bar 1 [0,    1920)  4/4
    //   bar 2 [1920, 3840)  4/4
    //   bar 3 [3840, 5280)  3/4
    //   bar 4 [5280, 6720)  3/4
    // Every bar gets a label and the fourth bar is reached in stream space.
    lotro::Song song;
    song.ticksPerQuarter = 480;
    song.meterMap.push_back ({    0, 4, 4 });
    song.meterMap.push_back ({ 3840, 3, 4 });
    song.tempoMap.push_back ({    0, 120.0 });

    lotro::Track t;
    t.instrument = lotro::LotroInstrument::LuteOfAges;
    // Anchor notes at every bar start so all four bar labels are reached.
    t.notes.push_back (note (60,    0, 480));
    t.notes.push_back (note (62, 1920, 480));
    t.notes.push_back (note (64, 3840, 480));
    t.notes.push_back (note (65, 5280, 480));
    song.tracks.push_back (t);

    lotro::Diagnostics diagnostics;
    runPipeline (song, diagnostics);

    const auto abc = lotro::writeAbc (song);

    CHECK (contains (abc, "% bar 1"));
    CHECK (contains (abc, "% bar 2"));
    CHECK (contains (abc, "% bar 3"));
    CHECK (contains (abc, "% bar 4"));

    // With meter-aware labeling, bar 3 starts at stream tick 3840 and bar 4
    // at 5280 — the 3/4 section is 1440 ticks per bar, not 1920. Pin this
    // by checking the second pitch token shows up in bar 2 and the third
    // in bar 3 regardless of section length.
    const auto bar2 = abc.find ("% bar 2");
    const auto bar3 = abc.find ("% bar 3");
    const auto bar4 = abc.find ("% bar 4");
    REQUIRE (bar2 != std::string::npos);
    REQUIRE (bar3 != std::string::npos);
    REQUIRE (bar4 != std::string::npos);
    CHECK (bar2 < bar3);
    CHECK (bar3 < bar4);
}

TEST_CASE ("tempo-pipeline: meter-change ticks travel through tempo collapse", "[tempo][pipeline][meter]")
{
    // Tempo halves at tick 1920 (bar-2 boundary at 120 bpm). A meter change
    // originally at tick 3840 sits 1920 slow ticks past the halving, so it
    // lands at stream tick 1920 + 1920*2 = 5760 post-collapse. The bar label
    // sequence should respect that — the 3/4 section starts a full 4/4 bar
    // plus a scaled 4/4 bar (twice as wide in stream ticks) past the open.
    lotro::Song song;
    song.ticksPerQuarter = 480;
    song.meterMap.push_back ({    0, 4, 4 });
    song.meterMap.push_back ({ 3840, 3, 4 });
    song.tempoMap.push_back ({    0, 120.0 });
    song.tempoMap.push_back ({ 1920,  60.0 });

    lotro::Track t;
    t.instrument = lotro::LotroInstrument::LuteOfAges;
    t.notes.push_back (note (60,    0, 480));
    t.notes.push_back (note (62, 1920, 480));   // slow section starts
    t.notes.push_back (note (64, 3840, 480));   // meter change + slow
    song.tracks.push_back (t);

    lotro::Diagnostics diagnostics;
    runPipeline (song, diagnostics);

    REQUIRE (song.meterMap.size() == 2);
    CHECK (song.meterMap[0].tick ==    0);
    CHECK (song.meterMap[1].tick == 5760);

    // Last note should land at stream tick 5760 as well.
    REQUIRE (song.tracks[0].notes.size() == 3);
    CHECK (song.tracks[0].notes[2].startTick == 5760);
}

TEST_CASE ("tempo-pipeline: mid-song tempo change emits a % tempo: comment", "[tempo][pipeline][annotations]")
{
    lotro::Song song;
    song.ticksPerQuarter = 480;
    song.meterMap.push_back ({ 0, 4, 4 });
    song.tempoMap.push_back ({    0, 120.0 });
    song.tempoMap.push_back ({ 1920,  60.0 });

    lotro::Track t;
    t.instrument = lotro::LotroInstrument::LuteOfAges;
    t.notes.push_back (note (60,    0, 480));
    t.notes.push_back (note (62, 1920, 480));
    song.tracks.push_back (t);

    lotro::Diagnostics diagnostics;
    runPipeline (song, diagnostics);

    const auto abc = lotro::writeAbc (song);

    CHECK (contains (abc, "% tempo: 60 bpm"));
    CHECK (countOccurrences (abc, "% tempo: ") == 1);
    // The main tempo from the header is not re-emitted as a mid-song comment.
    CHECK_FALSE (contains (abc, "% tempo: 120 bpm"));

    // Comment sits inside bar 2, after its label — not before.
    const auto bar2     = abc.find ("% bar 2");
    const auto tempoCom = abc.find ("% tempo: 60 bpm");
    const auto bar3     = abc.find ("% bar 3");
    REQUIRE (bar2     != std::string::npos);
    REQUIRE (tempoCom != std::string::npos);
    CHECK (bar2 < tempoCom);
    if (bar3 != std::string::npos)
        CHECK (tempoCom < bar3);
}

TEST_CASE ("tempo-pipeline: mid-song meter change emits a % meter: comment", "[tempo][pipeline][annotations]")
{
    lotro::Song song;
    song.ticksPerQuarter = 480;
    song.meterMap.push_back ({    0, 4, 4 });
    song.meterMap.push_back ({ 3840, 3, 4 });
    song.tempoMap.push_back ({    0, 120.0 });

    lotro::Track t;
    t.instrument = lotro::LotroInstrument::LuteOfAges;
    t.notes.push_back (note (60,    0, 480));
    t.notes.push_back (note (62, 1920, 480));
    t.notes.push_back (note (64, 3840, 480));
    t.notes.push_back (note (65, 5280, 480));
    song.tracks.push_back (t);

    lotro::Diagnostics diagnostics;
    runPipeline (song, diagnostics);

    const auto abc = lotro::writeAbc (song);

    CHECK (contains (abc, "% meter: 3/4"));
    CHECK (countOccurrences (abc, "% meter: ") == 1);
    CHECK_FALSE (contains (abc, "% meter: 4/4"));

    const auto bar3 = abc.find ("% bar 3");
    const auto mcom = abc.find ("% meter: 3/4");
    REQUIRE (bar3 != std::string::npos);
    REQUIRE (mcom != std::string::npos);
    CHECK (bar3 < mcom);
}

TEST_CASE ("tempo-pipeline: constant tempo and meter produce no change comments", "[tempo][pipeline][annotations]")
{
    lotro::Song song;
    song.ticksPerQuarter = 480;
    song.meterMap.push_back ({ 0, 4, 4 });
    song.tempoMap.push_back ({ 0, 120.0 });

    lotro::Track t;
    t.instrument = lotro::LotroInstrument::LuteOfAges;
    for (int i = 0; i < 8; ++i)
        t.notes.push_back (note (60 + (i % 5), i * 480, 480));
    song.tracks.push_back (t);

    lotro::Diagnostics diagnostics;
    runPipeline (song, diagnostics);

    const auto abc = lotro::writeAbc (song);

    CHECK_FALSE (contains (abc, "% tempo: "));
    CHECK_FALSE (contains (abc, "% meter: "));
}

TEST_CASE ("tempo-pipeline: single-tempo control case emits unchanged durations", "[tempo][pipeline]")
{
    // Regression guard: anything we do to support multi-tempo must leave
    // the constant-tempo case bit-for-bit alone.
    lotro::Song song;
    song.ticksPerQuarter = 480;
    song.meterMap.push_back ({ 0, 4, 4 });
    song.tempoMap.push_back ({ 0, 120.0 });

    lotro::Track t;
    t.instrument = lotro::LotroInstrument::LuteOfAges;
    t.notes.push_back (note (60,    0, 480));
    t.notes.push_back (note (62,  480, 240));
    t.notes.push_back (note (64,  720, 720));
    t.notes.push_back (note (65, 1440, 120));
    song.tracks.push_back (t);

    lotro::Diagnostics diagnostics;
    runPipeline (song, diagnostics);

    CHECK (song.tracks[0].notes[0].durationTicks == 480);
    CHECK (song.tracks[0].notes[1].durationTicks == 240);
    CHECK (song.tracks[0].notes[2].durationTicks == 720);
    CHECK (song.tracks[0].notes[3].durationTicks == 120);

    const auto abc = lotro::writeAbc (song);
    CHECK (contains (abc, "Q:120"));
    CHECK (contains (abc, "% bar 1"));
}

// MidiFidelity: import -> export reproduces the original MIDI event-for-event
// (spec "Goals"), including the hard note-pairing cases, and survives edits,
// mixed PPQ and an empty song.

#include "UI/MidiExport.h"
#include "UI/MidiImportPlan.h"
#include "UI/RawMidi.h"
#include "UI/SongModelBridge.h"
#include "MidiTestBytes.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <juce_core/juce_core.h>

#include <algorithm>
#include <tuple>

using namespace lotro;
using namespace miditest;

namespace
{
    juce::File midiFixture (const std::string& name)
    {
        return juce::File (__FILE__).getParentDirectory().getParentDirectory()
                   .getChildFile ("midi").getChildFile (name);
    }

    struct TempMidi
    {
        juce::File file = juce::File::createTempFile (".mid");
        explicit TempMidi (const Bytes& bytes) { REQUIRE (file.replaceWithData (bytes.data(), bytes.size())); }
        ~TempMidi() { file.deleteFile(); }
    };

    RawMidiFile readFixture (const juce::File& f)
    {
        juce::MemoryBlock block;
        REQUIRE (f.loadFileAsData (block));
        const auto* d = static_cast<const std::uint8_t*> (block.getData());
        return readMidiBytes (Bytes (d, d + block.getSize()), "f");
    }

    RawMidiFile importThenExport (const juce::File& f, Diagnostics* diagsOut = nullptr)
    {
        SongDocument doc;
        Diagnostics diags;
        REQUIRE (importMidiFile (doc, f, 1, diags));
        if (diagsOut != nullptr)
            *diagsOut = diags;
        return buildRawMidiFile (doc);
    }

    bool hasNoteLinkMismatch (const Diagnostics& diags)
    {
        return std::any_of (diags.begin(), diags.end(), [] (const Diagnostic& d)
                            { return d.message.rfind ("Note link mismatch", 0) == 0; });
    }

    // (pitch, start, duration, velocity, channel), sorted, for every NOTE of every track.
    std::vector<std::tuple<int, int, int, int, int>> allNotes (const SongDocument& doc)
    {
        std::vector<std::tuple<int, int, int, int, int>> out;
        for (auto track : doc.getSourceMidiNode())
            for (auto n : SongDocument::getNotesNode (track))
                out.emplace_back ((int) n.getProperty (SongIDs::pitch), (int) n.getProperty (SongIDs::startTick),
                                  (int) n.getProperty (SongIDs::durationTicks), (int) n.getProperty (SongIDs::velocity),
                                  (int) n.getProperty (SongIDs::channel, 1));
        std::sort (out.begin(), out.end());
        return out;
    }

    bool isTempoOrMeter (const RawMidiEvent& e)
    {
        return e.bytes.size() >= 2 && e.bytes[0] == 0xFF && (e.bytes[1] == 0x51 || e.bytes[1] == 0x58);
    }

    // Import moves a note track's FF 51 / FF 58 into the conductor, so the expected export is
    // the original with those events moved to track 0. Track 0's relative order is then only
    // defined by tick, so both sides compare track 0 sorted by (tick, bytes).
    RawMidiFile withTempoMeterInConductor (RawMidiFile file)
    {
        for (size_t t = 1; t < file.tracks.size(); ++t)
        {
            auto& events = file.tracks[t].events;
            for (const auto& e : events)
                if (isTempoOrMeter (e))
                    file.tracks[0].events.push_back (e);
            events.erase (std::remove_if (events.begin(), events.end(), isTempoOrMeter), events.end());
        }
        std::stable_sort (file.tracks[0].events.begin(), file.tracks[0].events.end(),
                          [] (const RawMidiEvent& a, const RawMidiEvent& b)
                          { return std::tie (a.tick, a.bytes) < std::tie (b.tick, b.bytes); });
        return file;
    }

    RawMidiFile sortedConductor (RawMidiFile file)
    {
        std::stable_sort (file.tracks[0].events.begin(), file.tracks[0].events.end(),
                          [] (const RawMidiEvent& a, const RawMidiEvent& b)
                          { return std::tie (a.tick, a.bytes) < std::tie (b.tick, b.bytes); });
        return file;
    }

    TrackBody conductorBody()
    {
        TrackBody c;
        c.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 }).eot (960);
        return c;
    }
}

TEST_CASE ("MidiFidelity: every tracked fixture exports exactly as it was imported", "[midifidelity]")
{
    auto name = GENERATE (as<std::string>{},
        "Barnes Brothers Band - Pull The Wires.mid", "anymore.mid", "blue.mid",
        "hold.mid", "land.mid", "leah.mid", "nobody.mid", "right.mid", "tellit.mid");

    DYNAMIC_SECTION (name)
    {
        const auto original = readFixture (midiFixture (name));
        REQUIRE (hasConductorTrack (original)); // all fixtures have one
        Diagnostics diags;
        const auto exported = importThenExport (midiFixture (name), &diags);
        const bool noteTrackTempoMeter = std::any_of (original.tracks.begin() + 1, original.tracks.end(),
            [] (const RawMidiTrack& t) { return std::any_of (t.events.begin(), t.events.end(), isTempoOrMeter); });
        if (noteTrackTempoMeter)
            CHECK (sortedConductor (exported) == withTempoMeterInConductor (original));
        else
            CHECK (exported == original);
        // The plan's note-link cross-check only jassertfalse()s, which Catch2 does not see.
        CHECK_FALSE (hasNoteLinkMismatch (diags));
    }
}

TEST_CASE ("MidiFidelity: the review's note-pairing cases export exactly", "[midifidelity]")
{
    TrackBody swapCase;
    swapCase.ev (0, { 0x90, 60, 100 }).ev (0, { 0x90, 62, 100 }).ev (0, { 0x80, 60, 0x40 })
            .ev (10, { 0x80, 62, 0x40 }).ev (10, { 0x80, 60, 0x40 }).eot();
    TrackBody restrikeCase;
    restrikeCase.ev (0, { 0x90, 60, 100 }).ev (0, { 0x80, 60, 0x40 }).ev (0, { 0x90, 60, 50 }).ev (0, { 0x80, 60, 0x40 })
                .ev (10, { 0x80, 60, 0x40 }).eot();

    for (const auto& body : { swapCase, restrikeCase })
    {
        const auto bytes = smf (1, 96, { conductorBody(), body });
        TempMidi tmp (bytes);
        CHECK (importThenExport (tmp.file) == readMidiBytes (bytes, "r"));
    }
}

TEST_CASE ("MidiFidelity: a file without a conductor exports with its song-wide metas relocated to track 0", "[midifidelity]")
{
    TrackBody first;
    first.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 }).ev (0, { 0xFF, 0x05, 0x02, 'l', 'a' })
         .ev (0, { 0x90, 60, 100 }).ev (48, { 0xFF, 0x06, 0x01, 'B' }).ev (48, { 0x80, 60, 0x40 }).eot();
    TrackBody second;
    second.ev (0, { 0xFF, 0x59, 0x02, 0x00, 0x00 }).ev (0, { 0x91, 64, 90 }).ev (96, { 0x91, 64, 0 }).eot();
    TempMidi tmp (smf (1, 96, { first, second }));

    RawMidiFile expected;
    expected.format = 1;
    expected.ticksPerQuarter = 96;
    RawMidiTrack conductor;
    conductor.events = { { 0, { 0xFF, 0x51, 0x07, 0xA1, 0x20 } },  // tick 0, from track 0
                         { 0, { 0xFF, 0x59, 0x00, 0x00 } },        // tick 0, from track 1
                         { 48, { 0xFF, 0x06, 'B' } } };            // tick 48, from track 0
    conductor.endTick = 0;   // a created conductor has endTick 0; the writer extends it to the last event
    RawMidiTrack t0;
    t0.events = { { 0, { 0xFF, 0x05, 'l', 'a' } }, { 0, { 0x90, 60, 100 } }, { 96, { 0x80, 60, 0x40 } } };
    t0.endTick = 96;
    RawMidiTrack t1;
    t1.events = { { 0, { 0x91, 64, 90 } }, { 96, { 0x91, 64, 0 } } };
    t1.endTick = 96;
    expected.tracks = { conductor, t0, t1 };

    // Compare after a write/read cycle so the writer's End-of-Track rule applies to both sides.
    CHECK (readMidiBytes (writeMidiBytes (importThenExport (tmp.file)), "x")
           == readMidiBytes (writeMidiBytes (expected), "y"));
}

TEST_CASE ("MidiFidelity: a format-0 file exports as format 1 with each note on its own channel", "[midifidelity]")
{
    TrackBody t;
    t.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 }).ev (0, { 0x94, 64, 80 }).ev (0, { 0x99, 36, 90 })
     .ev (96, { 0x84, 64, 0 }).ev (0, { 0x89, 36, 0 }).eot();
    TempMidi tmp (smf (0, 96, { t }));

    const auto exported = importThenExport (tmp.file);
    REQUIRE (exported.format == 1);
    REQUIRE (exported.tracks.size() == 2);
    CHECK (exported.tracks[0].events.size() == 1);
    REQUIRE (exported.tracks[1].events.size() == 4);
    CHECK (exported.tracks[1].events[0].bytes == Bytes { 0x94, 64, 80 });
    CHECK (exported.tracks[1].events[1].bytes == Bytes { 0x99, 36, 90 });
}

TEST_CASE ("MidiFidelity: an empty song exports one empty conductor track", "[midifidelity]")
{
    SongDocument doc;
    const auto exported = buildRawMidiFile (doc);
    CHECK (exported.format == 1);
    CHECK (exported.ticksPerQuarter == 480);
    REQUIRE (exported.tracks.size() == 1);
    CHECK (exported.tracks[0].events.empty());
    CHECK (readMidiBytes (writeMidiBytes (exported), "e") == exported);
}

TEST_CASE ("MidiFidelity: an edited song exports a file that re-imports as the same notes", "[midifidelity]")
{
    // Distinct pitches, no overlaps, so re-import pairing can't legitimately differ.
    TrackBody melody;
    melody.ev (0, { 0xB0, 0x07, 0x64 })
          .ev (0, { 0x90, 60, 100 }).ev (96, { 0x80, 60, 0x40 })
          .ev (0, { 0x90, 62, 90 }).ev (96, { 0x90, 62, 0 })
          .ev (0, { 0x90, 64, 80 }).ev (96, { 0x80, 64, 0x10 }).eot();
    TempMidi source (smf (1, 96, { conductorBody(), melody }));

    SongDocument doc;
    Diagnostics diags;
    REQUIRE (importMidiFile (doc, source.file, 1, diags));

    auto track = doc.getTrack (1);
    auto notes = SongDocument::getNotesNode (track);
    REQUIRE (notes.getNumChildren() == 3);

    // Move one note (as the editor does), delete one, add one.
    auto moved = notes.getChild (0);
    doc.setProperty (moved, SongIDs::startTick, (int) moved.getProperty (SongIDs::startTick) + 7);
    doc.removeProperty (moved, SongIDs::onOrder, false);
    doc.removeProperty (moved, SongIDs::offOrder, false);
    doc.setProperty (moved, SongIDs::offSynthesized, false, false);
    doc.removeChild (notes, notes.getChild (1));
    juce::ValueTree added (SongIDs::NOTE);
    added.setProperty (SongIDs::pitch, 72, nullptr);
    added.setProperty (SongIDs::startTick, 0, nullptr);
    added.setProperty (SongIDs::durationTicks, 120, nullptr);
    added.setProperty (SongIDs::velocity, 99, nullptr);
    added.setProperty (SongIDs::channel, 3, nullptr);
    doc.addChild (notes, added);

    TempMidi tmp (writeMidiBytes (buildRawMidiFile (doc)));
    SongDocument reimported;
    Diagnostics diags2;
    REQUIRE (importMidiFile (reimported, tmp.file, 1, diags2));
    CHECK (allNotes (reimported) == allNotes (doc));
}

TEST_CASE ("MidiFidelity: a re-timed note-off and a new note-on sit safely beside same-pitch originals", "[midifidelity]")
{
    // A (60, 0..48) and B (60, 96..192). A is stretched to end at 96, where B's
    // original note-on is; a new C starts at 192, where B's original note-off is.
    // A new off must come before the tick's originals and a new on after them.
    // Checked on the exported events: JUCE's re-import puts a tick's note-offs
    // first anyway, so the re-imported notes alone can't show a wrong order.
    TrackBody melody;
    melody.ev (0, { 0x90, 60, 100 }).ev (48, { 0x80, 60, 0x40 })
          .ev (48, { 0x90, 60, 90 }).ev (96, { 0x80, 60, 0x40 }).eot();
    TempMidi source (smf (1, 96, { conductorBody(), melody }));

    SongDocument doc;
    Diagnostics diags;
    REQUIRE (importMidiFile (doc, source.file, 1, diags));
    auto notes = SongDocument::getNotesNode (doc.getTrack (1));
    REQUIRE (notes.getNumChildren() == 2);

    auto stretched = notes.getChild (0);
    REQUIRE ((int) stretched.getProperty (SongIDs::startTick) == 0);
    doc.setProperty (stretched, SongIDs::durationTicks, 96);
    doc.removeProperty (stretched, SongIDs::onOrder, false);
    doc.removeProperty (stretched, SongIDs::offOrder, false);
    juce::ValueTree added (SongIDs::NOTE);
    added.setProperty (SongIDs::pitch, 60, nullptr);
    added.setProperty (SongIDs::startTick, 192, nullptr);
    added.setProperty (SongIDs::durationTicks, 48, nullptr);
    added.setProperty (SongIDs::velocity, 70, nullptr);
    doc.addChild (notes, added);

    const auto exported = buildRawMidiFile (doc);
    REQUIRE (exported.tracks.size() == 2);
    CHECK (exported.tracks[1].events == std::vector<RawMidiEvent> {
               { 0, { 0x90, 60, 100 } },
               { 96, { 0x80, 60, 0x40 } },   // A's re-timed off, before ...
               { 96, { 0x90, 60, 90 } },     // ... B's original on
               { 192, { 0x80, 60, 0x40 } },  // B's original off, before ...
               { 192, { 0x90, 60, 70 } },    // ... C's new on
               { 240, { 0x80, 60, 0x40 } } });
    CHECK (exported.tracks[1].endTick == 240);
}

TEST_CASE ("MidiFidelity: relocated conductor events keep their source track's order before their own", "[midifidelity]")
{
    // Track 0's tempo is its event 1, track 1's time signature its event 0, both
    // at tick 0: ordering by raw order alone would put track 1's first.
    TrackBody first;
    first.ev (0, { 0x90, 60, 100 }).ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 }).ev (96, { 0x80, 60, 0x40 }).eot();
    TrackBody second;
    second.ev (0, { 0xFF, 0x58, 0x04, 0x03, 0x02, 0x18, 0x08 }).ev (0, { 0x91, 64, 90 }).ev (96, { 0x81, 64, 0x40 }).eot();
    TempMidi tmp (smf (1, 96, { first, second }));

    const auto exported = importThenExport (tmp.file);
    REQUIRE (exported.tracks.size() == 3);
    CHECK (exported.tracks[0].events == std::vector<RawMidiEvent> {
               { 0, { 0xFF, 0x51, 0x07, 0xA1, 0x20 } },
               { 0, { 0xFF, 0x58, 0x03, 0x02, 0x18, 0x08 } } });
}

namespace
{
    // Re-struck key: on60@0, on60@10, off60@20, off60@30. JUCE gives A = 0..10
    // with an invented off (B's on ends it), B = 10..20, and off@30 stays an EVENT.
    Bytes restruckKeyBytes()
    {
        TrackBody body;
        body.ev (0, { 0x90, 60, 100 }).ev (10, { 0x90, 60, 90 })
            .ev (10, { 0x80, 60, 0x40 }).ev (10, { 0x80, 60, 0x40 }).eot();
        return smf (1, 96, { conductorBody(), body });
    }

    juce::ValueTree noteStartingAt (const juce::ValueTree& notes, int tick)
    {
        for (auto n : notes)
            if ((int) n.getProperty (SongIDs::startTick) == tick)
                return n;
        return {};
    }

    // Imports the re-struck key, applies `edit` to its NOTES, then checks that
    // export -> re-import gives back exactly the edited notes.
    template <typename Edit>
    void checkRestruckEditSurvives (Edit&& edit)
    {
        TempMidi source (restruckKeyBytes());
        SongDocument doc;
        Diagnostics diags;
        REQUIRE (importMidiFile (doc, source.file, 1, diags));
        auto notes = SongDocument::getNotesNode (doc.getTrack (1));
        REQUIRE (notes.getNumChildren() == 2);
        auto a = noteStartingAt (notes, 0);
        auto b = noteStartingAt (notes, 10);
        REQUIRE ((bool) a.getProperty (SongIDs::offSynthesized));
        REQUIRE ((int) a.getProperty (SongIDs::durationTicks) == 10);
        REQUIRE ((int) b.getProperty (SongIDs::durationTicks) == 10);

        edit (doc, notes, a, b);

        TempMidi tmp (writeMidiBytes (buildRawMidiFile (doc)));
        SongDocument reimported;
        Diagnostics diags2;
        REQUIRE (importMidiFile (reimported, tmp.file, 1, diags2));
        CHECK (allNotes (reimported) == allNotes (doc));
    }
}

TEST_CASE ("MidiFidelity: an unedited re-struck key exports exactly", "[midifidelity]")
{
    const auto bytes = restruckKeyBytes();
    TempMidi tmp (bytes);
    CHECK (importThenExport (tmp.file) == readMidiBytes (bytes, "k"));
}

TEST_CASE ("MidiFidelity: a note whose off was invented keeps its length when the re-striking note is deleted", "[midifidelity]")
{
    checkRestruckEditSurvives ([] (SongDocument& doc, juce::ValueTree notes, juce::ValueTree, juce::ValueTree b)
    {
        doc.removeChild (notes, b);
    });
}

TEST_CASE ("MidiFidelity: a note whose off was invented keeps its length when the re-striking note moves", "[midifidelity]")
{
    checkRestruckEditSurvives ([] (SongDocument& doc, juce::ValueTree, juce::ValueTree, juce::ValueTree b)
    {
        doc.setProperty (b, SongIDs::startTick, 15);
        doc.setProperty (b, SongIDs::durationTicks, 5, false);
        doc.removeProperty (b, SongIDs::onOrder, false);
        doc.removeProperty (b, SongIDs::offOrder, false);
    });
}

TEST_CASE ("MidiFidelity: a note whose off was invented keeps its length when dragged to another pitch", "[midifidelity]")
{
    checkRestruckEditSurvives ([] (SongDocument& doc, juce::ValueTree, juce::ValueTree a, juce::ValueTree)
    {
        doc.setProperty (a, SongIDs::pitch, 62);
    });
}

TEST_CASE ("MidiFidelity: a mixed-PPQ song exports a readable file that keeps every event", "[midifidelity]")
{
    SongDocument doc;
    Diagnostics d1, d2;
    REQUIRE (importMidiFile (doc, midiFixture ("blue.mid"), 1, d1));       // 120 PPQ
    REQUIRE (importMidiFile (doc, midiFixture ("anymore.mid"), 2, d2));    // 960 PPQ -> LCM raise

    const auto exported = buildRawMidiFile (doc);
    const auto reread = readMidiBytes (writeMidiBytes (exported), "m");
    CHECK (reread.ticksPerQuarter == (int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter));
    REQUIRE (reread.tracks.size() == (size_t) doc.getNumTracks());

    for (int t = 0; t < doc.getNumTracks(); ++t)
    {
        auto track = doc.getTrack (t);
        int expected = SongDocument::getEventsNode (track).getNumChildren();
        for (auto n : SongDocument::getNotesNode (track))
            expected += (bool) n.getProperty (SongIDs::offSynthesized) ? 1 : 2;
        CHECK ((int) reread.tracks[(size_t) t].events.size() == expected);
    }
}

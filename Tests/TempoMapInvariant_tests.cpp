// The invariant behind "export equals editor": the maps are always the derivation
// of the conductor's events, through import, undo and save/load.

#include "MidiTestBytes.h"
#include "Core/MidiImporter.h"
#include "UI/MidiImportPlan.h"
#include "UI/SongDocument.h"
#include "UI/MidiExport.h"
#include "UI/SongFile.h"
#include "UI/SongModelBridge.h"
#include "UI/TempoMapSync.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <juce_core/juce_core.h>

#include <algorithm>
#include <sstream>
#include <tuple>

using namespace lotro;
using namespace miditest;
using Catch::Approx;

namespace
{
    struct TempMidi
    {
        juce::File file = juce::File::createTempFile (".mid");
        explicit TempMidi (const Bytes& bytes) { REQUIRE (file.replaceWithData (bytes.data(), bytes.size())); }
        ~TempMidi() { file.deleteFile(); }
    };

    TrackBody conductorWith (std::initializer_list<std::pair<int, std::uint32_t>> tempos)
    {
        TrackBody c;
        int last = 0;
        for (const auto& [tick, us] : tempos)
        {
            c.ev ((std::uint32_t) (tick - last), { 0xFF, 0x51, 0x03, (std::uint8_t) (us >> 16), (std::uint8_t) (us >> 8), (std::uint8_t) us });
            last = tick;
        }
        c.eot (960);
        return c;
    }

    TrackBody melody (int pitch)
    {
        TrackBody t;
        t.ev (0, { 0x90, (std::uint8_t) pitch, 100 }).ev (96, { 0x80, (std::uint8_t) pitch, 0x40 }).eot();
        return t;
    }

    // The map nodes as plain values, to compare with the derivation.
    std::vector<std::pair<int, double>> tempoNodes (const SongDocument& doc)
    {
        std::vector<std::pair<int, double>> out;
        for (auto c : doc.getTempoMapNode())
            out.emplace_back ((int) c.getProperty (SongIDs::tick), (double) c.getProperty (SongIDs::bpm));
        return out;
    }

    std::vector<std::tuple<int, int, int>> meterNodes (const SongDocument& doc)
    {
        std::vector<std::tuple<int, int, int>> out;
        for (auto c : doc.getMeterMapNode())
            out.emplace_back ((int) c.getProperty (SongIDs::tick), (int) c.getProperty (SongIDs::numerator),
                              (int) c.getProperty (SongIDs::denominator));
        return out;
    }

    void requireMapsMatchEvents (const SongDocument& doc)
    {
        const auto derived = deriveMaps (conductorEventsOf (doc));
        REQUIRE (tempoNodes (doc).size() == derived.tempo.size());
        for (size_t i = 0; i < derived.tempo.size(); ++i)
        {
            CHECK (tempoNodes (doc)[i].first == derived.tempo[i].tick);
            CHECK (tempoNodes (doc)[i].second == Approx (derived.tempo[i].bpm));
        }
        REQUIRE (meterNodes (doc).size() == derived.meter.size());
        for (size_t i = 0; i < derived.meter.size(); ++i)
            CHECK (meterNodes (doc)[i] == std::make_tuple (derived.meter[i].tick, derived.meter[i].numerator, derived.meter[i].denominator));
    }

    bool isTempoOrMeter (const std::vector<std::uint8_t>& b)
    {
        return b.size() >= 2 && b[0] == 0xFF && (b[1] == 0x51 || b[1] == 0x58);
    }

    // Every FF 51 / FF 58 the exported file holds, in EVERY track (conductor first at a
    // shared tick), derived the way a reader would, must equal the map nodes exactly.
    void requireExportMatchesMaps (const SongDocument& doc)
    {
        const auto exported = buildRawMidiFile (doc);
        std::vector<RawMidiEvent> timeEvents;
        for (const auto& track : exported.tracks)
            for (const auto& e : track.events)
                if (isTempoOrMeter (e.bytes))
                    timeEvents.push_back (e);
        std::stable_sort (timeEvents.begin(), timeEvents.end(),
                          [] (const RawMidiEvent& a, const RawMidiEvent& b) { return a.tick < b.tick; });

        const auto derived = deriveMaps (timeEvents);
        REQUIRE (tempoNodes (doc).size() == derived.tempo.size());
        for (size_t i = 0; i < derived.tempo.size(); ++i)
        {
            CHECK (tempoNodes (doc)[i].first == derived.tempo[i].tick);
            CHECK_THAT (tempoNodes (doc)[i].second, Catch::Matchers::WithinULP (derived.tempo[i].bpm, 0));
        }
        REQUIRE (meterNodes (doc).size() == derived.meter.size());
        for (size_t i = 0; i < derived.meter.size(); ++i)
            CHECK (meterNodes (doc)[i] == std::make_tuple (derived.meter[i].tick, derived.meter[i].numerator, derived.meter[i].denominator));
    }

    juce::ValueTree makeEvent (int tick, int order, std::vector<std::uint8_t> bytes)
    {
        juce::ValueTree e (SongIDs::EVENT);
        e.setProperty (SongIDs::tick, tick, nullptr);
        e.setProperty (SongIDs::order, order, nullptr);
        e.setProperty (SongIDs::data, juce::var (juce::MemoryBlock (bytes.data(), bytes.size())), nullptr);
        return e;
    }

    struct MapChangeCounter : juce::ValueTree::Listener
    {
        int changes = 0;
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { ++changes; }
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { ++changes; }
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { ++changes; }
    };
}

TEST_CASE ("a new document has empty maps and no time base", "[tempo-sync]")
{
    SongDocument doc;
    CHECK_FALSE (doc.hasTimeBase());
    CHECK (doc.getTempoMapNode().getNumChildren() == 0);
    CHECK (doc.getMeterMapNode().getNumChildren() == 0);
}

TEST_CASE ("import: the maps are derived from the conductor events (tempo changes and a file with none)", "[tempo-sync]")
{
    TempMidi withTempos (smf (1, 96, { conductorWith ({ { 0, 500000 }, { 96, 600000 } }), melody (60) }));
    TempMidi noTempo (smf (1, 96, { melody (60) }));

    SongDocument doc;
    Diagnostics d;
    REQUIRE (importMidiFile (doc, withTempos.file, doc.mintImportBatch(), d));
    CHECK (doc.hasTimeBase());
    REQUIRE (tempoNodes (doc).size() == 2);
    CHECK (tempoNodes (doc)[1].second == Approx (100.0));
    requireMapsMatchEvents (doc);

    SongDocument bare;
    REQUIRE (importMidiFile (bare, noTempo.file, bare.mintImportBatch(), d));
    REQUIRE (tempoNodes (bare).size() == 1);     // the default, as importMidi seeds
    CHECK (tempoNodes (bare)[0].second == Approx (120.0));
    requireMapsMatchEvents (bare);
}

TEST_CASE ("import: the maps match what importMidi reads, for a tempo that lives in a note track", "[tempo-sync]")
{
    TrackBody cond;
    cond.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 }).eot();
    TrackBody notes;
    notes.ev (0, { 0x90, 60, 100 }).ev (96, { 0xFF, 0x51, 0x03, 0x09, 0x27, 0xC0 })
         .ev (0, { 0x80, 60, 0x40 }).eot();
    const auto bytes = smf (1, 96, { cond, notes });
    TempMidi file (bytes);

    std::istringstream in (std::string (bytes.begin(), bytes.end()), std::ios::binary);
    Diagnostics discard;
    const Song reference = importMidi (in, "t", discard);

    SongDocument doc;
    Diagnostics d;
    REQUIRE (importMidiFile (doc, file.file, doc.mintImportBatch(), d));
    REQUIRE (tempoNodes (doc).size() == reference.tempoMap.size());
    for (size_t i = 0; i < reference.tempoMap.size(); ++i)
    {
        CHECK (tempoNodes (doc)[i].first == reference.tempoMap[i].tick);
        CHECK_THAT (tempoNodes (doc)[i].second, Catch::Matchers::WithinULP (reference.tempoMap[i].bpm, 0));   // bit-identical
    }
}

TEST_CASE ("a later import with the tempo map kept never changes the maps; Replace swaps them", "[tempo-sync]")
{
    TempMidi first  (smf (1, 96, { conductorWith ({ { 0, 500000 } }), melody (60) }));
    TrackBody cond2;
    cond2.ev (0, { 0xFF, 0x51, 0x03, 0x09, 0x27, 0xC0 }).eot();
    TrackBody notes2;   // a tempo change hidden in a note track of a file with a conductor
    notes2.ev (0, { 0x90, 64, 100 }).ev (48, { 0xFF, 0x51, 0x03, 0x0B, 0x71, 0xB0 }).ev (48, { 0x80, 64, 0x40 }).eot();
    TempMidi second (smf (1, 96, { cond2, notes2 }));

    SongDocument kept;
    Diagnostics d;
    REQUIRE (importMidiFile (kept, first.file, kept.mintImportBatch(), d));
    REQUIRE (importMidiFile (kept, second.file, kept.mintImportBatch(), d));
    REQUIRE (tempoNodes (kept).size() == 1);
    CHECK (tempoNodes (kept)[0].second == Approx (120.0));
    requireMapsMatchEvents (kept);
    requireExportMatchesMaps (kept);   // the kept file's note-track tempo does not leak into the export

    SongDocument replaced;
    REQUIRE (importMidiFile (replaced, first.file, replaced.mintImportBatch(), d));
    ImportOptions options;
    options.tempo = TempoMode::replace;
    REQUIRE (importMidiFile (replaced, second.file, replaced.mintImportBatch(), d, options));
    REQUIRE (tempoNodes (replaced).size() == 2);
    CHECK (tempoNodes (replaced)[0].second == Approx (100.0));
    requireMapsMatchEvents (replaced);
    requireExportMatchesMaps (replaced);
}

TEST_CASE ("changing a conductor tempo event rebuilds the maps; undo and redo restore them", "[tempo-sync]")
{
    TempMidi file (smf (1, 96, { conductorWith ({ { 0, 500000 } }), melody (60) }));
    SongDocument doc;
    Diagnostics d;
    REQUIRE (importMidiFile (doc, file.file, doc.mintImportBatch(), d));

    auto events = SongDocument::getEventsNode (doc.getConductorTrack());
    REQUIRE (events.getNumChildren() == 1);
    auto event = events.getChild (0);

    doc.getUndoManager().beginNewTransaction();
    const std::uint8_t faster[] = { 0xFF, 0x51, 0x06, 0x1A, 0x80 };   // stored without the length byte; 400000 us = 150 BPM
    event.setProperty (SongIDs::data, juce::var (juce::MemoryBlock (faster, sizeof faster)), &doc.getUndoManager());
    CHECK (tempoNodes (doc)[0].second == Approx (150.0));

    doc.undo();
    CHECK (tempoNodes (doc)[0].second == Approx (120.0));
    doc.redo();
    CHECK (tempoNodes (doc)[0].second == Approx (150.0));
    requireMapsMatchEvents (doc);
}

TEST_CASE ("an unrelated event or note edit leaves the map nodes untouched", "[tempo-sync]")
{
    TempMidi file (smf (1, 96, { conductorWith ({ { 0, 500000 } }), melody (60) }));
    SongDocument doc;
    Diagnostics d;
    REQUIRE (importMidiFile (doc, file.file, doc.mintImportBatch(), d));

    MapChangeCounter counter;
    auto tempoNode = doc.getTempoMapNode();
    tempoNode.addListener (&counter);

    juce::ValueTree controller (SongIDs::EVENT);
    controller.setProperty (SongIDs::tick, 10, nullptr);
    controller.setProperty (SongIDs::order, 5, nullptr);
    const std::uint8_t cc[] = { 0xB0, 7, 100 };
    controller.setProperty (SongIDs::data, juce::var (juce::MemoryBlock (cc, sizeof cc)), nullptr);
    doc.addChild (SongDocument::getEventsNode (doc.getConductorTrack()), controller);

    CHECK (counter.changes == 0);   // a rebuild with an identical result writes nothing
    tempoNode.removeListener (&counter);
}

TEST_CASE ("save and load: stale maps in the file are rebuilt from its events", "[tempo-sync]")
{
    TempMidi file (smf (1, 96, { conductorWith ({ { 0, 500000 } }), melody (60) }));
    SongDocument doc;
    Diagnostics d;
    REQUIRE (importMidiFile (doc, file.file, doc.mintImportBatch(), d));

    auto tree = doc.getTree().createCopy();
    auto tempoNode = tree.getChildWithName (SongIDs::TEMPO_MAP);
    tempoNode.getChild (0).setProperty (SongIDs::bpm, 33.0, nullptr);   // a disagreement written by an old build

    SongDocument loaded;
    loaded.replaceContents (tree);
    CHECK (loaded.hasTimeBase());
    CHECK (tempoNodes (loaded)[0].second == Approx (120.0));
    requireMapsMatchEvents (loaded);
}

TEST_CASE ("loading a file saved before timeBaseSet existed infers it from the stored tempo map", "[tempo-sync]")
{
    TempMidi file (smf (1, 96, { conductorWith ({ { 0, 500000 } }), melody (60) }));
    SongDocument doc;
    Diagnostics d;
    REQUIRE (importMidiFile (doc, file.file, doc.mintImportBatch(), d));

    auto tree = doc.getTree().createCopy();
    tree.getChildWithName (SongIDs::SOURCE_MIDI).removeProperty (SongIDs::timeBaseSet, nullptr);
    SongDocument loaded;
    loaded.replaceContents (tree);
    CHECK (loaded.hasTimeBase());

    SongDocument fresh;
    auto freshTree = fresh.getTree().createCopy();
    freshTree.getChildWithName (SongIDs::SOURCE_MIDI).removeProperty (SongIDs::timeBaseSet, nullptr);
    SongDocument loadedEmpty;
    loadedEmpty.replaceContents (freshTree);
    CHECK_FALSE (loadedEmpty.hasTimeBase());
}

TEST_CASE ("export contains exactly the maps' tempo and meter changes, across every track", "[tempo-sync]")
{
    TrackBody cond;
    cond.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 })            // 120 BPM
        .ev (0, { 0xFF, 0x58, 0x04, 0x03, 0x02, 0x18, 0x08 })      // 3/4
        .ev (96, { 0xFF, 0x51, 0x03, 0x09, 0x27, 0xC0 })           // 100 BPM
        .ev (0, { 0xFF, 0x58, 0x04, 0x06, 0x03, 0x18, 0x08 })      // 6/8
        .eot (960);
    TempMidi file (smf (1, 96, { cond, melody (60) }));
    SongDocument doc;
    Diagnostics d;
    REQUIRE (importMidiFile (doc, file.file, doc.mintImportBatch(), d));

    REQUIRE (tempoNodes (doc).size() == 2);
    REQUIRE (meterNodes (doc).size() == 2);
    CHECK (meterNodes (doc)[1] == std::make_tuple (96, 6, 8));
    requireExportMatchesMaps (doc);
}

TEST_CASE ("the maps order same-tick conductor events the way export writes them", "[tempo-sync]")
{
    // A native conductor tempo and one relocated from a note track, both at tick 0:
    // export sorts by (tick, relocatedFrom, order), so the native one comes first.
    SongDocument doc;
    doc.setTimeBase (true);
    auto events = SongDocument::getEventsNode (doc.getConductorTrack());
    SongDocument::appendChildBulk (events, makeEvent (0, 5, { 0xFF, 0x51, 0x07, 0xA1, 0x20 }));   // 120 BPM, native
    auto relocated = makeEvent (0, 0, { 0xFF, 0x51, 0x09, 0x27, 0xC0 });                          // 100 BPM
    relocated.setProperty (SongIDs::relocatedFrom, 1, nullptr);
    SongDocument::appendChildBulk (events, relocated);

    requireMapsMatchEvents (doc);
    requireExportMatchesMaps (doc);
}

TEST_CASE ("changing a conductor tempo event into a non-tempo event rebuilds the maps; undo restores", "[tempo-sync]")
{
    TempMidi file (smf (1, 96, { conductorWith ({ { 0, 500000 }, { 96, 600000 } }), melody (60) }));
    SongDocument doc;
    Diagnostics d;
    REQUIRE (importMidiFile (doc, file.file, doc.mintImportBatch(), d));
    REQUIRE (tempoNodes (doc).size() == 2);

    auto events = SongDocument::getEventsNode (doc.getConductorTrack());
    juce::ValueTree second;
    for (auto e : events)
        if ((int) e.getProperty (SongIDs::tick) == 96)
            second = e;
    REQUIRE (second.isValid());

    const std::uint8_t cc[] = { 0xB0, 7, 100 };
    doc.setProperty (second, SongIDs::data, juce::var (juce::MemoryBlock (cc, sizeof cc)));
    CHECK (tempoNodes (doc).size() == 1);
    requireMapsMatchEvents (doc);

    doc.undo();
    CHECK (tempoNodes (doc).size() == 2);
    requireMapsMatchEvents (doc);
}

TEST_CASE ("a rebuild whose result is identical writes nothing to the map nodes", "[tempo-sync]")
{
    TempMidi file (smf (1, 96, { conductorWith ({ { 0, 500000 } }), melody (60) }));
    SongDocument doc;
    Diagnostics d;
    REQUIRE (importMidiFile (doc, file.file, doc.mintImportBatch(), d));

    MapChangeCounter counter;
    auto tempoNode = doc.getTempoMapNode();
    auto meterNode = doc.getMeterMapNode();
    tempoNode.addListener (&counter);
    meterNode.addListener (&counter);

    // Changing a tempo event's order concerns the maps (so they are rebuilt) but leaves them equal.
    auto tempoEvent = SongDocument::getEventsNode (doc.getConductorTrack()).getChild (0);
    doc.setProperty (tempoEvent, SongIDs::order, 7);

    CHECK (counter.changes == 0);
    tempoNode.removeListener (&counter);
    meterNode.removeListener (&counter);
}

TEST_CASE ("loading a legacy file moves note-track tempo and meter events into the conductor", "[tempo-sync]")
{
    // Before tempo/meter always went to the conductor, a file with a conductor kept a
    // note track's FF 51 / FF 58 in that note track while the stored map included them.
    TempMidi file (smf (1, 96, { conductorWith ({ { 0, 500000 } }), melody (60) }));
    SongDocument doc;
    Diagnostics d;
    REQUIRE (importMidiFile (doc, file.file, doc.mintImportBatch(), d));

    auto tree = doc.getTree().createCopy();
    auto sourceMidi = tree.getChildWithName (SongIDs::SOURCE_MIDI);
    sourceMidi.removeProperty (SongIDs::timeBaseSet, nullptr);
    REQUIRE (sourceMidi.getNumChildren() == 2);
    auto noteEvents = SongDocument::getEventsNode (sourceMidi.getChild (1));
    const int eventsBefore = noteEvents.getNumChildren();
    noteEvents.addChild (makeEvent (48, 1, { 0xFF, 0x51, 0x09, 0x27, 0xC0 }), -1, nullptr);         // 100 BPM
    noteEvents.addChild (makeEvent (0, 0, { 0xFF, 0x58, 0x03, 0x02, 0x18, 0x08 }), -1, nullptr);    // 3/4

    SongDocument loaded;
    loaded.replaceContents (tree);

    REQUIRE (tempoNodes (loaded).size() == 2);
    CHECK (tempoNodes (loaded)[1].first == 48);
    CHECK (tempoNodes (loaded)[1].second == Approx (100.0));
    REQUIRE (meterNodes (loaded).size() == 1);
    CHECK (meterNodes (loaded)[0] == std::make_tuple (0, 3, 4));

    const auto loadedNoteEvents = SongDocument::getEventsNode (loaded.getTrack (1));
    CHECK (loadedNoteEvents.getNumChildren() == eventsBefore);
    for (auto e : loadedNoteEvents)
    {
        const auto* block = e.getProperty (SongIDs::data).getBinaryData();
        REQUIRE (block != nullptr);
        const auto* b = static_cast<const std::uint8_t*> (block->getData());
        CHECK_FALSE (isTempoOrMeter (std::vector<std::uint8_t> (b, b + block->getSize())));
    }
    requireMapsMatchEvents (loaded);
    requireExportMatchesMaps (loaded);
    CHECK_FALSE (loaded.canUndo());
}

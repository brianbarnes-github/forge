// Undoable tempo and meter edits: they write only the conductor's FF 51 / FF 58
// EVENTs, the maps follow, and undo/redo restore both.

#include "MidiTestBytes.h"
#include "UI/MidiExport.h"
#include "UI/SongDocument.h"
#include "UI/SongModelBridge.h"
#include "UI/TempoEdit.h"
#include "UI/TempoMapSync.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <cstdint>
#include <limits>
#include <vector>

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

    TrackBody oneNote()
    {
        TrackBody n;
        n.ev (0, { 0x90, 60, 100 }).ev (480, { 0x80, 60, 0x40 }).eot();
        return n;
    }

    // 120 BPM and 4/4 at tick 0, one key-signature event, one note, PPQ 480.
    Bytes standardSong()
    {
        TrackBody c;
        c.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 }).ev (0, { 0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08 })
         .ev (0, { 0xFF, 0x59, 0x02, 0x00, 0x00 }).eot (3840);
        return smf (1, 480, { c, oneNote() });
    }

    struct Loaded
    {
        TempMidi midi;
        SongDocument doc;
        explicit Loaded (const Bytes& bytes = standardSong()) : midi (bytes)
        {
            Diagnostics d;
            REQUIRE (importMidiFile (doc, midi.file, 1, d));
        }
    };

    std::vector<TempoMeterEvent> events (const SongDocument& doc) { return listTempoMeterEvents (doc); }

    std::vector<TempoMeterEvent> tempos (const SongDocument& doc)
    {
        std::vector<TempoMeterEvent> out;
        for (const auto& e : listTempoMeterEvents (doc))
            if (e.kind == TempoMeterKind::tempo)
                out.push_back (e);
        return out;
    }

    // Exact double equality without -Wfloat-equal noise.
    Approx exactly (double value) { return Approx (value).epsilon (0.0).margin (0.0); }

    int conductorEventCount (const SongDocument& doc) { return SongDocument::getEventsNode (doc.getConductorTrack()).getNumChildren(); }

    bool exportHas (const SongDocument& doc, int tick, const std::vector<std::uint8_t>& bytes)
    {
        const auto file = buildRawMidiFile (doc);
        for (const auto& e : file.tracks.front().events)
            if (e.tick == tick && e.bytes == bytes)
                return true;
        return false;
    }
}

TEST_CASE ("TempoEdit: listing shows the imported tempo and meter, by tick", "[tempo-edit]")
{
    Loaded f;
    const auto list = events (f.doc);
    REQUIRE (list.size() == 2);
    CHECK (list[0].kind == TempoMeterKind::tempo);
    CHECK (list[0].bpm == Approx (120.0));
    CHECK (list[1].kind == TempoMeterKind::meter);
    CHECK (list[1].numerator == 4);
}

TEST_CASE ("TempoEdit: setTempo adds an event, the map follows, and undo removes it in one step", "[tempo-edit]")
{
    Loaded f;
    const int before = conductorEventCount (f.doc);
    REQUIRE_FALSE (setTempo (f.doc, 1920, 90.0).has_value());

    CHECK (conductorEventCount (f.doc) == before + 1);
    REQUIRE (f.doc.getTempoMapNode().getNumChildren() == 2);
    CHECK ((int) f.doc.getTempoMapNode().getChild (1).getProperty (SongIDs::tick) == 1920);
    CHECK ((double) f.doc.getTempoMapNode().getChild (1).getProperty (SongIDs::bpm) == Approx (90.0));

    f.doc.undo();
    CHECK (conductorEventCount (f.doc) == before);
    CHECK (f.doc.getTempoMapNode().getNumChildren() == 1);
    f.doc.redo();
    CHECK (f.doc.getTempoMapNode().getNumChildren() == 2);
}

TEST_CASE ("TempoEdit: a typed BPM is stored rounded to whole microseconds, in the event and the map", "[tempo-edit]")
{
    Loaded f;
    REQUIRE_FALSE (setTempo (f.doc, 480, 100.5).has_value());
    const double stored = (double) f.doc.getTempoMapNode().getChild (1).getProperty (SongIDs::bpm);
    REQUIRE (roundedBpm (100.5).has_value());
    CHECK (stored == exactly (*roundedBpm (100.5)));
    CHECK (stored == Approx (100.5).margin (0.001));

    const auto exported = buildRawMidiFile (f.doc);
    const auto derived = deriveMaps (exported.tracks.front().events);
    REQUIRE (derived.tempo.size() == 2);
    CHECK (derived.tempo[1].bpm == exactly (stored));   // what the file will say is what the map says
}

TEST_CASE ("TempoEdit: setTempo on an occupied tick replaces it; the tick-0 tempo can be edited", "[tempo-edit]")
{
    Loaded f;
    const int before = conductorEventCount (f.doc);
    REQUIRE_FALSE (setTempo (f.doc, 0, 80.0).has_value());
    CHECK (conductorEventCount (f.doc) == before);   // replaced, not duplicated
    CHECK (events (f.doc)[0].bpm == Approx (80.0));
}

TEST_CASE ("TempoEdit: BPM outside what MIDI encodes is rejected and changes nothing", "[tempo-edit]")
{
    Loaded f;
    const int before = conductorEventCount (f.doc);
    const bool couldUndo = f.doc.canUndo();
    const int pendingActions = f.doc.getUndoManager().getNumActionsInCurrentTransaction();

    CHECK (setTempo (f.doc, 480, 0.0) == TempoEditError::BpmOutOfRange);
    CHECK (setTempo (f.doc, 480, -5.0) == TempoEditError::BpmOutOfRange);
    CHECK (setTempo (f.doc, 480, 1000.5) == TempoEditError::BpmOutOfRange);
    CHECK (setTempo (f.doc, 480, 3.0) == TempoEditError::BpmOutOfRange);   // > 24-bit microseconds
    CHECK (setTempo (f.doc, -1, 100.0) == TempoEditError::NegativeTick);
    CHECK (moveTempo (f.doc, 0, 480, 2000.0) == TempoEditError::BpmOutOfRange);
    CHECK (moveTempo (f.doc, 0, -5, 100.0) == TempoEditError::NegativeTick);

    CHECK (conductorEventCount (f.doc) == before);
    CHECK (f.doc.canUndo() == couldUndo);   // no transaction opened
    CHECK (f.doc.getUndoManager().getNumActionsInCurrentTransaction() == pendingActions);
}

TEST_CASE ("TempoEdit: moveTempo changes position and value as one step; occupied and missing ticks are rejected", "[tempo-edit]")
{
    Loaded f;
    REQUIRE_FALSE (setTempo (f.doc, 480, 100.0).has_value());
    REQUIRE_FALSE (setTempo (f.doc, 960, 110.0).has_value());

    CHECK (moveTempo (f.doc, 480, 960, 100.0) == TempoEditError::TickOccupied);
    CHECK (moveTempo (f.doc, 1234, 1300, 100.0) == TempoEditError::NoSuchEvent);

    REQUIRE_FALSE (moveTempo (f.doc, 480, 720, 105.0).has_value());
    const auto list = tempos (f.doc);
    CHECK (list[1].tick == 720);
    CHECK (list[1].bpm == Approx (105.0));
    f.doc.undo();
    CHECK (tempos (f.doc)[1].tick == 480);
    CHECK (tempos (f.doc)[1].bpm == Approx (100.0));
}

TEST_CASE ("TempoEdit: moving the tick-0 tempo changes its value but keeps it at tick 0", "[tempo-edit]")
{
    Loaded f;
    REQUIRE_FALSE (moveTempo (f.doc, 0, 480, 90.0).has_value());
    REQUIRE (f.doc.getTempoMapNode().getNumChildren() == 1);
    CHECK (tempos (f.doc)[0].tick == 0);
    CHECK (tempos (f.doc)[0].bpm == Approx (90.0));
}

TEST_CASE ("TempoEdit: moving a tick's tempo takes along the duplicates it shadowed", "[tempo-edit]")
{
    TrackBody c;   // two tempo events on tick 480; the later one is the one in force
    c.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 }).ev (480, { 0xFF, 0x51, 0x03, 0x0F, 0x42, 0x40 })
     .ev (0, { 0xFF, 0x51, 0x03, 0x09, 0x27, 0xC0 }).eot (3840);
    Loaded f (smf (1, 480, { c, oneNote() }));
    REQUIRE (tempos (f.doc).size() == 3);                 // the map lists both; the last applies
    REQUIRE (tempos (f.doc)[2].bpm == Approx (100.0));    // 600000 us

    REQUIRE_FALSE (moveTempo (f.doc, 480, 960, 100.0).has_value());
    const auto list = tempos (f.doc);
    REQUIRE (list.size() == 2);                           // nothing left behind on 480
    CHECK (list[1].tick == 960);
    CHECK (list[1].bpm == Approx (100.0));
    f.doc.undo();
    REQUIRE (tempos (f.doc).size() == 3);
    CHECK (tempos (f.doc)[1].tick == 480);
    CHECK (tempos (f.doc)[2].tick == 480);
    CHECK (tempos (f.doc)[2].bpm == Approx (100.0));
}

TEST_CASE ("TempoEdit: removeTempo removes a later event but never the one at tick 0", "[tempo-edit]")
{
    Loaded f;
    REQUIRE_FALSE (setTempo (f.doc, 480, 100.0).has_value());
    REQUIRE_FALSE (removeTempo (f.doc, 480).has_value());
    CHECK (f.doc.getTempoMapNode().getNumChildren() == 1);
    CHECK (removeTempo (f.doc, 0) == TempoEditError::CannotRemoveFirst);
    CHECK (removeTempo (f.doc, 480) == TempoEditError::NoSuchEvent);
}

TEST_CASE ("TempoEdit: setMeter snaps to a bar start, writes FF 58, and the bars renumber", "[tempo-edit]")
{
    Loaded f;
    REQUIRE_FALSE (setMeter (f.doc, 2000, 3, 4).has_value());   // nearest 4/4 bar line is 1920
    const auto list = events (f.doc);
    REQUIRE (list.size() == 3);
    CHECK (list[2].kind == TempoMeterKind::meter);
    CHECK (list[2].tick == 1920);
    CHECK (list[2].numerator == 3);
    CHECK (list[2].denominator == 4);
    CHECK (exportHas (f.doc, 1920, { 0xFF, 0x58, 3, 2, 24, 8 }));
}

TEST_CASE ("TempoEdit: meter values are validated; editing keeps the original cc/bb bytes", "[tempo-edit]")
{
    Loaded f;
    CHECK (setMeter (f.doc, 1920, 0, 4) == TempoEditError::MeterOutOfRange);
    CHECK (setMeter (f.doc, 1920, 33, 4) == TempoEditError::MeterOutOfRange);
    CHECK (setMeter (f.doc, 1920, 4, 3) == TempoEditError::MeterOutOfRange);
    CHECK (setMeter (f.doc, 1920, 4, 64) == TempoEditError::MeterOutOfRange);
    CHECK (setMeter (f.doc, -1, 4, 4) == TempoEditError::NegativeTick);

    REQUIRE_FALSE (setMeter (f.doc, 0, 6, 8).has_value());   // edit the opening 4/4 in place
    bool found = false;
    const auto file = buildRawMidiFile (f.doc);
    for (const auto& e : file.tracks.front().events)
        if (e.tick == 0 && e.bytes.size() == 6 && e.bytes[1] == 0x58)
            found = (e.bytes == std::vector<std::uint8_t> { 0xFF, 0x58, 6, 3, 0x18, 0x08 });   // cc/bb of the imported event
    CHECK (found);
}

TEST_CASE ("TempoEdit: moveMeter and removeMeter obey the same rules", "[tempo-edit]")
{
    Loaded f;
    REQUIRE_FALSE (setMeter (f.doc, 1920, 3, 4).has_value());
    CHECK (moveMeter (f.doc, 1920, 0, 3, 4) == TempoEditError::TickOccupied);
    CHECK (moveMeter (f.doc, 960, 2880, 3, 4) == TempoEditError::NoSuchEvent);

    // Snapped with the meters in force before it: once the 3/4 leaves 1920 everything
    // before 3840 is 4/4, so 3700 snaps to 3840 (not to a 3/4 bar line such as 3360).
    REQUIRE_FALSE (moveMeter (f.doc, 1920, 3700, 5, 4).has_value());
    const auto moved = events (f.doc).back();
    CHECK (moved.tick == 3840);
    CHECK (moved.numerator == 5);
    f.doc.undo();
    CHECK (events (f.doc).back().tick == 1920);
    CHECK (events (f.doc).back().numerator == 3);
    f.doc.redo();

    CHECK (removeMeter (f.doc, 0) == TempoEditError::CannotRemoveFirst);
    REQUIRE_FALSE (removeMeter (f.doc, events (f.doc).back().tick).has_value());
    CHECK (f.doc.getMeterMapNode().getNumChildren() == 1);
}

TEST_CASE ("TempoEdit: other conductor events and every note are untouched by an edit", "[tempo-edit]")
{
    Loaded f;
    const auto notesBefore = SongDocument::getNotesNode (f.doc.getTrack (1)).createCopy();
    REQUIRE_FALSE (setTempo (f.doc, 480, 100.0).has_value());
    REQUIRE_FALSE (setMeter (f.doc, 1920, 3, 4).has_value());

    bool keySignatureKept = false;
    for (auto e : SongDocument::getEventsNode (f.doc.getConductorTrack()))
        if (const auto* b = e.getProperty (SongIDs::data).getBinaryData())
            if (b->getSize() >= 2 && static_cast<const std::uint8_t*> (b->getData())[1] == 0x59)
                keySignatureKept = true;
    CHECK (keySignatureKept);
    CHECK (SongDocument::getNotesNode (f.doc.getTrack (1)).isEquivalentTo (notesBefore));
}

TEST_CASE ("TempoEdit: a new event is ordered after the events already on its tick", "[tempo-edit]")
{
    Loaded f;
    REQUIRE_FALSE (setTempo (f.doc, 1920, 100.0).has_value());
    REQUIRE_FALSE (setMeter (f.doc, 1920, 3, 4).has_value());
    int tempoOrder = -1, meterOrder = -1;
    for (auto e : SongDocument::getEventsNode (f.doc.getConductorTrack()))
        if ((int) e.getProperty (SongIDs::tick) == 1920)
        {
            CHECK_FALSE (e.hasProperty (SongIDs::relocatedFrom));
            const auto* b = e.getProperty (SongIDs::data).getBinaryData();
            REQUIRE (b != nullptr);
            (static_cast<const std::uint8_t*> (b->getData())[1] == 0x51 ? tempoOrder : meterOrder) = (int) e.getProperty (SongIDs::order);
        }
    CHECK (tempoOrder == 0);
    CHECK (meterOrder == 1);
    CHECK (exportHas (f.doc, 1920, { 0xFF, 0x58, 3, 2, 24, 8 }));
}

TEST_CASE ("TempoEdit: before any import every edit is rejected", "[tempo-edit]")
{
    SongDocument doc;
    CHECK (setTempo (doc, 0, 120.0) == TempoEditError::NoTimeBase);
    CHECK (setMeter (doc, 0, 4, 4) == TempoEditError::NoTimeBase);
    CHECK (removeTempo (doc, 480) == TempoEditError::NoTimeBase);
    CHECK (moveTempo (doc, 0, 0, 120.0) == TempoEditError::NoTimeBase);
    CHECK (moveMeter (doc, 0, 0, 4, 4) == TempoEditError::NoTimeBase);
    CHECK (removeMeter (doc, 480) == TempoEditError::NoTimeBase);
    CHECK (SongDocument::getEventsNode (doc.getConductorTrack()).getNumChildren() == 0);
    CHECK (listTempoMeterEvents (doc).empty());
}

TEST_CASE ("TempoEdit: after an edit, undo, redo and the export all agree with the maps", "[tempo-edit]")
{
    Loaded f;
    REQUIRE_FALSE (setTempo (f.doc, 480, 100.0).has_value());
    REQUIRE_FALSE (setMeter (f.doc, 1920, 3, 4).has_value());

    const auto check = [&]
    {
        const auto derived = deriveMaps (buildRawMidiFile (f.doc).tracks.front().events);
        REQUIRE (derived.tempo.size() == (size_t) f.doc.getTempoMapNode().getNumChildren());
        REQUIRE (derived.meter.size() == (size_t) f.doc.getMeterMapNode().getNumChildren());
        for (size_t i = 0; i < derived.tempo.size(); ++i)
        {
            CHECK (derived.tempo[i].tick == (int) f.doc.getTempoMapNode().getChild ((int) i).getProperty (SongIDs::tick));
            CHECK (derived.tempo[i].bpm == exactly ((double) f.doc.getTempoMapNode().getChild ((int) i).getProperty (SongIDs::bpm)));
        }
        for (size_t i = 0; i < derived.meter.size(); ++i)
            CHECK (derived.meter[i].tick == (int) f.doc.getMeterMapNode().getChild ((int) i).getProperty (SongIDs::tick));
    };
    check();
    f.doc.undo(); check();
    CHECK (f.doc.getMeterMapNode().getNumChildren() == 1);
    f.doc.undo(); check();
    CHECK (f.doc.getTempoMapNode().getNumChildren() == 1);
    f.doc.redo(); check();
    f.doc.redo(); check();
    CHECK (f.doc.getMeterMapNode().getNumChildren() == 2);
}

namespace
{
    // A conductor with a key signature but no tempo and no meter: the maps start as the implicit defaults.
    Bytes songWithoutTempoOrMeter()
    {
        TrackBody c;
        c.ev (0, { 0xFF, 0x59, 0x02, 0x00, 0x00 }).eot (3840);
        return smf (1, 480, { c, oneNote() });
    }

    int conductorEventsOfKind (const SongDocument& doc, std::uint8_t metaType)
    {
        int count = 0;
        for (auto e : SongDocument::getEventsNode (doc.getConductorTrack()))
            if (const auto* b = e.getProperty (SongIDs::data).getBinaryData())
                if (b->getSize() >= 2 && static_cast<const std::uint8_t*> (b->getData())[1] == metaType)
                    ++count;
        return count;
    }
}

TEST_CASE ("TempoEdit: before any edit the implicit default is listed, but only set works on it", "[tempo-edit]")
{
    Loaded f (songWithoutTempoOrMeter());
    REQUIRE (events (f.doc).size() == 2);
    CHECK (events (f.doc)[0].bpm == Approx (120.0));
    CHECK (conductorEventsOfKind (f.doc, 0x51) == 0);

    CHECK (moveTempo (f.doc, 0, 0, 100.0) == TempoEditError::NoSuchEvent);
    CHECK (moveMeter (f.doc, 0, 0, 3, 4) == TempoEditError::NoSuchEvent);
    CHECK (removeTempo (f.doc, 0) == TempoEditError::CannotRemoveFirst);

    REQUIRE_FALSE (setTempo (f.doc, 0, 100.0).has_value());   // tick 0 itself: just that one event
    CHECK (conductorEventsOfKind (f.doc, 0x51) == 1);
    CHECK (tempos (f.doc).size() == 1);
    CHECK (tempos (f.doc)[0].bpm == Approx (100.0));
}

TEST_CASE ("TempoEdit: the first tempo edit past tick 0 also writes the default at tick 0, in the same undo step", "[tempo-edit]")
{
    Loaded f (songWithoutTempoOrMeter());
    REQUIRE_FALSE (setTempo (f.doc, 480, 90.0).has_value());

    CHECK (conductorEventsOfKind (f.doc, 0x51) == 2);
    const auto list = tempos (f.doc);
    REQUIRE (list.size() == 2);
    CHECK (list[0].tick == 0);
    CHECK (list[0].bpm == Approx (120.0));
    CHECK (list[1].tick == 480);
    CHECK (exportHas (f.doc, 0, { 0xFF, 0x51, 0x07, 0xA1, 0x20 }));
    CHECK (exportHas (f.doc, 480, { 0xFF, 0x51, 0x0A, 0x2C, 0x2B }));   // 666667 us

    // The tick-0 row is now a real event: it moves (in value) like any tick-0 event.
    REQUIRE_FALSE (moveTempo (f.doc, 0, 0, 110.0).has_value());
    CHECK (tempos (f.doc)[0].bpm == Approx (110.0));
    f.doc.undo();

    f.doc.undo();   // one step removes both
    CHECK (conductorEventsOfKind (f.doc, 0x51) == 0);
    CHECK (tempos (f.doc).size() == 1);
    f.doc.redo();
    CHECK (conductorEventsOfKind (f.doc, 0x51) == 2);
}

TEST_CASE ("TempoEdit: the first meter edit past tick 0 also writes the 4/4 default at tick 0, in the same undo step", "[tempo-edit]")
{
    Loaded f (songWithoutTempoOrMeter());
    REQUIRE_FALSE (setMeter (f.doc, 1920, 3, 4).has_value());

    CHECK (conductorEventsOfKind (f.doc, 0x58) == 2);
    REQUIRE (f.doc.getMeterMapNode().getNumChildren() == 2);
    CHECK ((int) f.doc.getMeterMapNode().getChild (0).getProperty (SongIDs::tick) == 0);
    CHECK ((int) f.doc.getMeterMapNode().getChild (0).getProperty (SongIDs::numerator) == 4);
    CHECK (exportHas (f.doc, 0, { 0xFF, 0x58, 0x04, 0x02, 0x18, 0x08 }));
    CHECK (exportHas (f.doc, 1920, { 0xFF, 0x58, 3, 2, 24, 8 }));
    CHECK (moveMeter (f.doc, 0, 0, 2, 4) == std::nullopt);   // a real tick-0 event now

    f.doc.undo();
    f.doc.undo();   // one step removes both
    CHECK (conductorEventsOfKind (f.doc, 0x58) == 0);
    CHECK (f.doc.getMeterMapNode().getNumChildren() == 1);
}

TEST_CASE ("TempoEdit: a tempo Replace import clears the undo history, so undo cannot corrupt the new conductor", "[tempo-edit]")
{
    Loaded f;
    REQUIRE_FALSE (setTempo (f.doc, 480, 100.0).has_value());
    REQUIRE (f.doc.canUndo());

    TrackBody c;
    c.ev (0, { 0xFF, 0x51, 0x03, 0x09, 0x27, 0xC0 }).ev (960, { 0xFF, 0x51, 0x03, 0x0F, 0x42, 0x40 }).eot (3840);
    TempMidi second (smf (1, 480, { c, oneNote() }));
    ImportOptions options;
    options.tempo = TempoMode::replace;
    Diagnostics d;
    REQUIRE (importMidiFile (f.doc, second.file, f.doc.mintImportBatch(), d, options));

    CHECK_FALSE (f.doc.canUndo());
    bool noted = false;
    for (const auto& diagnostic : d)
        if (diagnostic.severity == Severity::Info && diagnostic.message.find ("undo history cleared") != std::string::npos)
            noted = true;
    CHECK (noted);

    const auto conductorBefore = SongDocument::getEventsNode (f.doc.getConductorTrack()).createCopy();
    f.doc.undo();
    f.doc.redo();
    CHECK (SongDocument::getEventsNode (f.doc.getConductorTrack()).isEquivalentTo (conductorBefore));
    const auto list = tempos (f.doc);
    REQUIRE (list.size() == 2);
    CHECK (list[0].bpm == Approx (100.0));
    CHECK (list[1].tick == 960);
    CHECK (list[1].bpm == Approx (60.0));
}

TEST_CASE ("TempoEdit: roundedBpm only answers for a BPM a tempo event can hold", "[tempo-edit]")
{
    CHECK_FALSE (roundedBpm (0.0).has_value());
    CHECK_FALSE (roundedBpm (-5.0).has_value());
    CHECK_FALSE (roundedBpm (std::numeric_limits<double>::quiet_NaN()).has_value());
    CHECK_FALSE (roundedBpm (3.0).has_value());      // > 24-bit microseconds
    CHECK_FALSE (roundedBpm (1000.5).has_value());
    REQUIRE (roundedBpm (maxBpm).has_value());
    CHECK (*roundedBpm (maxBpm) == Approx (1000.0));
}

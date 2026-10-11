// TempoMeterTable: the Tempo/Meter editor's rows, cell parsing and TempoEdit-backed edits.

#include "MidiTestBytes.h"
#include "UI/MidiExport.h"
#include "UI/SongDocument.h"
#include "UI/SongModelBridge.h"
#include "UI/TempoEdit.h"
#include "UI/TempoMeterTable.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <set>
#include <string>
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

    // 120 BPM and 4/4 at tick 0, one note, PPQ 480.
    Bytes standardSong()
    {
        TrackBody c;
        c.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 }).ev (0, { 0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08 })
         .ev (0, { 0xFF, 0x59, 0x02, 0x00, 0x00 }).eot (3840);
        return smf (1, 480, { c, oneNote() });
    }

    // A conductor with neither a tempo nor a meter event: tick 0 lists the implicit defaults.
    Bytes bareSong()
    {
        TrackBody c;
        c.eot (3840);
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

    bool has (const SongDocument& doc, TempoMeterKind kind, int tick)
    {
        for (const auto& e : events (doc))
            if (e.kind == kind && e.tick == tick)
                return true;
        return false;
    }

    TempoMeterEvent find (const SongDocument& doc, TempoMeterKind kind, int tick)
    {
        for (const auto& e : events (doc))
            if (e.kind == kind && e.tick == tick)
                return e;
        FAIL ("no such event in the listing");
        return {};
    }

    bool sameList (const std::vector<TempoMeterEvent>& a, const std::vector<TempoMeterEvent>& b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i].kind != b[i].kind || a[i].tick != b[i].tick || a[i].numerator != b[i].numerator
                || a[i].denominator != b[i].denominator || ! (a[i].bpm == Approx (b[i].bpm).epsilon (0.0).margin (0.0)))
                return false;
        return true;
    }

    bool rejectedWith (const TableEditResult& r, TempoEditError cause)
    {
        return r.has_value() && r->kind == TableEditError::Kind::Rejected && r->cause == cause;
    }

    TempoMeterEvent tempoAt (int tick)
    {
        TempoMeterEvent e;
        e.tick = tick;
        e.bpm = 100.0;
        return e;
    }
}

TEST_CASE ("TempoMeterTable: rows show kind, bar.beat, tick and value in listing order", "[tempo-meter-table]")
{
    Loaded f;
    REQUIRE_FALSE (setTempo (f.doc, 1920, 93.75).has_value());
    REQUIRE_FALSE (setMeter (f.doc, 1920, 3, 4).has_value());

    const auto rows = tableRows (events (f.doc), f.doc.getMeterChanges(), 480);
    REQUIRE (rows.size() == 4);

    CHECK (rows[0].kind == TempoMeterKind::tempo);
    CHECK (rows[0].kindText == "Tempo");
    CHECK (rows[0].barBeatText == "1.1");
    CHECK (rows[0].tickText == "0");
    CHECK (rows[0].valueText == "120");
    CHECK_FALSE (rows[0].tickEditable);

    CHECK (rows[1].kind == TempoMeterKind::meter);
    CHECK (rows[1].kindText == "Meter");
    CHECK (rows[1].barBeatText == "1.1");
    CHECK (rows[1].tickText == "0");
    CHECK (rows[1].valueText == "4/4");
    CHECK_FALSE (rows[1].tickEditable);

    CHECK (rows[2].kind == TempoMeterKind::tempo);
    CHECK (rows[2].barBeatText == "2.1");
    CHECK (rows[2].tickText == "1920");
    CHECK (rows[2].valueText == "93.75");
    CHECK (rows[2].tickEditable);

    CHECK (rows[3].kind == TempoMeterKind::meter);
    CHECK (rows[3].barBeatText == "2.1");
    CHECK (rows[3].tickText == "1920");
    CHECK (rows[3].valueText == "3/4");
    CHECK (rows[3].tickEditable);
}

TEST_CASE ("TempoMeterTable: bar and beat are counted through meter changes", "[tempo-meter-table]")
{
    const std::vector<MeterChange> map { { 0, 4, 4 }, { 1920, 3, 4 } };

    // 1920 + one 3/4 bar of 1440 starts bar 3.
    CHECK (tableRows ({ tempoAt (3360) }, map, 480).front().barBeatText == "3.1");
    // One beat into the 3/4 bar 2, and its last beat.
    CHECK (tableRows ({ tempoAt (2400) }, map, 480).front().barBeatText == "2.2");
    CHECK (tableRows ({ tempoAt (3359) }, map, 480).front().barBeatText == "2.3");
    // The last beat of the 4/4 bar 1.
    CHECK (tableRows ({ tempoAt (1440) }, map, 480).front().barBeatText == "1.4");
}

TEST_CASE ("TempoMeterTable: parsing accepts exactly well-formed text", "[tempo-meter-table]")
{
    CHECK (parseBpm ("120") == Approx (120.0));
    CHECK (parseBpm (" 93.75 ") == Approx (93.75));
    CHECK (parseBpm ("1e2") == Approx (100.0));
    for (const char* bad : { "", "abc", "0", "-5", "nan", "inf", "12x" })
        CHECK_FALSE (parseBpm (bad).has_value());

    CHECK (parseMeter ("3/4") == std::make_pair (3, 4));
    CHECK (parseMeter (" 6 / 8 ") == std::make_pair (6, 8));
    for (const char* bad : { "", "3", "3/", "/4", "0/4", "3/0", "a/b", "3/4/5" })
        CHECK_FALSE (parseMeter (bad).has_value());

    CHECK (parseTick ("0") == 0);
    CHECK (parseTick ("1920") == 1920);
    for (const char* bad : { "", "-1", "1.5", "x", "99999999999" })
        CHECK_FALSE (parseTick (bad).has_value());
}

TEST_CASE ("TempoMeterTable: an unparsable cell changes nothing and opens no undo step", "[tempo-meter-table]")
{
    Loaded f;
    f.doc.getUndoManager().clearUndoHistory();
    const auto before = events (f.doc);
    const auto tempoRow = find (f.doc, TempoMeterKind::tempo, 0);
    const auto meterRow = find (f.doc, TempoMeterKind::meter, 0);

    for (const char* bad : { "", "abc", "0", "-5", "nan" })
    {
        const auto r = editValue (f.doc, tempoRow, bad);
        REQUIRE (r.has_value());
        CHECK (r->kind == TableEditError::Kind::UnparsableBpm);
    }
    CHECK (rejectedWith (editValue (f.doc, tempoRow, "1001"), TempoEditError::BpmOutOfRange));
    CHECK (rejectedWith (editValue (f.doc, tempoRow, "1"), TempoEditError::BpmOutOfRange));

    const auto m = editValue (f.doc, meterRow, "3/");
    REQUIRE (m.has_value());
    CHECK (m->kind == TableEditError::Kind::UnparsableMeter);
    CHECK (rejectedWith (editValue (f.doc, meterRow, "3/5"), TempoEditError::MeterOutOfRange));
    CHECK (rejectedWith (editValue (f.doc, meterRow, "33/4"), TempoEditError::MeterOutOfRange));

    const auto t = editTick (f.doc, tempoRow, "-1");
    REQUIRE (t.has_value());
    CHECK (t->kind == TableEditError::Kind::UnparsableTick);

    CHECK (sameList (events (f.doc), before));
    CHECK_FALSE (f.doc.canUndo());
}

TEST_CASE ("TempoMeterTable: editValue and editTick go through TempoEdit", "[tempo-meter-table]")
{
    Loaded f;

    SECTION ("values")
    {
        REQUIRE_FALSE (editValue (f.doc, find (f.doc, TempoMeterKind::tempo, 0), "90").has_value());
        CHECK (find (f.doc, TempoMeterKind::tempo, 0).bpm == Approx (roundedBpm (90).value()));
        REQUIRE_FALSE (editValue (f.doc, find (f.doc, TempoMeterKind::meter, 0), "6/8").has_value());
        const auto m = find (f.doc, TempoMeterKind::meter, 0);
        CHECK (m.numerator == 6);
        CHECK (m.denominator == 8);
    }

    SECTION ("moving a tempo")
    {
        REQUIRE_FALSE (setTempo (f.doc, 1920, 77.0).has_value());
        REQUIRE_FALSE (editTick (f.doc, find (f.doc, TempoMeterKind::tempo, 1920), "960").has_value());
        CHECK_FALSE (has (f.doc, TempoMeterKind::tempo, 1920));
        CHECK (find (f.doc, TempoMeterKind::tempo, 960).bpm == Approx (roundedBpm (77.0).value()));
    }

    SECTION ("a meter snaps to the nearest bar start")
    {
        REQUIRE_FALSE (setMeter (f.doc, 1920, 3, 4).has_value());
        REQUIRE_FALSE (editTick (f.doc, find (f.doc, TempoMeterKind::meter, 1920), "2000").has_value());
        CHECK_FALSE (has (f.doc, TempoMeterKind::meter, 2000));
        const auto m = find (f.doc, TempoMeterKind::meter, 1920);
        CHECK (m.numerator == 3);
        CHECK (m.denominator == 4);
    }

    SECTION ("moving onto an occupied tick is rejected")
    {
        REQUIRE_FALSE (setTempo (f.doc, 1920, 77.0).has_value());
        REQUIRE_FALSE (setTempo (f.doc, 3840, 88.0).has_value());
        f.doc.getUndoManager().clearUndoHistory();
        const auto before = events (f.doc);
        CHECK (rejectedWith (editTick (f.doc, find (f.doc, TempoMeterKind::tempo, 3840), "1920"), TempoEditError::TickOccupied));
        CHECK (sameList (events (f.doc), before));
        CHECK_FALSE (f.doc.canUndo());
    }

    SECTION ("moving the implicit tick-0 row is a NoSuchEvent that changes nothing")
    {
        Loaded bare (bareSong());
        bare.doc.getUndoManager().clearUndoHistory();
        const auto before = events (bare.doc);
        REQUIRE (before.size() == 2);   // the implicit 120 BPM and 4/4 rows
        CHECK (rejectedWith (editTick (bare.doc, find (bare.doc, TempoMeterKind::tempo, 0), "960"), TempoEditError::NoSuchEvent));
        CHECK (sameList (events (bare.doc), before));
        CHECK_FALSE (bare.doc.canUndo());
    }
}

TEST_CASE ("TempoMeterTable: addEvent picks the next free bar after the selection and copies the value in force", "[tempo-meter-table]")
{
    SECTION ("no selection: one bar after the last event of that kind")
    {
        Loaded f;
        REQUIRE_FALSE (addEvent (f.doc, TempoMeterKind::tempo, std::nullopt).has_value());
        CHECK (find (f.doc, TempoMeterKind::tempo, 1920).bpm == Approx (120.0));
    }

    SECTION ("an occupied bar is skipped and the value in force is copied")
    {
        Loaded f;
        REQUIRE_FALSE (setTempo (f.doc, 1920, 93.75).has_value());
        REQUIRE_FALSE (addEvent (f.doc, TempoMeterKind::tempo, find (f.doc, TempoMeterKind::tempo, 0)).has_value());
        CHECK (find (f.doc, TempoMeterKind::tempo, 3840).bpm == Approx (roundedBpm (93.75).value()));
    }

    SECTION ("a meter steps by the bars of the meter in force")
    {
        Loaded f;
        REQUIRE_FALSE (addEvent (f.doc, TempoMeterKind::meter, find (f.doc, TempoMeterKind::meter, 0)).has_value());
        CHECK (find (f.doc, TempoMeterKind::meter, 1920).numerator == 4);

        REQUIRE_FALSE (setMeter (f.doc, 1920, 3, 4).has_value());
        REQUIRE_FALSE (addEvent (f.doc, TempoMeterKind::meter, find (f.doc, TempoMeterKind::meter, 1920)).has_value());
        const auto m = find (f.doc, TempoMeterKind::meter, 3360);   // 1920 + one 3/4 bar of 1440
        CHECK (m.numerator == 3);
        CHECK (m.denominator == 4);
    }

    SECTION ("an unaligned selection goes to the next bar line, not the nearest one")
    {
        for (const int selectedTick : { 1000, 960 })
        {
            Loaded f;
            REQUIRE_FALSE (setTempo (f.doc, selectedTick, 77.0).has_value());
            REQUIRE_FALSE (addEvent (f.doc, TempoMeterKind::tempo, find (f.doc, TempoMeterKind::tempo, selectedTick)).has_value());
            CHECK (find (f.doc, TempoMeterKind::tempo, 1920).bpm == Approx (roundedBpm (77.0).value()));
            CHECK_FALSE (has (f.doc, TempoMeterKind::tempo, 3840));
        }
    }

    SECTION ("an unaligned selection steps by the bars of the segment containing it")
    {
        Loaded f;
        REQUIRE_FALSE (setMeter (f.doc, 1920, 3, 4).has_value());
        REQUIRE_FALSE (setTempo (f.doc, 2000, 77.0).has_value());   // inside the 3/4 bar 1920..3360
        REQUIRE_FALSE (addEvent (f.doc, TempoMeterKind::tempo, find (f.doc, TempoMeterKind::tempo, 2000)).has_value());
        CHECK (find (f.doc, TempoMeterKind::tempo, 3360).bpm == Approx (roundedBpm (77.0).value()));
    }

    SECTION ("the step stops at a meter change that comes before the next bar of the selection's own meter")
    {
        Loaded f;
        REQUIRE_FALSE (setMeter (f.doc, 1920, 3, 4).has_value());
        REQUIRE_FALSE (setTempo (f.doc, 1900, 77.0).has_value());   // last 20 ticks of the 4/4 bar
        REQUIRE_FALSE (addEvent (f.doc, TempoMeterKind::tempo, find (f.doc, TempoMeterKind::tempo, 1900)).has_value());
        CHECK (has (f.doc, TempoMeterKind::tempo, 1920));
    }

    SECTION ("no time base")
    {
        SongDocument empty;
        CHECK (rejectedWith (addEvent (empty, TempoMeterKind::tempo, std::nullopt), TempoEditError::NoTimeBase));
    }
}

TEST_CASE ("TempoMeterTable: removeEvent removes by tick and refuses tick 0", "[tempo-meter-table]")
{
    Loaded f;
    REQUIRE_FALSE (setTempo (f.doc, 1920, 93.75).has_value());
    REQUIRE_FALSE (removeEvent (f.doc, find (f.doc, TempoMeterKind::tempo, 1920)).has_value());
    CHECK_FALSE (has (f.doc, TempoMeterKind::tempo, 1920));

    const auto before = events (f.doc);
    CHECK (rejectedWith (removeEvent (f.doc, find (f.doc, TempoMeterKind::tempo, 0)), TempoEditError::CannotRemoveFirst));
    CHECK (sameList (events (f.doc), before));
}

TEST_CASE ("TempoMeterTable: edits re-find the event and use its stored value, not the caller's copy", "[tempo-meter-table]")
{
    Loaded f;
    REQUIRE_FALSE (setTempo (f.doc, 1920, 120.004).has_value());

    // A stale copy (as a displayed row would keep): the stored bpm is 120.004.
    auto stale = find (f.doc, TempoMeterKind::tempo, 1920);
    stale.bpm = 120.0;
    REQUIRE_FALSE (editTick (f.doc, stale, "960").has_value());
    CHECK (find (f.doc, TempoMeterKind::tempo, 960).bpm == Approx (120.004).margin (0.0001));

    // An event that is gone: nothing is recreated, and NoSuchEvent is reported.
    const auto gone = find (f.doc, TempoMeterKind::tempo, 960);
    REQUIRE_FALSE (removeTempo (f.doc, 960).has_value());
    const auto before = events (f.doc);
    CHECK (rejectedWith (editValue (f.doc, gone, "70"), TempoEditError::NoSuchEvent));
    CHECK (rejectedWith (editTick (f.doc, gone, "480"), TempoEditError::NoSuchEvent));
    CHECK (rejectedWith (removeEvent (f.doc, gone), TempoEditError::NoSuchEvent));
    CHECK (sameList (events (f.doc), before));
}

TEST_CASE ("TempoMeterTable: every message from describe is non-empty and distinct", "[tempo-meter-table]")
{
    std::set<std::string> messages;
    size_t count = 0;
    for (auto kind : { TableEditError::Kind::UnparsableBpm, TableEditError::Kind::UnparsableMeter,
                       TableEditError::Kind::UnparsableTick, TableEditError::Kind::NothingToRemove })
    {
        TableEditError e;
        e.kind = kind;
        messages.insert (describe (e));
        ++count;
    }
    for (auto cause : { TempoEditError::NoTimeBase, TempoEditError::NegativeTick, TempoEditError::BpmOutOfRange,
                        TempoEditError::MeterOutOfRange, TempoEditError::TickOccupied, TempoEditError::NoSuchEvent,
                        TempoEditError::CannotRemoveFirst })
    {
        TableEditError e;
        e.kind = TableEditError::Kind::Rejected;
        e.cause = cause;
        messages.insert (describe (e));
        ++count;
    }
    CHECK (messages.size() == count);
    CHECK (messages.count ("") == 0);
    CHECK (describe ({ TableEditError::Kind::UnparsableBpm, std::nullopt }) == "Enter a tempo in BPM, e.g. 120");
    CHECK (describe ({ TableEditError::Kind::Rejected, TempoEditError::BpmOutOfRange }) == "Tempo must be between 3.58 and 1000 BPM");
}

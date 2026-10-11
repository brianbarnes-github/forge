#include "MidiTestBytes.h"
#include "UI/ConductorMarkers.h"
#include "UI/MidiExport.h"
#include "UI/SongDocument.h"
#include "UI/SongModelBridge.h"
#include "UI/TempoEdit.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <algorithm>

using namespace lotro;
using namespace miditest;

namespace
{
    TempoMeterEvent tempo (int tick, double bpm) { return { TempoMeterKind::tempo, tick, bpm, 0, 0 }; }
    TempoMeterEvent meter (int tick, int n, int d) { return { TempoMeterKind::meter, tick, 0.0, n, d }; }
    const auto xOf  = [] (int tick) { return tick / 10; };          // 0.1 px per tick
    // 6 px per character; the note glyph is three UTF-8 bytes, so count code points, not bytes.
    const auto wide = [] (const std::string& s)
    {
        return (int) std::count_if (s.begin(), s.end(), [] (char c) { return ((unsigned char) c & 0xC0) != 0x80; }) * 6;
    };

    struct TempMidi
    {
        juce::File file = juce::File::createTempFile (".mid");
        explicit TempMidi (const Bytes& bytes) { REQUIRE (file.replaceWithData (bytes.data(), bytes.size())); }
        ~TempMidi() { file.deleteFile(); }
    };

    Bytes threeFourSong()
    {
        TrackBody c;
        c.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 }).ev (0, { 0xFF, 0x58, 0x04, 0x03, 0x02, 0x18, 0x08 }).eot (3840);
        TrackBody n;
        n.ev (0, { 0x90, 60, 100 }).ev (480, { 0x80, 60, 0x40 }).eot();
        return smf (1, 480, { c, n });
    }

    struct Loaded
    {
        TempMidi midi;
        SongDocument doc;
        Loaded() : midi (threeFourSong())
        {
            Diagnostics d;
            REQUIRE (importMidiFile (doc, midi.file, 1, d));
        }
    };
}

TEST_CASE ("ConductorMarkers: BPM is shown with up to two decimals, trailing zeros trimmed", "[conductor-markers]")
{
    CHECK (formatMarkerBpm (120.0) == "120");
    CHECK (formatMarkerBpm (93.75) == "93.75");
    CHECK (formatMarkerBpm (100.5) == "100.5");
    CHECK (formatMarkerBpm (60e6 / 500001.0) == "120");      // rounds, never "119.999998"
}

TEST_CASE ("ConductorMarkers: labels", "[conductor-markers]")
{
    CHECK (markerLabel (tempo (0, 120.0)) == "120");
    CHECK (markerLabel (meter (0, 6, 8)) == "6/8");
}

TEST_CASE ("ConductorMarkers: one marker per event, order and ticks kept", "[conductor-markers]")
{
    const auto m = conductorMarkers ({ tempo (0, 120), meter (0, 4, 4), tempo (960, 90) });
    REQUIRE (m.size() == 3);
    CHECK (m[1].kind == TempoMeterKind::meter);
    CHECK (m[2].tick == 960);
    CHECK (conductorMarkers ({}).empty());
}

TEST_CASE ("ConductorMarkers: placeMarkers thins labels per kind but keeps every marker", "[conductor-markers]")
{
    // Marker x = 0, 20, 40, 60. A label is 5 chars * 6 = 30 px wide and starts 3 px after its marker.
    // Label 0 ends at 33, so the next may start at 33 + 4 = 37: marker 1 (23) hides, marker 2 (43) shows
    // and ends at 73, so marker 3 (63) hides.
    const auto m = conductorMarkers ({ tempo (0, 100.5), tempo (200, 110.5), tempo (400, 120.5), tempo (600, 130.5) });
    const auto p = placeMarkers (m, xOf, wide);
    REQUIRE (p.size() == 4);
    CHECK (p[0].labelShown);
    CHECK_FALSE (p[1].labelShown);
    CHECK (p[2].labelShown);
    CHECK_FALSE (p[3].labelShown);
    CHECK (p[3].x == 60);
}

TEST_CASE ("ConductorMarkers: a label exactly labelGap after the previous one is shown", "[conductor-markers]")
{
    // Label 0 ends at 3 + 30 = 33; a marker at x = 34 starts at 37 = 33 + 4 (shown), one at x = 33 starts at 36 (hidden).
    const auto shown  = placeMarkers (conductorMarkers ({ tempo (0, 100.5), tempo (340, 110.5) }), xOf, wide);
    const auto hidden = placeMarkers (conductorMarkers ({ tempo (0, 100.5), tempo (330, 110.5) }), xOf, wide);
    CHECK (shown[1].labelShown);
    CHECK_FALSE (hidden[1].labelShown);
}

TEST_CASE ("ConductorMarkers: thinning is per kind, so a same-tick tempo and meter both show", "[conductor-markers]")
{
    const auto p = placeMarkers (conductorMarkers ({ tempo (0, 120), meter (0, 4, 4) }), xOf, wide);
    REQUIRE (p.size() == 2);
    CHECK (p[0].labelShown);
    CHECK (p[1].labelShown);
    CHECK (placeMarkers ({}, xOf, wide).empty());
}

TEST_CASE ("ConductorMarkers: listing from the tree equals listing from the document", "[conductor-markers]")
{
    Loaded f;
    const auto fromTree = listTempoMeterEvents (f.doc.getTree());
    const auto fromDoc  = listTempoMeterEvents (f.doc);
    REQUIRE (fromTree.size() == 2);
    REQUIRE (fromTree.size() == fromDoc.size());
    for (size_t i = 0; i < fromTree.size(); ++i)
    {
        CHECK (fromTree[i].kind == fromDoc[i].kind);
        CHECK (fromTree[i].tick == fromDoc[i].tick);
        CHECK (fromTree[i].bpm == fromDoc[i].bpm);
        CHECK (fromTree[i].numerator == fromDoc[i].numerator);
        CHECK (fromTree[i].denominator == fromDoc[i].denominator);
    }
    CHECK (fromTree[1].numerator == 3);
}

TEST_CASE ("ConductorMarkers: placeMarkers stops after stopAfterX, measuring nothing beyond it, and records label widths", "[conductor-markers]")
{
    const auto m = conductorMarkers ({ tempo (0, 100.5), tempo (200, 110.5), tempo (400, 120.5), tempo (600, 130.5) });   // x = 0, 20, 40, 60
    int measured = 0;
    const auto counting = [&] (const std::string& s) { ++measured; return wide (s); };
    const auto p = placeMarkers (m, xOf, counting, 3, 4, 40);
    REQUIRE (p.size() == 3);                 // x = 60 is past the stop
    CHECK (measured == 2);                   // shown: x = 0 and 40 only
    CHECK (p[0].labelWidth == 30);
    CHECK (p[1].labelWidth == 0);
    CHECK (p[2].labelWidth == 30);
    const auto full = placeMarkers (m, xOf, wide);
    CHECK (full[2].labelShown == p[2].labelShown);   // identical decisions up to the stop
}

namespace
{
    PlacedMarker placedAt (TempoMeterKind kind, int tick, int x, int labelWidth = 0)
    {
        return { { kind, tick, "L" }, x, labelWidth > 0, labelWidth };
    }
    constexpr int rowH = 40;   // tempo half y < 20, meter half y >= 20
}

TEST_CASE ("markerAt: a click on a line hits it; empty space and far-off clicks hit nothing", "[conductor-markers][hit]")
{
    const std::vector<PlacedMarker> placed { placedAt (TempoMeterKind::tempo, 480, 100) };
    const auto hit = markerAt (placed, 102, 5, rowH);
    REQUIRE (hit);
    CHECK (hit->kind == TempoMeterKind::tempo);
    CHECK (hit->tick == 480);
    CHECK (markerAt (placed, 96, 5, rowH));
    CHECK_FALSE (markerAt (placed, 105, 5, rowH));
    CHECK_FALSE (markerAt (placed, 95, 5, rowH));
    CHECK_FALSE (markerAt ({}, 100, 5, rowH));
    CHECK (markerAt (placed, 109, 5, rowH, 10));   // tolerance is a parameter
}

TEST_CASE ("markerAt: a tempo and a meter on the same tick are told apart by the half clicked", "[conductor-markers][hit]")
{
    const std::vector<PlacedMarker> placed { placedAt (TempoMeterKind::tempo, 960, 200), placedAt (TempoMeterKind::meter, 960, 200) };
    CHECK (markerAt (placed, 200, 3, rowH)->kind == TempoMeterKind::tempo);
    CHECK (markerAt (placed, 200, 19, rowH)->kind == TempoMeterKind::tempo);
    CHECK (markerAt (placed, 200, 20, rowH)->kind == TempoMeterKind::meter);
    CHECK (markerAt (placed, 200, 39, rowH)->kind == TempoMeterKind::meter);
    // Order in the vector does not matter.
    const std::vector<PlacedMarker> flipped { placed[1], placed[0] };
    CHECK (markerAt (flipped, 200, 3, rowH)->kind == TempoMeterKind::tempo);
}

TEST_CASE ("markerAt: only the kinds present can be hit, whichever half is clicked", "[conductor-markers][hit]")
{
    const std::vector<PlacedMarker> onlyTempo { placedAt (TempoMeterKind::tempo, 480, 100) };
    CHECK (markerAt (onlyTempo, 100, 30, rowH)->kind == TempoMeterKind::tempo);
    const std::vector<PlacedMarker> onlyMeter { placedAt (TempoMeterKind::meter, 480, 100) };
    CHECK (markerAt (onlyMeter, 100, 5, rowH)->kind == TempoMeterKind::meter);
}

TEST_CASE ("markerAt: of two lines within tolerance the nearer wins; a half match beats distance", "[conductor-markers][hit]")
{
    const std::vector<PlacedMarker> twoTempos { placedAt (TempoMeterKind::tempo, 100, 100), placedAt (TempoMeterKind::tempo, 130, 103) };
    CHECK (markerAt (twoTempos, 101, 5, rowH)->tick == 100);
    CHECK (markerAt (twoTempos, 102, 5, rowH)->tick == 130);
    CHECK (markerAt (twoTempos, 103, 5, rowH)->tick == 130);

    const std::vector<PlacedMarker> mixed { placedAt (TempoMeterKind::tempo, 100, 100), placedAt (TempoMeterKind::meter, 130, 103) };
    CHECK (markerAt (mixed, 100, 30, rowH)->kind == TempoMeterKind::meter);   // meter half, farther but matching
    CHECK (markerAt (mixed, 103, 5, rowH)->kind == TempoMeterKind::tempo);
}

TEST_CASE ("markerAt: a shown label is a hit area in its own half only", "[conductor-markers][hit]")
{
    const std::vector<PlacedMarker> placed { placedAt (TempoMeterKind::tempo, 480, 100, 30), placedAt (TempoMeterKind::meter, 480, 100, 24) };
    // tempo label spans x 103 .. 135, meter label 103 .. 129
    CHECK (markerAt (placed, 120, 5, rowH)->kind == TempoMeterKind::tempo);
    CHECK (markerAt (placed, 120, 30, rowH)->kind == TempoMeterKind::meter);
    CHECK (markerAt (placed, 133, 5, rowH)->kind == TempoMeterKind::tempo);
    CHECK_FALSE (markerAt (placed, 133, 30, rowH));   // past the meter label
    CHECK_FALSE (markerAt (placed, 136, 5, rowH));
    // A hidden label (width 0) has no area.
    const std::vector<PlacedMarker> hidden { placedAt (TempoMeterKind::tempo, 480, 100) };
    CHECK_FALSE (markerAt (hidden, 120, 5, rowH));
}

TEST_CASE ("markerAt: markers scrolled just off the left edge still hit within tolerance; the far edge is plain x", "[conductor-markers][hit]")
{
    const std::vector<PlacedMarker> placed { placedAt (TempoMeterKind::tempo, 0, -2), placedAt (TempoMeterKind::meter, 9000, 600) };
    CHECK (markerAt (placed, 0, 5, rowH)->tick == 0);
    CHECK_FALSE (markerAt (placed, 3, 5, rowH));
    CHECK (markerAt (placed, 599, 30, rowH)->tick == 9000);
}

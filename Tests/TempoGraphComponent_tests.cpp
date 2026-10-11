// TempoGraphComponent: the tempo graph of the Tempo/Meter window (drawing and editing).

#include "MidiTestBytes.h"
#include "UI/SongDocument.h"
#include "UI/SongModelBridge.h"
#include "UI/SongsmithColours.h"
#include "UI/TempoEdit.h"
#include "UI/TempoGraphComponent.h"
#include "UI/TempoMeterTable.h"
#include "UI/TimelineViewState.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

#include <cmath>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

using namespace lotro;
using Catch::Approx;

namespace
{
    void importStandard (SongDocument& doc)
    {
        miditest::TrackBody c;
        c.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 }).ev (0, { 0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08 })
         .ev (0, { 0xFF, 0x59, 0x02, 0x00, 0x00 }).eot (3840);
        miditest::TrackBody n;
        n.ev (0, { 0x90, 60, 100 }).ev (480, { 0x80, 60, 0x40 }).eot();
        const auto bytes = miditest::smf (1, 480, { c, n });
        auto file = juce::File::createTempFile (".mid");
        REQUIRE (file.replaceWithData (bytes.data(), bytes.size()));
        Diagnostics d;
        REQUIRE (importMidiFile (doc, file, 1, d));
        file.deleteFile();
        doc.getUndoManager().clearUndoHistory();
    }

    // 120 @ 0, 90 @ 1920, 150 @ 3840.
    void threeTempos (SongDocument& doc)
    {
        importStandard (doc);
        REQUIRE_FALSE (setTempo (doc, 1920, 90));
        REQUIRE_FALSE (setTempo (doc, 3840, 150));
    }

    juce::Image render (juce::Component& c)
    {
        return c.createComponentSnapshot (c.getLocalBounds(), false, 1.0f);
    }

    bool sameRgb (juce::Colour a, juce::uint32 argb)
    {
        const juce::Colour b (argb);
        return a.getRed() == b.getRed() && a.getGreen() == b.getGreen() && a.getBlue() == b.getBlue();
    }
}

TEST_CASE ("TempoGraphComponent: x mirrors the timeline view, higher BPM is higher up", "[tempo-graph]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    threeTempos (doc);
    TimelineViewState view;
    view.setPixelsPerTick (0.1);
    TempoGraphComponent graph (doc, view);
    graph.setSize (600, 160);
    graph.setEvents (listTempoMeterEvents (doc));

    CHECK (graph.xForTick (1920) == view.xForTick (1920));
    const int before = graph.xForTick (1920);
    view.setScrollOffsetTicks (960);
    CHECK (graph.xForTick (1920) == view.xForTick (1920));
    CHECK (graph.xForTick (1920) < before);

    CHECK (graph.yForBpm (150) < graph.yForBpm (90));
    CHECK (graph.plotArea() == graph.getLocalBounds().reduced (0, 8));
}

TEST_CASE ("TempoGraphComponent: painting never crashes across event counts, zooms and sizes", "[tempo-graph]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    threeTempos (doc);
    const auto all = listTempoMeterEvents (doc);
    TimelineViewState view;
    TempoGraphComponent graph (doc, view);
    graph.setSize (600, 160);

    for (const double ppt : { 0.001, 0.1, 2.5 })
    {
        view.setPixelsPerTick (ppt);
        for (size_t n : { size_t (0), size_t (1), all.size() })
        {
            graph.setEvents (std::vector<TempoMeterEvent> (all.begin(), all.begin() + (std::ptrdiff_t) n));
            graph.setSelected (std::vector<TempoMeterEvent> (all.begin(), all.begin() + (std::ptrdiff_t) n));
            CHECK (render (graph).isValid());
        }
    }

    graph.setBounds (0, 0, 0, 0);
    graph.repaint();
    CHECK_NOTHROW (graph.setEvents (all));
}

TEST_CASE ("TempoGraphComponent: draws the point and the step line in the tempo colour", "[tempo-graph]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    threeTempos (doc);
    TimelineViewState view;
    view.setPixelsPerTick (0.1);
    TempoGraphComponent graph (doc, view);
    graph.setSize (600, 160);
    graph.setEvents (listTempoMeterEvents (doc));

    const auto img = render (graph);
    const int x = graph.xForTick (1920), y = graph.yForBpm (90);
    CHECK_FALSE (sameRgb (img.getPixelAt (x, y), SongsmithColours::background));
    CHECK (sameRgb (img.getPixelAt (x + 20, y), SongsmithColours::conductorTempo));
    // Between the points, away from the line, the plot is background.
    CHECK (sameRgb (img.getPixelAt (x + 20, y - 3), SongsmithColours::background));

    // A selected point is filled with the event-marker colour; an unselected one is hollow.
    const int hx = graph.xForTick (3840), hy = graph.yForBpm (150);
    CHECK (sameRgb (img.getPixelAt (hx + 2, hy + 2), SongsmithColours::background));
    TempoMeterEvent high;   // the 150 BPM event at 3840
    for (const auto& e : listTempoMeterEvents (doc))
        if (e.kind == TempoMeterKind::tempo && e.tick == 3840)
            high = e;
    REQUIRE (high.tick == 3840);
    graph.setSelected ({ high });
    CHECK (sameRgb (render (graph).getPixelAt (hx + 2, hy + 2), SongsmithColours::eventMarker));
}

TEST_CASE ("TempoGraphComponent: with no time base nothing is drawn", "[tempo-graph]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument empty;
    TimelineViewState view;
    TempoGraphComponent graph (empty, view);
    graph.setSize (600, 160);
    graph.setEvents ({});

    const auto img = render (graph);
    CHECK (sameRgb (img.getPixelAt (300, 80), SongsmithColours::background));
    CHECK (sameRgb (img.getPixelAt (graph.xForTick (0), graph.yForBpm (120)), SongsmithColours::background));
}

TEST_CASE ("TempoGraphComponent: setEvents and setSelected repaint only on change", "[tempo-graph]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    threeTempos (doc);
    TimelineViewState view;
    TempoGraphComponent graph (doc, view);
    graph.setSize (600, 160);
    const auto events = listTempoMeterEvents (doc);

    graph.setEvents (events);
    const int a = graph.repaintRequestsForTesting();
    graph.setEvents (events);
    CHECK (graph.repaintRequestsForTesting() == a);
    graph.setSelected ({ events[0] });
    CHECK (graph.repaintRequestsForTesting() == a + 1);
    graph.setSelected ({ events[0] });
    CHECK (graph.repaintRequestsForTesting() == a + 1);
}

// ---- editing: select, add, drag ------------------------------------------------------

namespace
{
    juce::MouseEvent makeMouse (juce::Component& c, juce::Point<int> pos, juce::ModifierKeys extra,
                                juce::Point<int> mouseDownPos)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos.toFloat(),
                                 juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | extra.getRawFlags()),
                                 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, mouseDownPos.toFloat(), now, 1,
                                 pos != mouseDownPos);
    }

    void press   (juce::Component& c, juce::Point<int> p)                         { c.mouseDown (makeMouse (c, p, {}, p)); }
    void dragTo  (juce::Component& c, juce::Point<int> p, juce::Point<int> down)  { c.mouseDrag (makeMouse (c, p, {}, down)); }
    void release (juce::Component& c, juce::Point<int> p, juce::Point<int> down)  { c.mouseUp (makeMouse (c, p, {}, down)); }
    void click   (juce::Component& c, juce::Point<int> p)                         { press (c, p); release (c, p, p); }

    using TickBpm = std::vector<std::pair<int, double>>;

    TickBpm tempos (const SongDocument& doc)
    {
        TickBpm out;
        for (const auto& e : listTempoMeterEvents (doc))
            if (e.kind == TempoMeterKind::tempo)
                out.emplace_back (e.tick, e.bpm);
        return out;
    }

    std::optional<double> tempoAt (const SongDocument& doc, int tick)
    {
        for (const auto& [t, bpm] : tempos (doc))
            if (t == tick)
                return bpm;
        return std::nullopt;
    }

    bool isRejected (const TableEditResult& r, TempoEditError cause)
    {
        return r && r->kind == TableEditError::Kind::Rejected && r->cause == cause;
    }

    // A 600 x 160 graph at 0.1 px/tick over a document; onEdited also refreshes the
    // graph's listing (as the window does once the document has changed).
    struct Rig
    {
        SongDocument doc;
        TimelineViewState view;
        std::unique_ptr<TempoGraphComponent> graph;
        std::vector<TableEditResult> edits;
        std::vector<std::pair<TempoMeterKind, int>> selects;

        explicit Rig (bool withThreeTempos = true)
        {
            if (withThreeTempos)
            {
                threeTempos (doc);
                doc.getUndoManager().clearUndoHistory();
            }
            view.setPixelsPerTick (0.1);
            graph = std::make_unique<TempoGraphComponent> (doc, view);
            graph->setSize (600, 160);
            sync();
            graph->onEdited = [this] (const TableEditResult& r) { edits.push_back (r); sync(); };
            graph->onSelect = [this] (TempoMeterKind k, int tick) { selects.emplace_back (k, tick); };
        }

        void sync() { graph->setEvents (listTempoMeterEvents (doc)); }
        juce::Point<int> at (int tick, double bpm) const { return { graph->xForTick (tick), graph->yForBpm (bpm) }; }
        double bpmAtY (const std::vector<TempoMeterEvent>& listing, int y) const
        {
            const auto plot = graph->plotArea();
            return yToBpm (bpmRangeFor (listing), y, plot.getY(), plot.getHeight());
        }
    };

    std::pair<TempoMeterKind, int> tempoSel (int tick) { return { TempoMeterKind::tempo, tick }; }
}

TEST_CASE ("TempoGraphComponent: clicking a point selects it and writes nothing", "[tempo-graph][edit]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Rig r;
    const auto before = tempos (r.doc);
    const auto p = r.at (1920, 90);

    press (*r.graph, p);
    CHECK (r.graph->isDraggingForTesting());
    // A 2 px jitter is still a click, not a drag.
    dragTo (*r.graph, p + juce::Point<int> (2, 2), p);
    release (*r.graph, p + juce::Point<int> (2, 2), p);

    REQUIRE (r.selects.size() == 1);
    CHECK (r.selects[0] == tempoSel (1920));
    CHECK (r.edits.empty());
    CHECK (tempos (r.doc) == before);
    CHECK_FALSE (r.doc.getUndoManager().canUndo());
    CHECK_FALSE (r.graph->isDraggingForTesting());
}

TEST_CASE ("TempoGraphComponent: a drag that returns to its start writes nothing", "[tempo-graph][edit]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Rig r;
    const auto before = tempos (r.doc);
    const auto p = r.at (1920, 90);

    press (*r.graph, p);
    dragTo (*r.graph, p + juce::Point<int> (20, 20), p);
    REQUIRE (r.graph->dragPreviewForTesting().has_value());
    CHECK (r.graph->dragPreviewForTesting()->second != 90.0);   // it really moved meanwhile
    dragTo (*r.graph, p, p);
    release (*r.graph, p, p);

    for (const auto& e : r.edits)
        CHECK_FALSE (e.has_value());
    CHECK (tempos (r.doc) == before);
    CHECK_FALSE (r.doc.getUndoManager().canUndo());
}

TEST_CASE ("TempoGraphComponent: an off-beat tempo is not snapped unless dragged sideways", "[tempo-graph][edit]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Rig r (false);
    importStandard (r.doc);
    REQUIRE_FALSE (setTempo (r.doc, 1925, 90));   // off the beat (480 ticks)
    REQUIRE_FALSE (setTempo (r.doc, 3840, 150));
    r.doc.getUndoManager().clearUndoHistory();
    r.sync();
    const auto before = tempos (r.doc);
    const auto p = r.at (1925, 90);
    // The fixture can show the bug: the nearest beat to the point's x is 1920.
    REQUIRE (snapToBeat (meterSegments (r.doc.getMeterChanges(), 480), r.view.tickForX (p.x)) == 1920);

    SECTION ("a vertical drag changes only the BPM")
    {
        const juce::Point<int> to (p.x, r.graph->yForBpm (130));
        press (*r.graph, p);
        dragTo (*r.graph, to, p);
        REQUIRE (r.graph->dragPreviewForTesting().has_value());
        CHECK (r.graph->dragPreviewForTesting()->first == 1925);
        release (*r.graph, to, p);
        REQUIRE (r.edits.size() == 1);
        CHECK_FALSE (r.edits[0].has_value());
        CHECK_FALSE (tempoAt (r.doc, 1920).has_value());
        REQUIRE (tempoAt (r.doc, 1925).has_value());
        CHECK (*tempoAt (r.doc, 1925) == Approx (130.0).margin (0.01));
    }

    SECTION ("out past the threshold and back writes nothing")
    {
        press (*r.graph, p);
        dragTo (*r.graph, p + juce::Point<int> (20, 20), p);
        dragTo (*r.graph, p, p);
        release (*r.graph, p, p);
        for (const auto& e : r.edits)
            CHECK_FALSE (e.has_value());
        CHECK (tempos (r.doc) == before);
        CHECK_FALSE (r.doc.getUndoManager().canUndo());
    }
}

TEST_CASE ("TempoGraphComponent: a drag previews, then mouse-up moves the tempo in one undo step", "[tempo-graph][edit]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Rig r;
    const auto before = tempos (r.doc);
    const auto p = r.at (1920, 90);
    const juce::Point<int> to (r.graph->xForTick (2400) + 3, r.graph->yForBpm (130));
    // Before the drag nothing is drawn where the moved step line will be.
    const juce::Point<int> onPreviewLine (r.graph->xForTick (2600), to.y);
    CHECK (sameRgb (render (*r.graph).getPixelAt (onPreviewLine.x, onPreviewLine.y), SongsmithColours::background));

    press (*r.graph, p);
    dragTo (*r.graph, to, p);
    REQUIRE (r.graph->dragPreviewForTesting().has_value());
    CHECK (r.graph->dragPreviewForTesting()->first == 2400);
    CHECK (r.graph->dragPreviewForTesting()->second == 130.0);
    CHECK (tempos (r.doc) == before);
    CHECK_FALSE (r.doc.getUndoManager().canUndo());
    // The preview is drawn: the step line now runs at the previewed BPM.
    CHECK (sameRgb (render (*r.graph).getPixelAt (onPreviewLine.x, onPreviewLine.y), SongsmithColours::conductorTempo));

    release (*r.graph, to, p);
    REQUIRE (r.edits.size() == 1);
    CHECK_FALSE (r.edits[0].has_value());
    CHECK_FALSE (tempoAt (r.doc, 1920).has_value());
    REQUIRE (tempoAt (r.doc, 2400).has_value());
    CHECK (*tempoAt (r.doc, 2400) == Approx (130.0).margin (0.01));
    CHECK_FALSE (r.graph->isDraggingForTesting());
    REQUIRE (r.selects.size() == 2);
    CHECK (r.selects.back() == tempoSel (2400));   // the moved point stays selected

    r.doc.undo();
    CHECK (tempos (r.doc) == before);
    CHECK_FALSE (r.doc.getUndoManager().canUndo());
}

TEST_CASE ("TempoGraphComponent: a horizontal drag keeps a fractional BPM as stored", "[tempo-graph][edit]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Rig r;
    REQUIRE_FALSE (setTempo (r.doc, 1920, 93.75));
    r.doc.getUndoManager().clearUndoHistory();
    r.sync();
    const double stored = *tempoAt (r.doc, 1920);
    const auto p = r.at (1920, stored);

    press (*r.graph, p);
    dragTo (*r.graph, { r.graph->xForTick (2400), p.y + 1 }, p);
    release (*r.graph, { r.graph->xForTick (2400), p.y + 1 }, p);

    REQUIRE (tempoAt (r.doc, 2400).has_value());
    CHECK (*tempoAt (r.doc, 2400) == stored);   // not rounded to a whole BPM
}

TEST_CASE ("TempoGraphComponent: dropping onto an occupied tick is rejected and changes nothing", "[tempo-graph][edit]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Rig r;
    const auto before = tempos (r.doc);
    const auto p = r.at (1920, 90);
    const juce::Point<int> to (r.graph->xForTick (3840) + 2, r.graph->yForBpm (100));

    press (*r.graph, p);
    dragTo (*r.graph, to, p);
    release (*r.graph, to, p);

    REQUIRE (r.edits.size() == 1);
    CHECK (isRejected (r.edits[0], TempoEditError::TickOccupied));
    CHECK (tempos (r.doc) == before);
    CHECK_FALSE (r.doc.getUndoManager().canUndo());
    CHECK_FALSE (r.graph->isDraggingForTesting());
    CHECK_FALSE (r.graph->dragPreviewForTesting().has_value());
    CHECK (r.selects == std::vector<std::pair<TempoMeterKind, int>> { tempoSel (1920) });   // not the occupied tick
}

TEST_CASE ("TempoGraphComponent: the tick-0 tempo only changes BPM, explicit or implicit", "[tempo-graph][edit]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SECTION ("explicit tick-0 event")
    {
        Rig r;
        const auto p = r.at (0, 120);
        const juce::Point<int> to (r.graph->xForTick (960), r.graph->yForBpm (140));
        press (*r.graph, p);
        dragTo (*r.graph, to, p);
        REQUIRE (r.graph->dragPreviewForTesting().has_value());
        CHECK (r.graph->dragPreviewForTesting()->first == 0);
        release (*r.graph, to, p);

        REQUIRE (r.edits.size() == 1);
        CHECK_FALSE (r.edits[0].has_value());
        REQUIRE (tempos (r.doc).size() == 3);
        CHECK (tempos (r.doc)[0].first == 0);
        CHECK (tempos (r.doc)[0].second == Approx (140.0).margin (0.01));
        CHECK_FALSE (tempoAt (r.doc, 960).has_value());
    }

    SECTION ("implicit default (no FF 51 in the file)")
    {
        Rig r (false);
        miditest::TrackBody c;
        c.ev (0, { 0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08 }).eot (3840);
        miditest::TrackBody n;
        n.ev (0, { 0x90, 60, 100 }).ev (480, { 0x80, 60, 0x40 }).eot();
        const auto bytes = miditest::smf (1, 480, { c, n });
        auto file = juce::File::createTempFile (".mid");
        REQUIRE (file.replaceWithData (bytes.data(), bytes.size()));
        Diagnostics d;
        REQUIRE (importMidiFile (r.doc, file, 1, d));
        file.deleteFile();
        r.doc.getUndoManager().clearUndoHistory();
        r.sync();
        const auto before = tempos (r.doc);
        REQUIRE (before == TickBpm { { 0, 120.0 } });

        const auto p = r.at (0, 120);
        const juce::Point<int> to (r.graph->xForTick (960), r.graph->yForBpm (100));
        press (*r.graph, p);
        dragTo (*r.graph, to, p);
        release (*r.graph, to, p);

        REQUIRE (r.edits.size() == 1);
        CHECK_FALSE (r.edits[0].has_value());
        REQUIRE (tempos (r.doc).size() == 1);
        CHECK (tempos (r.doc)[0].first == 0);
        CHECK (tempos (r.doc)[0].second == Approx (100.0).margin (0.01));
        r.doc.undo();
        CHECK (tempos (r.doc) == before);
        CHECK_FALSE (r.doc.getUndoManager().canUndo());
    }
}

TEST_CASE ("TempoGraphComponent: releases far outside the graph clamp to values TempoEdit accepts", "[tempo-graph][edit]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SECTION ("far below: the minimum BPM")
    {
        Rig r;
        const auto p = r.at (3840, 150);
        press (*r.graph, p);
        dragTo (*r.graph, p + juce::Point<int> (0, 10), p);
        release (*r.graph, { p.x, 5000 }, p);   // no drag event out there
        REQUIRE (r.edits.size() == 1);
        CHECK_FALSE (r.edits[0].has_value());
        REQUIRE (tempoAt (r.doc, 3840).has_value());
        CHECK (*tempoAt (r.doc, 3840) == Approx (std::ceil (minBpm())).margin (0.01));
    }

    SECTION ("far above: the maximum BPM")
    {
        Rig r;
        const auto p = r.at (3840, 150);
        press (*r.graph, p);
        dragTo (*r.graph, p - juce::Point<int> (0, 10), p);
        release (*r.graph, { p.x, -5000 }, p);
        REQUIRE (r.edits.size() == 1);
        CHECK_FALSE (r.edits[0].has_value());
        REQUIRE (tempoAt (r.doc, 3840).has_value());
        CHECK (*tempoAt (r.doc, 3840) == Approx (1000.0).margin (0.01));
    }

    SECTION ("far left: tick clamps to 0, which is occupied")
    {
        Rig r;
        const auto before = tempos (r.doc);
        const auto p = r.at (1920, 90);
        press (*r.graph, p);
        dragTo (*r.graph, p - juce::Point<int> (10, 0), p);
        release (*r.graph, { -500, p.y }, p);
        REQUIRE (r.edits.size() == 1);
        CHECK (isRejected (r.edits[0], TempoEditError::TickOccupied));
        CHECK (tempos (r.doc) == before);
        CHECK_FALSE (r.doc.getUndoManager().canUndo());
    }
}

TEST_CASE ("TempoGraphComponent: the BPM range is frozen for the whole drag", "[tempo-graph][edit]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Rig r;
    const auto listing = listTempoMeterEvents (r.doc);
    const auto p = r.at (3840, 150);
    const int untouchedY = r.graph->yForBpm (90);

    press (*r.graph, p);
    dragTo (*r.graph, { p.x, r.graph->yForBpm (70) }, p);
    CHECK (r.graph->yForBpm (90) == untouchedY);

    // A refresh mid-drag that still holds the dragged point but adds a new extreme tempo.
    auto widened = listing;
    TempoMeterEvent extreme;
    extreme.tick = 5760;
    extreme.bpm  = 400.0;
    widened.push_back (extreme);
    r.graph->setEvents (widened);
    REQUIRE (r.graph->isDraggingForTesting());
    CHECK (r.graph->yForBpm (90) == untouchedY);

    const int y2 = 30;
    // The fixture tells a frozen range from a recomputed one.
    REQUIRE (r.bpmAtY (listing, y2) != r.bpmAtY (widened, y2));
    dragTo (*r.graph, { p.x, y2 }, p);
    REQUIRE (r.graph->dragPreviewForTesting().has_value());
    CHECK (r.graph->dragPreviewForTesting()->second == r.bpmAtY (listing, y2));
}

TEST_CASE ("TempoGraphComponent: clicking empty space adds a snapped tempo; on an existing tick it only selects", "[tempo-graph][edit]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Rig r;
    const auto listing = listTempoMeterEvents (r.doc);
    const juce::Point<int> p (r.graph->xForTick (960) + 2, r.graph->yForBpm (110));
    const double expectedBpm = r.bpmAtY (listing, p.y);
    REQUIRE (expectedBpm == Approx (110.0).margin (1.0));

    click (*r.graph, p);
    REQUIRE (r.edits.size() == 1);
    CHECK_FALSE (r.edits[0].has_value());
    REQUIRE (tempoAt (r.doc, 960).has_value());
    CHECK (*tempoAt (r.doc, 960) == Approx (expectedBpm).margin (0.01));
    CHECK_FALSE (tempoAt (r.doc, 980).has_value());
    REQUIRE (r.selects.size() == 1);
    CHECK (r.selects.back() == tempoSel (960));
    CHECK (r.doc.getUndoManager().canUndo());
    const auto afterAdd = tempos (r.doc);

    // Clicking the new point only selects it.
    click (*r.graph, r.at (960, *tempoAt (r.doc, 960)));
    CHECK (r.edits.size() == 1);
    CHECK (r.selects.size() == 2);
    CHECK (r.selects.back() == tempoSel (960));
    CHECK (tempos (r.doc) == afterAdd);

    // Empty space whose nearest beat is 1920 (well away from the 90 point) selects that tempo.
    const juce::Point<int> nearTick (r.graph->xForTick (1920) + 3, r.graph->yForBpm (150));
    click (*r.graph, nearTick);
    CHECK (r.edits.size() == 1);
    CHECK (r.selects.size() == 3);
    CHECK (r.selects.back() == tempoSel (1920));
    CHECK (tempos (r.doc) == afterAdd);

    r.doc.undo();
    CHECK_FALSE (tempoAt (r.doc, 960).has_value());
    CHECK_FALSE (r.doc.getUndoManager().canUndo());   // exactly one step was written
}

TEST_CASE ("TempoGraphComponent: undo mid-drag drops the drag and mouse-up writes nothing", "[tempo-graph][edit]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Rig r (false);
    importStandard (r.doc);
    REQUIRE_FALSE (setTempo (r.doc, 3840, 150));
    r.doc.getUndoManager().clearUndoHistory();
    REQUIRE_FALSE (setTempo (r.doc, 1920, 90));   // undoable
    r.sync();
    const auto p = r.at (1920, 90);
    const juce::Point<int> to (r.graph->xForTick (2400), r.graph->yForBpm (130));

    SECTION ("the listing is refreshed before mouse-up")
    {
        press (*r.graph, p);
        dragTo (*r.graph, to, p);
        REQUIRE (r.graph->isDraggingForTesting());
        r.doc.undo();
        const auto afterUndo = tempos (r.doc);
        r.sync();
        CHECK_FALSE (r.graph->isDraggingForTesting());
        CHECK_FALSE (r.graph->dragPreviewForTesting().has_value());
        release (*r.graph, to, p);
        CHECK (r.edits.empty());
        CHECK (tempos (r.doc) == afterUndo);
        CHECK (r.doc.getUndoManager().canRedo());   // no stale commit cleared the redo stack
    }

    SECTION ("mouse-up arrives before the refresh")
    {
        press (*r.graph, p);
        dragTo (*r.graph, to, p);
        REQUIRE (r.graph->isDraggingForTesting());
        r.doc.undo();
        const auto afterUndo = tempos (r.doc);
        release (*r.graph, to, p);
        CHECK (tempos (r.doc) == afterUndo);
        CHECK (r.doc.getUndoManager().canRedo());
        CHECK_FALSE (r.graph->isDraggingForTesting());
        CHECK (r.edits.empty());   // abandoned silently, as the meter strip does
    }
}

TEST_CASE ("TempoGraphComponent: undo mid-drag that changes the dragged tempo's BPM writes nothing", "[tempo-graph][edit]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Rig r;   // 120 @ 0, 90 @ 1920, 150 @ 3840
    const auto original = tempos (r.doc);

    // Commit 90 -> 100 with a vertical drag.
    {
        const auto p  = r.at (1920, 90);
        const juce::Point<int> to (p.x, r.graph->yForBpm (100));
        press (*r.graph, p);
        dragTo (*r.graph, to, p);
        release (*r.graph, to, p);
        REQUIRE (r.edits.size() == 1);
        REQUIRE_FALSE (r.edits[0].has_value());
        REQUIRE (*tempoAt (r.doc, 1920) == Approx (100.0).margin (0.01));
    }
    r.edits.clear();

    // A sideways drag of the same point (|dy| < 3 keeps the stored BPM, 100).
    const auto p = r.at (1920, 100);
    const juce::Point<int> to (r.graph->xForTick (2400), p.y + 1);

    SECTION ("the listing is refreshed before mouse-up")
    {
        press (*r.graph, p);
        dragTo (*r.graph, to, p);
        REQUIRE (r.graph->isDraggingForTesting());
        r.doc.undo();   // back to 90 @ 1920: same tick, different BPM
        REQUIRE (tempos (r.doc) == original);
        r.sync();
        release (*r.graph, to, p);
        CHECK (tempos (r.doc) == original);
        CHECK (r.doc.getUndoManager().canRedo());
        CHECK (r.edits.empty());
    }

    SECTION ("mouse-up arrives before the refresh")
    {
        press (*r.graph, p);
        dragTo (*r.graph, to, p);
        r.doc.undo();
        REQUIRE (tempos (r.doc) == original);
        release (*r.graph, to, p);
        CHECK (tempos (r.doc) == original);
        CHECK (r.doc.getUndoManager().canRedo());
        CHECK (r.edits.empty());
    }
}

TEST_CASE ("TempoGraphComponent: only the left button selects, drags or adds", "[tempo-graph][edit]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    for (const auto button : { juce::ModifierKeys::rightButtonModifier, juce::ModifierKeys::middleButtonModifier })
    {
        Rig r;
        const auto before = tempos (r.doc);
        const auto p  = r.at (1920, 90);
        const juce::Point<int> to (r.graph->xForTick (2400), r.graph->yForBpm (130));
        const auto mouse = [&] (juce::Point<int> pos, juce::Point<int> down)
        {
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos.toFloat(),
                                     juce::ModifierKeys (button), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, r.graph.get(), r.graph.get(),
                                     now, down.toFloat(), now, 1, pos != down);
        };

        r.graph->mouseDown (mouse (p, p));
        CHECK_FALSE (r.graph->isDraggingForTesting());
        r.graph->mouseDrag (mouse (to, p));
        r.graph->mouseUp (mouse (to, p));

        const juce::Point<int> empty (r.graph->xForTick (960), r.graph->yForBpm (110));
        r.graph->mouseDown (mouse (empty, empty));
        r.graph->mouseUp (mouse (empty, empty));

        CHECK (r.edits.empty());
        CHECK (r.selects.empty());
        CHECK (tempos (r.doc) == before);
        CHECK_FALSE (r.doc.getUndoManager().canUndo());
    }
}

TEST_CASE ("TempoGraphComponent: with no time base a click adds nothing", "[tempo-graph][edit]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Rig r (false);
    REQUIRE_FALSE (r.doc.hasTimeBase());
    click (*r.graph, { 300, 80 });
    REQUIRE (r.edits.size() == 1);
    CHECK (isRejected (r.edits[0], TempoEditError::NoTimeBase));
    CHECK (r.selects.empty());
    CHECK (listTempoMeterEvents (r.doc).empty());
    CHECK_FALSE (r.doc.getUndoManager().canUndo());
}

TEST_CASE ("TempoGraphComponent: points are hit within 6 px at extreme zooms; the later wins a tie", "[tempo-graph][edit]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SECTION ("0.001 px/tick: two tempos one tick apart")
    {
        Rig r;
        REQUIRE_FALSE (setTempo (r.doc, 1921, 90));
        r.doc.getUndoManager().clearUndoHistory();
        r.view.setPixelsPerTick (0.001);
        r.sync();
        REQUIRE (r.at (1920, 90) == r.at (1921, 90));
        press (*r.graph, r.at (1920, 90));
        CHECK (r.graph->isDraggingForTesting());   // a hit, not an add
        release (*r.graph, r.at (1920, 90), r.at (1920, 90));
        REQUIRE (r.selects.size() == 1);
        CHECK (r.selects[0] == tempoSel (1921));
        CHECK (r.edits.empty());
    }

    SECTION ("2.5 px/tick: 4 px off the point still hits")
    {
        Rig r;
        r.view.setPixelsPerTick (2.5);
        r.view.setScrollOffsetTicks (1800);
        const auto p = r.at (1920, 90) + juce::Point<int> (4, 4);
        press (*r.graph, p);
        CHECK (r.graph->isDraggingForTesting());
        release (*r.graph, p, p);
        REQUIRE (r.selects.size() == 1);
        CHECK (r.selects[0] == tempoSel (1920));
        CHECK (r.edits.empty());
    }
}

TEST_CASE ("TempoGraphComponent: keys are never consumed", "[tempo-graph][edit]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Rig r;
    CHECK (r.graph->getWantsKeyboardFocus());
    CHECK_FALSE (r.graph->keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));
}

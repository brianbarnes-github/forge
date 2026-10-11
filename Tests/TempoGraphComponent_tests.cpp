// TempoGraphComponent: the read-only tempo graph of the Tempo/Meter window.

#include "MidiTestBytes.h"
#include "UI/SongDocument.h"
#include "UI/SongModelBridge.h"
#include "UI/SongsmithColours.h"
#include "UI/TempoEdit.h"
#include "UI/TempoGraphComponent.h"
#include "UI/TimelineViewState.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

#include <vector>

using namespace lotro;

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

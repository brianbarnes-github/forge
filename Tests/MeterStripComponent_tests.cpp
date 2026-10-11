// MeterStripComponent: the meter strip of the Tempo/Meter window (drawing and editing).

#include "MidiTestBytes.h"
#include "UI/MeterSegments.h"
#include "UI/MeterStripComponent.h"
#include "UI/SongDocument.h"
#include "UI/SongModelBridge.h"
#include "UI/SongsmithColours.h"
#include "UI/TempoEdit.h"
#include "UI/TempoMeterGraphModel.h"
#include "UI/TempoMeterTable.h"
#include "UI/TimelineViewState.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

#include <memory>
#include <optional>
#include <tuple>
#include <utility>
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

    juce::Image render (juce::Component& c)
    {
        return c.createComponentSnapshot (c.getLocalBounds(), false, 1.0f);
    }

    bool sameRgb (juce::Colour a, juce::Colour b)
    {
        return a.getRed() == b.getRed() && a.getGreen() == b.getGreen() && a.getBlue() == b.getBlue();
    }

    bool sameRgb (juce::Colour a, juce::uint32 argb) { return sameRgb (a, juce::Colour (argb)); }

    juce::MouseEvent makeMouse (juce::Component& c, juce::Point<int> pos, juce::Point<int> mouseDownPos,
                                int clicks = 1, juce::ModifierKeys::Flags button = juce::ModifierKeys::leftButtonModifier)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos.toFloat(),
                                 juce::ModifierKeys (button), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now,
                                 mouseDownPos.toFloat(), now, clicks, pos != mouseDownPos);
    }

    void press   (juce::Component& c, juce::Point<int> p)                         { c.mouseDown (makeMouse (c, p, p)); }
    void dragTo  (juce::Component& c, juce::Point<int> p, juce::Point<int> down)  { c.mouseDrag (makeMouse (c, p, down)); }
    void release (juce::Component& c, juce::Point<int> p, juce::Point<int> down)  { c.mouseUp (makeMouse (c, p, down)); }

    // What JUCE delivers for a double-click: down, up, down (2 clicks), up, then
    // mouseDoubleClick from inside the second mouse-up.
    void doubleClick (juce::Component& c, juce::Point<int> p,
                      juce::ModifierKeys::Flags button = juce::ModifierKeys::leftButtonModifier)
    {
        c.mouseDown (makeMouse (c, p, p, 1, button));
        c.mouseUp (makeMouse (c, p, p, 1, button));
        c.mouseDown (makeMouse (c, p, p, 2, button));
        c.mouseUp (makeMouse (c, p, p, 2, button));
        c.mouseDoubleClick (makeMouse (c, p, p, 2, button));
    }

    using Meter = std::tuple<int, int, int>;   // tick, numerator, denominator

    std::vector<Meter> meters (const SongDocument& doc)
    {
        std::vector<Meter> out;
        for (const auto& e : listTempoMeterEvents (doc))
            if (e.kind == TempoMeterKind::meter)
                out.emplace_back (e.tick, e.numerator, e.denominator);
        return out;
    }

    bool isRejected (const TableEditResult& r, TempoEditError cause)
    {
        return r && r->kind == TableEditError::Kind::Rejected && r->cause == cause;
    }

    std::pair<TempoMeterKind, int> meterSel (int tick) { return { TempoMeterKind::meter, tick }; }
    using Selects = std::vector<std::pair<TempoMeterKind, int>>;

    // 4/4 @ 0 and 3/4 @ 3840 (bar 3); a 600 x 28 strip at 0.05 px/tick. onEdited also
    // refreshes the strip's listing (as the window does once the document has changed).
    struct Rig
    {
        SongDocument doc;
        TimelineViewState view;
        std::unique_ptr<MeterStripComponent> strip;
        std::vector<TableEditResult> edits;
        Selects selects;

        explicit Rig (bool withTwoMeters = true)
        {
            if (withTwoMeters)
            {
                importStandard (doc);
                REQUIRE_FALSE (setMeter (doc, 3840, 3, 4));
                doc.getUndoManager().clearUndoHistory();
            }
            view.setPixelsPerTick (0.05);
            strip = std::make_unique<MeterStripComponent> (doc, view);
            strip->setSize (600, 28);
            sync();
            strip->onEdited = [this] (const TableEditResult& r) { edits.push_back (r); sync(); };
            strip->onSelect = [this] (TempoMeterKind k, int tick) { selects.emplace_back (k, tick); };
        }

        void sync() { strip->setEvents (listTempoMeterEvents (doc)); }
        juce::Point<int> at (int tick, int dx = 0) const { return { strip->xForTick (tick) + dx, 14 }; }
    };
}

TEST_CASE ("MeterStripComponent: x mirrors the timeline view", "[meter-strip]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Rig r;
    CHECK (r.strip->xForTick (3840) == r.view.xForTick (3840));
    r.view.setScrollOffsetTicks (960);
    CHECK (r.strip->xForTick (3840) == r.view.xForTick (3840));
    CHECK (MeterStripComponent::preferredHeight == 28);
}

TEST_CASE ("MeterStripComponent: painting never crashes; blocks, edges and labels are drawn", "[meter-strip]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Rig r;
    const auto all = listTempoMeterEvents (r.doc);

    SECTION ("event counts and extreme zooms")
    {
        std::vector<TempoMeterEvent> oneMeter;
        for (const auto& e : all)
            if (! (e.kind == TempoMeterKind::meter && e.tick == 3840))
                oneMeter.push_back (e);
        for (const double ppt : { 0.0005, 0.05, 4.0 })
        {
            r.view.setPixelsPerTick (ppt);
            for (const auto& listing : { std::vector<TempoMeterEvent> {}, oneMeter, all })
            {
                r.strip->setEvents (listing);
                r.strip->setSelected (listing);
                CHECK (render (*r.strip).isValid());
            }
        }
        r.strip->setBounds (0, 0, 0, 0);
        CHECK_NOTHROW (r.strip->setEvents (all));
    }

    SECTION ("the 3/4 block: left edge line, tinted fill, a label over the fill")
    {
        const auto img = render (*r.strip);
        const int x = r.strip->xForTick (3840);
        REQUIRE (x == 192);
        CHECK (sameRgb (img.getPixelAt (x, 14), SongsmithColours::conductorMeter));
        CHECK (sameRgb (img.getPixelAt (x + 1, 14), SongsmithColours::conductorMeter));
        const auto fill3 = img.getPixelAt (500, 3);   // inside the 3/4 block, away from its label
        const auto fill4 = img.getPixelAt (150, 3);   // inside the 4/4 block, away from its label
        CHECK_FALSE (sameRgb (fill3, SongsmithColours::background));
        CHECK_FALSE (sameRgb (fill4, SongsmithColours::background));
        CHECK_FALSE (sameRgb (fill3, fill4));          // alternating shade

        // Somewhere in the label area of each block a pixel differs from that block's own fill.
        const auto labelInk = [&] (int left, juce::Colour fill)
        {
            for (int px = left + 3; px < left + 30; ++px)
                for (int py = 6; py < 22; ++py)
                    if (! sameRgb (img.getPixelAt (px, py), fill))
                        return true;
            return false;
        };
        CHECK (labelInk (x, fill3));
        CHECK (labelInk (r.strip->xForTick (0), fill4));
        // ... and far from any label there is none.
        CHECK_FALSE (labelInk (440, fill3));
    }

    SECTION ("a selected block is drawn differently")
    {
        const auto before   = render (*r.strip).getPixelAt (500, 3);
        const auto other    = render (*r.strip).getPixelAt (150, 3);
        TempoMeterEvent threeFour;
        for (const auto& e : all)
            if (e.kind == TempoMeterKind::meter && e.tick == 3840)
                threeFour = e;
        r.strip->setSelected ({ threeFour });
        const auto after = render (*r.strip);
        CHECK_FALSE (sameRgb (after.getPixelAt (500, 3), before));
        CHECK (sameRgb (after.getPixelAt (150, 3), other));   // the 4/4 block is not selected
    }

    SECTION ("no time base: nothing but background")
    {
        Rig empty (false);
        const auto img = render (*empty.strip);
        CHECK (sameRgb (img.getPixelAt (0, 14), SongsmithColours::background));
        CHECK (sameRgb (img.getPixelAt (300, 14), SongsmithColours::background));
    }
}

TEST_CASE ("MeterStripComponent: setEvents and setSelected repaint only on change; selection is meters only", "[meter-strip]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Rig r;
    const auto events = listTempoMeterEvents (r.doc);
    const int a = r.strip->repaintRequestsForTesting();
    r.strip->setEvents (events);
    CHECK (r.strip->repaintRequestsForTesting() == a);

    // tempo @ 0 shares tick 0 with the 4/4: selecting it must not highlight the meter.
    TempoMeterEvent tempo0, meter0;
    for (const auto& e : events)
        if (e.tick == 0)
            (e.kind == TempoMeterKind::tempo ? tempo0 : meter0) = e;
    REQUIRE (tempo0.kind == TempoMeterKind::tempo);
    REQUIRE (meter0.kind == TempoMeterKind::meter);
    r.strip->setSelected ({ tempo0 });
    CHECK (r.strip->selectedForTesting().empty());
    CHECK (r.strip->repaintRequestsForTesting() == a);
    r.strip->setSelected ({ tempo0, meter0 });
    REQUIRE (r.strip->selectedForTesting().size() == 1);
    CHECK (r.strip->selectedForTesting()[0].kind == TempoMeterKind::meter);
    CHECK (r.strip->repaintRequestsForTesting() == a + 1);
    r.strip->setSelected ({ meter0 });
    CHECK (r.strip->repaintRequestsForTesting() == a + 1);
}

TEST_CASE ("MeterStripComponent: dragging a block previews, then mouse-up moves it to the bar in one undo step", "[meter-strip][edit]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Rig r;
    const auto before = meters (r.doc);
    // Bars without the 3/4 are every 1920 ticks: tick 7710 snaps to bar 5 (7680), and
    // with the 3/4 kept it would snap to 8160 - so the fixture tells the two apart.
    REQUIRE (snapMeterTick (r.doc.getMeterChanges(), 480, -1, 7710) != 7680);
    const auto to = r.at (7680 + 30);

    const auto grab = GENERATE (0, 30, -4);   // on the edge, inside the 40 px grab zone, just left of the edge
    const auto p = r.at (3840, grab);
    const int  ghostX = r.strip->xForTick (7680);
    const auto ghostBefore = render (*r.strip).getPixelAt (ghostX, 2);

    press (*r.strip, p);
    CHECK (r.strip->isDraggingForTesting());
    dragTo (*r.strip, to, p);
    REQUIRE (r.strip->dragPreviewForTesting().has_value());
    CHECK (*r.strip->dragPreviewForTesting() == 7680);
    CHECK (meters (r.doc) == before);
    CHECK_FALSE (r.doc.getUndoManager().canUndo());
    // The ghost line is drawn at the previewed bar.
    CHECK_FALSE (sameRgb (render (*r.strip).getPixelAt (ghostX, 2), ghostBefore));

    release (*r.strip, to, p);
    CHECK_FALSE (r.strip->isDraggingForTesting());
    REQUIRE (r.edits.size() == 1);
    CHECK_FALSE (r.edits[0].has_value());
    CHECK (meters (r.doc) == std::vector<Meter> { { 0, 4, 4 }, { 7680, 3, 4 } });
    CHECK (r.selects == Selects { meterSel (3840), meterSel (7680) });   // the moved block stays selected

    r.doc.undo();
    CHECK (meters (r.doc) == before);
    CHECK_FALSE (r.doc.getUndoManager().canUndo());
}

TEST_CASE ("MeterStripComponent: a drop onto another meter's tick is rejected; the tick-0 meter only selects", "[meter-strip][edit]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SECTION ("onto the tick-0 meter")
    {
        Rig r;
        const auto before = meters (r.doc);
        const auto p = r.at (3840);
        const auto to = r.at (0, 2);
        press (*r.strip, p);
        dragTo (*r.strip, to, p);
        REQUIRE (r.strip->dragPreviewForTesting() == std::optional<int> (0));
        release (*r.strip, to, p);
        REQUIRE (r.edits.size() == 1);
        CHECK (isRejected (r.edits[0], TempoEditError::TickOccupied));
        CHECK (meters (r.doc) == before);
        CHECK_FALSE (r.doc.getUndoManager().canUndo());
        CHECK (r.selects == Selects { meterSel (3840) });   // not the occupied tick
    }

    SECTION ("onto a later meter")
    {
        Rig r;
        REQUIRE_FALSE (setMeter (r.doc, 8160, 2, 4));   // bar 4 of the 3/4
        r.doc.getUndoManager().clearUndoHistory();
        r.sync();
        const auto before = meters (r.doc);
        REQUIRE (before == std::vector<Meter> { { 0, 4, 4 }, { 3840, 3, 4 }, { 8160, 2, 4 } });
        const auto p = r.at (3840);
        const auto to = r.at (8160, 3);
        press (*r.strip, p);
        dragTo (*r.strip, to, p);
        release (*r.strip, to, p);
        REQUIRE (r.edits.size() == 1);
        CHECK (isRejected (r.edits[0], TempoEditError::TickOccupied));
        CHECK (meters (r.doc) == before);
        CHECK_FALSE (r.doc.getUndoManager().canUndo());
    }

    SECTION ("dragging the tick-0 meter")
    {
        Rig r;
        const auto before = meters (r.doc);
        const auto p = r.at (0, 1);
        const auto to = r.at (1920 * 2);
        press (*r.strip, p);
        CHECK_FALSE (r.strip->isDraggingForTesting());
        dragTo (*r.strip, to, p);
        CHECK_FALSE (r.strip->dragPreviewForTesting().has_value());
        release (*r.strip, to, p);
        CHECK (r.selects == Selects { meterSel (0) });
        CHECK (r.edits.empty());
        CHECK (meters (r.doc) == before);
        CHECK_FALSE (r.doc.getUndoManager().canUndo());
    }
}

TEST_CASE ("MeterStripComponent: a press and release without moving selects and writes nothing", "[meter-strip][edit]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SECTION ("on a bar line")
    {
        Rig r;
        const auto p = r.at (3840);
        press (*r.strip, p);
        release (*r.strip, p, p);
        CHECK (r.selects == Selects { meterSel (3840) });
        CHECK (r.edits.empty());
        CHECK_FALSE (r.doc.getUndoManager().canUndo());
    }

    SECTION ("off the bar lines of the other meters: nothing is quantised")
    {
        Rig r (false);
        importStandard (r.doc);
        REQUIRE_FALSE (setMeter (r.doc, 3840, 3, 4));
        REQUIRE_FALSE (setMeter (r.doc, 5280, 6, 8));   // bar 4 of the 3/4
        REQUIRE_FALSE (removeMeter (r.doc, 3840));      // 6/8 now sits mid-bar of the 4/4
        r.doc.getUndoManager().clearUndoHistory();
        r.sync();
        const auto before = meters (r.doc);
        REQUIRE (before == std::vector<Meter> { { 0, 4, 4 }, { 5280, 6, 8 } });
        // The fixture can show the bug: re-snapping 5280 would move it.
        REQUIRE (snapMeterTick (r.doc.getMeterChanges(), 480, 5280, 5280) != 5280);
        const auto p = r.at (5280);

        SECTION ("press / release")
        {
            press (*r.strip, p);
            release (*r.strip, p, p);
        }
        SECTION ("a vertical drag")
        {
            press (*r.strip, p);
            dragTo (*r.strip, { p.x, 40 }, p);
            REQUIRE (r.strip->dragPreviewForTesting() == std::optional<int> (5280));
            release (*r.strip, { p.x, 40 }, p);
        }
        SECTION ("out and back")
        {
            press (*r.strip, p);
            dragTo (*r.strip, r.at (7680), p);
            REQUIRE (r.strip->dragPreviewForTesting() != std::optional<int> (5280));
            dragTo (*r.strip, p, p);
            release (*r.strip, p, p);
        }

        CHECK (r.selects == Selects { meterSel (5280) });
        for (const auto& e : r.edits)
            CHECK_FALSE (e.has_value());
        CHECK (meters (r.doc) == before);
        CHECK_FALSE (r.doc.getUndoManager().canUndo());
    }
}

TEST_CASE ("MeterStripComponent: a click on a block body selects it and adds nothing", "[meter-strip][edit]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Rig r;
    const auto before = meters (r.doc);
    const auto p = r.at (3840, 100);   // past the 40 px grab zone
    press (*r.strip, p);
    CHECK_FALSE (r.strip->isDraggingForTesting());
    dragTo (*r.strip, r.at (7680), p);
    release (*r.strip, r.at (7680), p);
    CHECK (r.selects == Selects { meterSel (3840) });
    CHECK (r.edits.empty());
    CHECK (meters (r.doc) == before);
}

TEST_CASE ("MeterStripComponent: double-click adds the meter in force at the nearest bar", "[meter-strip][edit]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SECTION ("in the 4/4 block at bar 2")
    {
        Rig r;
        const auto before = meters (r.doc);
        doubleClick (*r.strip, r.at (1920, 3));
        REQUIRE (r.edits.size() == 1);
        CHECK_FALSE (r.edits[0].has_value());
        CHECK (meters (r.doc) == std::vector<Meter> { { 0, 4, 4 }, { 1920, 4, 4 }, { 3840, 3, 4 } });
        REQUIRE_FALSE (r.selects.empty());
        CHECK (r.selects.back() == meterSel (1920));
        r.doc.undo();
        CHECK (meters (r.doc) == before);
        CHECK_FALSE (r.doc.getUndoManager().canUndo());   // exactly one step
    }

    SECTION ("in the 3/4 block it carries 3/4, not the first meter")
    {
        Rig r;
        doubleClick (*r.strip, r.at (6720, 3));   // bar 3 of the 3/4 (3840 + 2 * 1440)
        REQUIRE (r.edits.size() == 1);
        CHECK_FALSE (r.edits[0].has_value());
        CHECK (meters (r.doc) == std::vector<Meter> { { 0, 4, 4 }, { 3840, 3, 4 }, { 6720, 3, 4 } });
        CHECK (r.selects.back() == meterSel (6720));
    }

    SECTION ("snapping onto an existing meter only selects it")
    {
        Rig r;
        const auto before = meters (r.doc);
        const auto p = r.at (3640);   // in the 4/4 block, beyond the 3/4's edge zone
        REQUIRE (snapMeterTick (r.doc.getMeterChanges(), 480, -1, r.view.tickForX (p.x)) == 3840);
        doubleClick (*r.strip, p);
        CHECK (r.edits.empty());
        CHECK (meters (r.doc) == before);
        CHECK_FALSE (r.doc.getUndoManager().canUndo());
        REQUIRE_FALSE (r.selects.empty());
        CHECK (r.selects.back() == meterSel (3840));
    }

    SECTION ("no time base")
    {
        Rig r (false);
        REQUIRE_FALSE (r.doc.hasTimeBase());
        doubleClick (*r.strip, { 300, 14 });
        REQUIRE (r.edits.size() == 1);
        CHECK (isRejected (r.edits[0], TempoEditError::NoTimeBase));
        CHECK (r.selects.empty());
        CHECK_FALSE (r.doc.getUndoManager().canUndo());
    }
}

TEST_CASE ("MeterStripComponent: only the left button selects, drags or adds", "[meter-strip][edit]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    for (const auto button : { juce::ModifierKeys::rightButtonModifier, juce::ModifierKeys::middleButtonModifier })
    {
        Rig r;
        const auto before = meters (r.doc);
        const auto p = r.at (3840);
        const auto to = r.at (7680);
        r.strip->mouseDown (makeMouse (*r.strip, p, p, 1, button));
        CHECK_FALSE (r.strip->isDraggingForTesting());
        r.strip->mouseDrag (makeMouse (*r.strip, to, p, 1, button));
        r.strip->mouseUp (makeMouse (*r.strip, to, p, 1, button));
        doubleClick (*r.strip, r.at (1920, 3), button);
        CHECK (r.edits.empty());
        CHECK (r.selects.empty());
        CHECK (meters (r.doc) == before);
        CHECK_FALSE (r.doc.getUndoManager().canUndo());
    }
}

TEST_CASE ("MeterStripComponent: undo mid-drag drops the drag and mouse-up writes nothing", "[meter-strip][edit]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Rig r (false);
    importStandard (r.doc);
    REQUIRE_FALSE (setMeter (r.doc, 3840, 3, 4));   // undoable
    r.sync();
    const auto p = r.at (3840);
    const auto to = r.at (7680);

    SECTION ("the listing is refreshed before mouse-up")
    {
        press (*r.strip, p);
        dragTo (*r.strip, to, p);
        REQUIRE (r.strip->isDraggingForTesting());
        r.doc.undo();
        const auto afterUndo = meters (r.doc);
        r.sync();
        CHECK_FALSE (r.strip->isDraggingForTesting());
        CHECK_FALSE (r.strip->dragPreviewForTesting().has_value());
        release (*r.strip, to, p);
        CHECK (r.edits.empty());
        CHECK (meters (r.doc) == afterUndo);
        CHECK (r.doc.getUndoManager().canRedo());   // no stale commit cleared the redo stack
    }

    SECTION ("mouse-up arrives before the refresh")
    {
        press (*r.strip, p);
        dragTo (*r.strip, to, p);
        r.doc.undo();
        const auto afterUndo = meters (r.doc);
        release (*r.strip, to, p);
        CHECK (r.edits.empty());
        CHECK (meters (r.doc) == afterUndo);
        CHECK (r.doc.getUndoManager().canRedo());
        CHECK_FALSE (r.strip->isDraggingForTesting());
    }
}

TEST_CASE ("MeterStripComponent: keys are never consumed", "[meter-strip][edit]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Rig r;
    CHECK (r.strip->getWantsKeyboardFocus());
    CHECK_FALSE (r.strip->keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));
}

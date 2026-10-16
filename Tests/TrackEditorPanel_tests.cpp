#include "UI/SongsmithColours.h"
#include "PlaybackTestSupport.h"
#include "UI/Playback/PlaybackController.h"
#include "UI/SongDocument.h"
#include "UI/EditorMenuModel.h"
#include "UI/Playback/PreviewRequest.h"
#include "UI/TrackEditorPanel.h"
#include "UI/Theme/ThemePaint.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace lotro
{
    struct TrackEditorPanelTestAccess
    {
        static juce::Rectangle<int> stripBounds (TrackEditorPanel& w) { return w.content->strip.getBounds(); }
        static juce::Rectangle<int> rulerBounds (TrackEditorPanel& w) { return w.content->ruler.getBounds(); }
        static juce::Rectangle<int> rollBounds (TrackEditorPanel& w) { return w.roll.getBounds(); }
        static EditorBottomBar& bottomBar (TrackEditorPanel& w) { return w.bottomBar; }
        static TimelineRuler& ruler (TrackEditorPanel& w) { return w.content->ruler; }
        static PianoRollComponent& roll (TrackEditorPanel& w) { return w.roll; }
        static juce::Component& content (TrackEditorPanel& w) { return *w.content; }
        static juce::MenuBarComponent& menuBar (TrackEditorPanel& w) { return w.menuBar; }
        static int menuActivations (TrackEditorPanel& w) { return w.menuModel.activationsForTesting(); }
        static juce::Component& rollCanvas (TrackEditorPanel& w) { return w.roll.canvasForTesting(); }
        // A plain left press on the roll's canvas in the middle of `pitch`'s row, past the notes.
        static void clickRow (TrackEditorPanel& w, int pitch)
        {
            const auto& geometry = w.roll.geometry;
            const juce::Point<int> pos { geometry.xForTick (48000), geometry.yForPitch (pitch) + geometry.getRowHeight() / 2 };
            w.roll.handleEditorMouseDown (pos, {}, false);
            w.roll.handleEditorMouseUp (pos);
        }
        // A double-click on the roll's canvas in the middle of `pitch`'s row, past the notes.
        static void doubleClickRow (TrackEditorPanel& w, int pitch)
        {
            const auto& geometry = w.roll.geometry;
            const juce::Point<int> pos { geometry.xForTick (48000), geometry.yForPitch (pitch) + geometry.getRowHeight() / 2 };
            w.roll.handleEditorMouseDown (pos, {}, true);
            w.roll.handleEditorMouseUp (pos);
        }
        static bool isSelected (TrackEditorPanel& w, const juce::ValueTree& note) { return w.roll.sourceEditor->isSelected (note); }
        static int numSelected (TrackEditorPanel& w) { return w.roll.sourceEditor->getNumSelected(); }
        static void addToSelection (TrackEditorPanel& w, const juce::ValueTree& note) { w.roll.sourceEditor->addToSelection (note); }
        // A body drag of the note at (`pitch`, `tick`): press inside it, then one
        // drag step per row up to `rows` rows up, calling `afterStep` after each,
        // then release.
        static void dragNoteRows (TrackEditorPanel& w, int pitch, int tick, int rows, const std::function<void()>& afterStep)
        {
            const auto& geometry = w.roll.geometry;
            const auto at = [&] (int p) { return juce::Point<int> { geometry.xForTick (tick), geometry.yForPitch (p) + geometry.getRowHeight() / 2 }; };
            w.roll.handleEditorMouseDown (at (pitch), {}, false);
            for (int r = 1; r <= rows; ++r)
            {
                w.roll.handleEditorMouseDrag (at (pitch + r));
                afterStep();
            }
            w.roll.handleEditorMouseUp (at (pitch + rows));
        }
    };
}

TEST_CASE ("TrackEditorPanel: setTrack re-points the same window to a different track without recreating it", "[track-editor-window]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto trackA = doc.addTrack ("Track A", 0xFFAABBCCu, 0, 0);
    auto trackB = doc.addTrack ("Track B", 0xFFDDEEFFu, 1, 0);
    const auto idA = (juce::int64) trackA.getProperty (SongIDs::trackId);
    const auto idB = (juce::int64) trackB.getProperty (SongIDs::trackId);

    TrackEditorPanel window (doc);
    window.setTrack (trackA);
    CHECK (window.getTrackId() == idA);

    window.setTrack (trackB);
    CHECK (window.getTrackId() == idB);
}

TEST_CASE ("TrackEditorPanel: a track appearance change retitles the window for its own track and repaints the roll for any", "[track-editor-window]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto trackA = doc.addTrack ("Track A", (int) 0xFFAABBCCu, 0, 0);
    auto trackB = doc.addTrack ("Track B", (int) 0xFFDDEEFFu, 1, 0);
    const auto idA = (juce::int64) trackA.getProperty (SongIDs::trackId);
    const auto idB = (juce::int64) trackB.getProperty (SongIDs::trackId);

    TrackEditorPanel window (doc);
    window.setTrack (trackA);
    REQUIRE (window.title() == "Edit Track: Track A");

    trackA.setProperty (SongIDs::name, "Harp", nullptr);
    trackB.setProperty (SongIDs::name, "Flute", nullptr);
    juce::StringArray retitles;
    window.onTitleChanged = [&] (const juce::String& t) { retitles.add (t); };
    const int repaints = window.appearanceRepaintsForTesting();
    window.trackAppearanceChanged (idB);                       // a ghost's colour, say
    CHECK (retitles.isEmpty());                                // not this panel's track: no retitle
    CHECK (window.appearanceRepaintsForTesting() == repaints + 1);
    window.trackAppearanceChanged (idA);
    CHECK (retitles == juce::StringArray { "Edit Track: Harp" });
    CHECK (window.title() == "Edit Track: Harp");
    CHECK (window.appearanceRepaintsForTesting() == repaints + 2);
}

TEST_CASE ("TrackEditorPanel: with playback set, the window hosts the transport strip and ruler above the roll", "[track-editor][playhead]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    TrackEditorPanel window (doc);
    CHECK (! window.hasTransportStripForTesting());
    CHECK (! window.hasRulerForTesting());

    window.setPlayback (&controller);
    CHECK (window.hasTransportStripForTesting());
    CHECK (window.hasRulerForTesting());

    using Access = TrackEditorPanelTestAccess;
    window.setSize (700, 400);
    const auto strip = Access::stripBounds (window);
    const auto ruler = Access::rulerBounds (window);
    const auto roll = Access::rollBounds (window);
    CHECK (strip.getY() == 0);
    CHECK (strip.getHeight() == TransportStrip::height);
    CHECK (ruler.getY() == strip.getBottom());
    CHECK (ruler.getHeight() == TimelineRuler::height);
    CHECK (roll.getY() == ruler.getBottom());
    CHECK (roll.getBottom() == Access::content (window).getHeight());
    // The ruler spans the roll's width, so a ruler x is a roll x.
    CHECK (ruler.getX() == roll.getX());
    CHECK (ruler.getWidth() == roll.getWidth());
}

TEST_CASE ("TrackEditorPanel: a bottom bar strip-high is pinned under the roll, with or without playback", "[track-editor][bottom-bar]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    TrackEditorPanel window (doc);
    using Access = TrackEditorPanelTestAccess;

    window.setSize (700, 400);
    {
        auto& bar = Access::bottomBar (window);
        CHECK (bar.getParentComponent() == &window);
        CHECK (bar.isVisible());
        CHECK (bar.getHeight() == TransportStrip::height);
        CHECK (bar.getBottom() == window.getHeight());
        CHECK (bar.getWidth() == window.getWidth());
        CHECK (Access::rollBounds (window).getBottom() == bar.getY());   // no playback: the roll sits on the bar
    }

    window.setPlayback (&controller);
    {
        auto& bar = Access::bottomBar (window);
        CHECK (bar.isVisible());
        CHECK (bar.getHeight() == TransportStrip::height);
        CHECK (bar.getBottom() == window.getHeight());
        CHECK (Access::content (window).getBottom() == bar.getY());
        CHECK (Access::rollBounds (window).getBottom() == Access::content (window).getHeight());
    }
}

TEST_CASE ("TrackEditorPanel: the bottom bar paints the panel gradient, like the transport strip", "[track-editor][bottom-bar][theme]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    TrackEditorPanel window (doc);
    window.setSize (700, 400);

    auto& bar = TrackEditorPanelTestAccess::bottomBar (window);
    juce::Image image (juce::Image::ARGB, bar.getWidth(), bar.getHeight(), true, juce::SoftwareImageType());
    { juce::Graphics g (image); bar.paintEntireComponent (g, false); }
    juce::Image reference (juce::Image::ARGB, bar.getWidth(), bar.getHeight(), true, juce::SoftwareImageType());
    { juce::Graphics rg (reference); ThemePaint::paintPanelGradient (rg, reference.getBounds()); }

    for (int y = 0; y < bar.getHeight(); ++y)
        CHECK (image.getPixelAt (bar.getWidth() / 2, y) == reference.getPixelAt (bar.getWidth() / 2, y));
}

TEST_CASE ("TrackEditorPanel: a fresh panel is empty, titled MIDI Editor, and clearTrack goes back to that", "[track-editor-window]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCC, 0, 0);

    TrackEditorPanel panel (doc);
    CHECK (panel.getTrackId() == -1);
    CHECK (panel.title() == "MIDI Editor");

    juce::StringArray titles;
    panel.onTitleChanged = [&] (const juce::String& t) { titles.add (t); };
    panel.setTrack (track);
    CHECK (panel.getTrackId() != -1);
    CHECK (panel.title() == "Edit Track: Track A");

    panel.clearTrack();
    CHECK (panel.getTrackId() == -1);
    CHECK (panel.title() == "MIDI Editor");
    CHECK (titles == juce::StringArray { "Edit Track: Track A", "MIDI Editor" });
}

TEST_CASE ("TrackEditorPanel: setPlayback fills the panel with strip, ruler and roll", "[track-editor][playhead]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    TrackEditorPanel panel (doc);
    panel.setSize (700, 400);
    panel.setPlayback (&controller);

    using Access = TrackEditorPanelTestAccess;
    // Everything under the 24 px menu bar.
    CHECK (Access::content (panel).getBounds() == juce::Rectangle<int> (0, TrackEditorPanel::menuBarHeight, 700, 400 - TrackEditorPanel::menuBarHeight - EditorBottomBar::height));
    CHECK (Access::stripBounds (panel).getBottom() == Access::rulerBounds (panel).getY());
    CHECK (Access::rollBounds (panel).getBottom() == Access::content (panel).getHeight());
}

TEST_CASE ("TrackEditorPanel: Space toggles playback and other keys are left alone", "[track-editor][playhead]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCC, 0, 0);
    lotro::playbacktest::addNote (track, 60, 0, 960);   // play() refuses an empty song
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    TrackEditorPanel window (doc);

    // Without playback the window does not claim Space.
    CHECK (! window.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)));

    window.setPlayback (&controller);
    CHECK (window.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)));
    CHECK (controller.isPlaying());
    CHECK (window.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)));
    CHECK (! controller.isPlaying());
    CHECK (! window.keyPressed (juce::KeyPress ('x')));
}

TEST_CASE ("TrackEditorPanel: right-click in the ruler seeks and left-click sets the marker, at the tick under that x of the roll", "[track-editor][playhead]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCC, 0, 0);
    lotro::playbacktest::addNote (track, 60, 0, 9600);
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    controller.flushRebuild();
    TrackEditorPanel window (doc);
    window.setPlayback (&controller);
    window.setTrack (track);

    using Access = TrackEditorPanelTestAccess;
    window.setSize (800, 400);
    auto& ruler = Access::ruler (window);
    const int x = Access::roll (window).xForTickInComponent (4800.0);

    auto* source = juce::Desktop::getInstance().getMouseSource (0);
    const auto now = juce::Time::getCurrentTime();
    const juce::Point<float> pos ((float) x, 3.0f);
    // Right-click moves the playhead...
    ruler.mouseDown (juce::MouseEvent (*source, pos, juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier),
                                       0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &ruler, &ruler, now, pos, now, 1, false));
    CHECK (controller.getPositionTicks() == Catch::Approx (4800.0).margin (30.0));
    CHECK (! controller.getMarkerTick().has_value());

    // ...a left click sets the marker and leaves the playhead where it was.
    const juce::Point<float> pos2 ((float) Access::roll (window).xForTickInComponent (2400.0), 20.0f);
    ruler.mouseDown (juce::MouseEvent (*source, pos2, juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                       &ruler, &ruler, now, pos2, now, 1, false));
    REQUIRE (controller.getMarkerTick().has_value());
    CHECK (*controller.getMarkerTick() == Catch::Approx (2400.0).margin (30.0));
    CHECK (controller.getPositionTicks() == Catch::Approx (4800.0).margin (30.0));
}

TEST_CASE ("TrackEditorPanel: the timing bar draws bar lines from the keyboard gutter's edge, not under the gutter", "[track-editor][ruler]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    TrackEditorPanel window (doc);
    window.setPlayback (&controller);

    using Access = TrackEditorPanelTestAccess;
    window.setSize (700, 400);
    REQUIRE (Access::roll (window).onViewChanged != nullptr);   // the roll tells the ruler to repaint

    auto& ruler = Access::ruler (window);
    juce::Image image (juce::Image::ARGB, ruler.getWidth(), ruler.getHeight(), true, juce::SoftwareImageType());
    {
        juce::Graphics g (image);
        ruler.paintEntireComponent (g, false);
    }

    // The ruler's background is the panel gradient; compare against it rendered alone.
    const auto background = [&]
    {
        juce::Image reference (juce::Image::ARGB, image.getWidth(), image.getHeight(), true, juce::SoftwareImageType());
        { juce::Graphics rg (reference); ThemePaint::paintPanelGradient (rg, reference.getBounds()); }
        return reference.getPixelAt (0, TimelineRuler::height - 4);
    }();
    const int gutter = Access::roll (window).getGutterWidth();
    const int lowerRow = TimelineRuler::height - 4;

    CHECK (image.getPixelAt (gutter, lowerRow) != background);   // bar 1's line (tick 0, unscrolled)
    for (int x = 0; x < gutter; ++x)
        CHECK (image.getPixelAt (x, lowerRow) == background);
}

namespace
{
    juce::MouseEvent rulerEvent (juce::Component& target, juce::Point<int> downPos, juce::Point<int> pos)
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos.toFloat(), juce::ModifierKeys(),
                                 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target,
                                 juce::Time::getCurrentTime(), downPos.toFloat(),
                                 juce::Time::getCurrentTime(), 1, pos != downPos);
    }

    std::optional<std::pair<double, double>> loopOf (const PlaybackController& c)
    {
        const auto r = c.getLoopRange();
        if (! r.has_value())
            return std::nullopt;
        return std::make_pair (r->startTick, r->endTick);
    }

    using TickPair = std::pair<double, double>;
}

TEST_CASE ("TrackEditorPanel: dragging a ruler bracket moves that loop edge, snapped to the roll's grid and anchored on the other edge", "[track-editor][loop][ruler]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCC, 0, 0);
    lotro::playbacktest::addNote (track, 60, 0, 9600);
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    controller.flushRebuild();
    TrackEditorPanel window (doc);
    window.setPlayback (&controller);
    window.setSize (800, 400);
    window.setTrack (track);
    window.setGridLength (GridLength::Eighth);   // the editor's own grid (240 ticks at 480 PPQ)

    using Access = TrackEditorPanelTestAccess;
    auto& ruler = Access::ruler (window);
    auto& roll = Access::roll (window);
    // Every target below is 20 ticks past a multiple of 240 (a fitted pixel is about 13 ticks),
    // so only a snapped edge lands on the multiple.
    const auto at = [&roll] (double tick) { return juce::Point<int> (roll.xForTickInComponent (tick), 5); };

    REQUIRE (controller.setLoopRange (1200.0, 2400.0));
    const auto endBracket = ruler.loopHandleBounds (false);
    REQUIRE (endBracket.has_value());

    // The end edge: 2900 snaps to 2880.
    const juce::Point<int> endDown { endBracket->getRight() - 3, endBracket->getCentreY() };
    ruler.mouseDown (rulerEvent (ruler, endDown, endDown));
    ruler.mouseDrag (rulerEvent (ruler, endDown, at (2900.0)));
    ruler.mouseUp (rulerEvent (ruler, endDown, at (2900.0)));
    CHECK (loopOf (controller) == std::optional<TickPair> (TickPair { 1200.0, 2880.0 }));

    // The start edge: to 1700 (1680), past the end to 3620 (3600), then back to 3140 (3120).
    // Every step is anchored on the end as it was at the press (2880).
    const auto startBracket = ruler.loopHandleBounds (true);
    REQUIRE (startBracket.has_value());
    const juce::Point<int> startDown { startBracket->getX() + 2, startBracket->getCentreY() };
    ruler.mouseDown (rulerEvent (ruler, startDown, startDown));
    ruler.mouseDrag (rulerEvent (ruler, startDown, at (1700.0)));
    CHECK (loopOf (controller) == std::optional<TickPair> (TickPair { 1680.0, 2880.0 }));
    ruler.mouseDrag (rulerEvent (ruler, startDown, at (3620.0)));
    CHECK (loopOf (controller) == std::optional<TickPair> (TickPair { 2880.0, 3600.0 }));
    ruler.mouseDrag (rulerEvent (ruler, startDown, at (3140.0)));
    ruler.mouseUp (rulerEvent (ruler, startDown, at (3140.0)));
    CHECK (loopOf (controller) == std::optional<TickPair> (TickPair { 2880.0, 3120.0 }));
}

TEST_CASE ("TrackEditorPanel: clicking a ruler bracket clears the loop range; a drag after clearing does nothing", "[track-editor][loop][ruler]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCC, 0, 0);
    lotro::playbacktest::addNote (track, 60, 0, 9600);
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    controller.flushRebuild();
    TrackEditorPanel window (doc);
    window.setPlayback (&controller);
    window.setSize (800, 400);
    window.setTrack (track);
    window.setGridLength (GridLength::Eighth);

    using Access = TrackEditorPanelTestAccess;
    auto& ruler = Access::ruler (window);
    auto& roll = Access::roll (window);
    REQUIRE (controller.setLoopRange (1200.0, 2400.0));
    const auto startBracket = ruler.loopHandleBounds (true);
    REQUIRE (startBracket.has_value());
    const juce::Point<int> startDown { startBracket->getX() + 2, startBracket->getCentreY() };

    ruler.mouseDown (rulerEvent (ruler, startDown, startDown));
    ruler.mouseUp (rulerEvent (ruler, startDown, startDown));
    CHECK_FALSE (controller.getLoopRange().has_value());

    // Cleared mid-drag (e.g. Escape): the rest of the drag must not bring a range back.
    REQUIRE (controller.setLoopRange (1200.0, 2400.0));
    ruler.mouseDown (rulerEvent (ruler, startDown, startDown));
    controller.clearLoopRange();
    const juce::Point<int> to { roll.xForTickInComponent (1700.0), 5 };
    ruler.mouseDrag (rulerEvent (ruler, startDown, to));
    ruler.mouseUp (rulerEvent (ruler, startDown, to));
    CHECK_FALSE (controller.getLoopRange().has_value());
}

TEST_CASE ("TrackEditorPanel: setStripButtonsHost works before and after setPlayback", "[track-editor][strip-buttons]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    StripButtonsHost host;
    host.menuState = [] { return MenuState {}; };

    SECTION ("host first, then playback")
    {
        SongDocument doc;
        lotro::playbacktest::RecordingSink sink;
        PlaybackController controller (doc, sink);
        TrackEditorPanel panel (doc);
        panel.setStripButtonsHost (&host);
        CHECK (panel.stripButtonsBarForTesting() == nullptr);   // no strip yet
        panel.setPlayback (&controller);
        CHECK (panel.stripButtonsBarForTesting() != nullptr);
    }

    SECTION ("playback first, then host")
    {
        SongDocument doc;
        lotro::playbacktest::RecordingSink sink;
        PlaybackController controller (doc, sink);
        TrackEditorPanel panel (doc);
        panel.setPlayback (&controller);
        CHECK (panel.stripButtonsBarForTesting() == nullptr);
        panel.setStripButtonsHost (&host);
        CHECK (panel.stripButtonsBarForTesting() != nullptr);
    }
}

TEST_CASE ("TrackEditorPanel: a 24 px menu bar with File, Edit, Navigate, Options, View sits across the top", "[track-editor-window][editor-menu]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    TrackEditorPanel panel (doc);
    panel.setSize (700, 400);

    using Access = TrackEditorPanelTestAccess;
    auto& bar = Access::menuBar (panel);
    CHECK (bar.getParentComponent() == &panel);
    CHECK (bar.isVisible());
    CHECK (TrackEditorPanel::menuBarHeight == 24);
    CHECK (bar.getBounds() == juce::Rectangle<int> (0, 0, 700, TrackEditorPanel::menuBarHeight));

    // Before playback (the empty state) the roll fills the rest.
    CHECK (Access::rollBounds (panel) == juce::Rectangle<int> (0, TrackEditorPanel::menuBarHeight, 700, 400 - TrackEditorPanel::menuBarHeight - EditorBottomBar::height));

    auto* model = bar.getModel();
    REQUIRE (model != nullptr);
    CHECK (model->getMenuBarNames() == juce::StringArray { "File", "Edit", "Navigate", "Options", "View" });
    CHECK (model->getMenuForIndex (0, "File").getNumItems() == 2);   // export, close (separators are not counted)
    CHECK (model->getMenuForIndex (2, "Navigate").getNumItems() == 14);   // marker left, right, semitone up, down, select / add nearest note, previous / next note, previous / next / all of the same pitch, add previous / next, cursor to selection start
    CHECK (model->getMenuForIndex (3, "Options").getNumItems() == 5);    // snap to grid, snap to key, note preview, grid length, grid division
    for (int i : { 1, 4 })
        CHECK (model->getMenuForIndex (i, {}).getNumItems() == 0);

    // Still there, still on top, once the strip/ruler/roll content is built.
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    panel.setPlayback (&controller);
    CHECK (bar.getParentComponent() == &panel);
    CHECK (bar.getBounds() == juce::Rectangle<int> (0, 0, 700, TrackEditorPanel::menuBarHeight));
    CHECK (Access::content (panel).getY() == bar.getBottom());
}

namespace
{
    juce::MouseEvent leftMouseAt (juce::Component& c, juce::Point<float> p)
    {
        const auto t = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), p,
                 juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                 juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                 juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
                 juce::MouseInputSource::defaultTiltY, &c, &c, t, p, t, 1, false };
    }
}

TEST_CASE ("TrackEditorPanel: pressing, dragging or hovering over an empty menu title leaves the menu bar unarmed", "[track-editor-window][editor-menu]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    TrackEditorPanel panel (doc);
    panel.setSize (700, 400);
    // The bar's hit-test (reallyContains -> contains) needs a peer, so the panel goes on the desktop.
    panel.addToDesktop (0);
    panel.setVisible (true);

    using Access = TrackEditorPanelTestAccess;
    auto& bar = Access::menuBar (panel);
    REQUIRE (bar.getNumChildComponents() == 5);   // one item component per title
    for (int i : { 1, 4 })   // Edit, View: File, Navigate and Options have items now
    {
        const auto centre = bar.getChildComponent (i)->getBounds().getCentre().toFloat();
        // JUCE never opens a popup for an empty menu and never reports it closed, so a
        // forwarded press would leave the bar armed (title lit, hover switching titles).
        bar.mouseDown (leftMouseAt (bar, centre));
        bar.mouseUp (leftMouseAt (bar, centre));
        bar.mouseDrag (leftMouseAt (bar, centre));
        bar.mouseMove (leftMouseAt (bar, centre));
        CHECK (Access::menuActivations (panel) == 0);
    }
    panel.removeFromDesktop();
}

TEST_CASE ("TrackEditorPanel: File > Export to New MIDI File asks for this track; Close Editor asks to close", "[track-editor-window][editor-menu]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCCu, 0, 0);
    const auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);
    TrackEditorPanel panel (doc);

    std::vector<juce::int64> exports;
    int closes = 0;
    panel.onExportTrackMidi = [&] (juce::int64 id) { exports.push_back (id); };
    panel.onCloseRequested = [&] { ++closes; };

    // No track: the export item is greyed and does nothing; close still works.
    auto* model = TrackEditorPanelTestAccess::menuBar (panel).getModel();
    const auto emptyMenu = model->getMenuForIndex (0, "File");   // the iterator keeps a reference
    juce::PopupMenu::MenuItemIterator empty (emptyMenu);
    REQUIRE (empty.next());
    CHECK_FALSE (empty.getItem().isEnabled);
    panel.invokeMenuCommand (EditorFileExportTrackMidi);
    CHECK (exports.empty());
    panel.invokeMenuCommand (EditorFileClose);
    CHECK (closes == 1);

    panel.setTrack (track);
    const auto loadedMenu = model->getMenuForIndex (0, "File");
    juce::PopupMenu::MenuItemIterator loaded (loadedMenu);
    REQUIRE (loaded.next());
    CHECK (loaded.getItem().isEnabled);
    model->menuItemSelected (EditorFileExportTrackMidi, 0);   // the bar's own dispatch path
    CHECK (exports == std::vector<juce::int64> { trackId });
    model->menuItemSelected (EditorFileClose, 0);
    CHECK (closes == 2);
}

TEST_CASE ("TrackEditorPanel: Options > Snap to Grid is ticked by the snap state and toggling it reports the new value", "[track-editor-window][editor-menu]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    TrackEditorPanel panel (doc);
    CHECK (panel.getSnapToGrid());   // on until told otherwise, like the setting's default

    std::vector<bool> reported;
    panel.onSnapToGridChanged = [&] (bool on) { reported.push_back (on); };

    auto* model = TrackEditorPanelTestAccess::menuBar (panel).getModel();
    const auto ticked = [model]
    {
        const auto menu = model->getMenuForIndex (3, "Options");   // the iterator keeps a reference
        juce::PopupMenu::MenuItemIterator it (menu);
        REQUIRE (it.next());
        CHECK (it.getItem().itemID == EditorOptionsSnapToGrid);
        CHECK (it.getItem().isEnabled);   // no track needed
        return it.getItem().isTicked;
    };
    CHECK (ticked());

    panel.setSnapToGrid (false);   // pushed from the setting: no report back
    CHECK_FALSE (ticked());
    CHECK (reported.empty());

    model->menuItemSelected (EditorOptionsSnapToGrid, 3);   // the bar's own dispatch path
    CHECK (panel.getSnapToGrid());
    CHECK (ticked());
    CHECK (reported == std::vector<bool> { true });

    panel.invokeMenuCommand (EditorOptionsSnapToGrid);
    CHECK_FALSE (panel.getSnapToGrid());
    CHECK (reported == std::vector<bool> { true, false });
}

namespace
{
    // A track editor on a 480-PPQ song with playback, a loaded track and a 1/4 grid.
    struct MarkerPanelFixture
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        SongDocument doc;
        juce::ValueTree track = doc.addTrack ("A", (int) 0xFFAABBCC, 0, 0);
        lotro::playbacktest::RecordingSink sink;
        std::unique_ptr<PlaybackController> controller;
        std::unique_ptr<TrackEditorPanel> panel;

        MarkerPanelFixture()
        {
            lotro::playbacktest::addNote (track, 60, 0, 9600);
            controller = std::make_unique<PlaybackController> (doc, sink);
            controller->flushRebuild();
            panel = std::make_unique<TrackEditorPanel> (doc);
            panel->setPlayback (controller.get());
            panel->setTrack (track);
            panel->setSize (800, 400);
            panel->setGridLength (GridLength::Quarter);
        }
        ~MarkerPanelFixture() { panel.reset(); }

        bool navigateEnabled (int item)
        {
            const auto menu = TrackEditorPanelTestAccess::menuBar (*panel).getModel()->getMenuForIndex (2, "Navigate");
            juce::PopupMenu::MenuItemIterator it (menu);
            for (int i = 0; i <= item; ++i)
                REQUIRE (it.next());
            return it.getItem().isEnabled;
        }
    };

    void rulerClick (TrackEditorPanel& panel, double tick, juce::ModifierKeys mods = {})
    {
        auto& ruler = TrackEditorPanelTestAccess::ruler (panel);
        auto* source = juce::Desktop::getInstance().getMouseSource (0);
        const auto now = juce::Time::getCurrentTime();
        const juce::Point<float> pos ((float) TrackEditorPanelTestAccess::roll (panel).xForTickInComponent (tick), 20.0f);
        ruler.mouseDown (juce::MouseEvent (*source, pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                           &ruler, &ruler, now, pos, now, 1, false));
    }
}

TEST_CASE ("TrackEditorPanel: Navigate > Move Marker Left / Right by Grid step the start marker along the grid", "[track-editor-window][editor-menu][marker]")
{
    MarkerPanelFixture f;
    f.controller->setMarkerTick (1000.0);   // between lines
    CHECK (f.navigateEnabled (0));
    CHECK (f.navigateEnabled (1));

    auto* model = TrackEditorPanelTestAccess::menuBar (*f.panel).getModel();
    model->menuItemSelected (EditorNavigateMarkerRight, 2);
    CHECK (*f.controller->getMarkerTick() == 1440.0);
    f.panel->invokeMenuCommand (EditorNavigateMarkerRight);
    CHECK (*f.controller->getMarkerTick() == 1920.0);
    f.panel->invokeMenuCommand (EditorNavigateMarkerLeft);
    f.panel->invokeMenuCommand (EditorNavigateMarkerLeft);
    f.panel->invokeMenuCommand (EditorNavigateMarkerLeft);
    f.panel->invokeMenuCommand (EditorNavigateMarkerLeft);
    f.panel->invokeMenuCommand (EditorNavigateMarkerLeft);
    CHECK (*f.controller->getMarkerTick() == 0.0);   // stops at the start
}

TEST_CASE ("TrackEditorPanel: with no marker the first step starts from the playhead", "[track-editor-window][editor-menu][marker]")
{
    MarkerPanelFixture f;
    f.controller->seekToTick (2000.0);
    REQUIRE_FALSE (f.controller->getMarkerTick().has_value());
    f.panel->invokeMenuCommand (EditorNavigateMarkerLeft);
    REQUIRE (f.controller->getMarkerTick().has_value());
    CHECK (*f.controller->getMarkerTick() == 1920.0);
    CHECK (f.controller->getPositionTicks() == Catch::Approx (2000.0).margin (1.0));   // the playhead stays
}

TEST_CASE ("TrackEditorPanel: the marker steps are greyed and do nothing with the grid Off or no track", "[track-editor-window][editor-menu][marker]")
{
    MarkerPanelFixture f;
    f.controller->setMarkerTick (1000.0);
    f.panel->setGridLength (GridLength::Off);
    CHECK_FALSE (f.navigateEnabled (0));
    CHECK_FALSE (f.navigateEnabled (1));
    f.panel->invokeMenuCommand (EditorNavigateMarkerRight);
    CHECK (*f.controller->getMarkerTick() == 1000.0);
    CHECK_FALSE (f.panel->keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));   // left to the roll's scrolling

    f.panel->setGridLength (GridLength::Quarter);
    f.panel->clearTrack();
    CHECK_FALSE (f.navigateEnabled (1));
    f.panel->invokeMenuCommand (EditorNavigateMarkerRight);
    CHECK (*f.controller->getMarkerTick() == 1000.0);
}

TEST_CASE ("TrackEditorPanel: Left and Right step the marker, from the panel and from the roll before the roll's scrolling", "[track-editor-window][marker]")
{
    MarkerPanelFixture f;
    f.controller->setMarkerTick (960.0);
    CHECK (f.panel->keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
    CHECK (*f.controller->getMarkerTick() == 1440.0);
    CHECK (f.panel->keyPressed (juce::KeyPress (juce::KeyPress::leftKey)));
    CHECK (*f.controller->getMarkerTick() == 960.0);

    // The roll's canvas has keyboard focus while editing; its Viewport would scroll on arrows.
    auto& canvas = TrackEditorPanelTestAccess::rollCanvas (*f.panel);
    REQUIRE (canvas.getWantsKeyboardFocus());
    CHECK (canvas.keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
    CHECK (*f.controller->getMarkerTick() == 1440.0);

    // Modified arrows are not the marker commands.
    CHECK_FALSE (f.panel->keyPressed (juce::KeyPress (juce::KeyPress::rightKey, juce::ModifierKeys::shiftModifier, 0)));
    CHECK (*f.controller->getMarkerTick() == 1440.0);
}

TEST_CASE ("TrackEditorPanel: with Snap to Grid on a ruler click sets the marker on the nearest grid line; off, exactly where clicked", "[track-editor-window][marker]")
{
    MarkerPanelFixture f;
    rulerClick (*f.panel, 1100.0);
    REQUIRE (f.controller->getMarkerTick().has_value());
    CHECK (*f.controller->getMarkerTick() == 960.0);

    f.panel->setSnapToGrid (false);
    rulerClick (*f.panel, 1100.0);
    CHECK (*f.controller->getMarkerTick() == Catch::Approx (1100.0).margin (30.0));
    CHECK (*f.controller->getMarkerTick() != 960.0);

    f.panel->setSnapToGrid (true);
    f.panel->setGridLength (GridLength::Off);   // grid Off: nothing to snap to
    rulerClick (*f.panel, 1100.0);
    CHECK (*f.controller->getMarkerTick() != 960.0);
}

TEST_CASE ("TrackEditorPanel: Alt on a ruler click sets the marker exactly where clicked even with Snap to Grid on", "[track-editor-window][marker][grid-snap]")
{
    MarkerPanelFixture f;
    REQUIRE (f.panel->getSnapToGrid());
    rulerClick (*f.panel, 1100.0, juce::ModifierKeys (juce::ModifierKeys::altModifier));
    REQUIRE (f.controller->getMarkerTick().has_value());
    CHECK (*f.controller->getMarkerTick() == Catch::Approx (1100.0).margin (30.0));
    CHECK (*f.controller->getMarkerTick() != 960.0);
}

TEST_CASE ("TrackEditorPanel: Navigate > Move Up / Down a Semitone step the active row, from the menu and the arrow keys", "[track-editor-window][editor-menu][active-row]")
{
    MarkerPanelFixture f;
    auto& roll = TrackEditorPanelTestAccess::roll (*f.panel);
    CHECK_FALSE (f.navigateEnabled (3));   // no active row yet
    CHECK_FALSE (f.navigateEnabled (4));
    CHECK_FALSE (f.panel->keyPressed (juce::KeyPress (juce::KeyPress::upKey)));   // left to the roll's scrolling

    TrackEditorPanelTestAccess::clickRow (*f.panel, 64);
    REQUIRE (roll.getActivePitch() == 64);
    CHECK (f.navigateEnabled (3));
    CHECK (f.navigateEnabled (4));

    TrackEditorPanelTestAccess::menuBar (*f.panel).getModel()->menuItemSelected (EditorNavigatePitchUp, 2);
    CHECK (roll.getActivePitch() == 65);
    f.panel->invokeMenuCommand (EditorNavigatePitchDown);
    f.panel->invokeMenuCommand (EditorNavigatePitchDown);
    CHECK (roll.getActivePitch() == 63);

    CHECK (f.panel->keyPressed (juce::KeyPress (juce::KeyPress::upKey)));
    CHECK (roll.getActivePitch() == 64);
    auto& canvas = TrackEditorPanelTestAccess::rollCanvas (*f.panel);
    CHECK (canvas.keyPressed (juce::KeyPress (juce::KeyPress::downKey)));   // before the Viewport's scrolling
    CHECK (roll.getActivePitch() == 63);

    // Modified arrows are not the semitone commands.
    CHECK_FALSE (f.panel->keyPressed (juce::KeyPress (juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0)));
    CHECK (roll.getActivePitch() == 63);

    // Independent of the grid: still steps with the grid Off.
    f.panel->setGridLength (GridLength::Off);
    CHECK (f.navigateEnabled (3));
    CHECK (f.panel->keyPressed (juce::KeyPress (juce::KeyPress::upKey)));
    CHECK (roll.getActivePitch() == 64);
}

TEST_CASE ("TrackEditorPanel: the active row stays across tracks and is gone once the editor has no track", "[track-editor-window][active-row]")
{
    MarkerPanelFixture f;
    auto& roll = TrackEditorPanelTestAccess::roll (*f.panel);
    TrackEditorPanelTestAccess::clickRow (*f.panel, 64);

    auto other = f.doc.addTrack ("B", (int) 0xFF112233, 1, 0);
    f.panel->setTrack (other);
    CHECK (roll.getActivePitch() == 64);
    CHECK (f.navigateEnabled (3));

    f.panel->clearTrack();
    CHECK (roll.getActivePitch() == -1);
    CHECK_FALSE (f.navigateEnabled (3));
    CHECK_FALSE (f.panel->keyPressed (juce::KeyPress (juce::KeyPress::upKey)));
}

TEST_CASE ("TrackEditorPanel: Navigate > Select Note Nearest Cursor selects the note starting nearest the marker, else the playhead", "[track-editor-window][editor-menu][note-nav]")
{
    MarkerPanelFixture f;   // one long note at 0
    lotro::playbacktest::addNote (f.track, 72, 2000, 480);
    lotro::playbacktest::addNote (f.track, 48, 4000, 480);
    f.panel->setTrack (f.track);   // reload the note source
    const auto notes = SongDocument::getNotesNode (f.track);
    using Access = TrackEditorPanelTestAccess;

    CHECK (f.navigateEnabled (6));   // no active row needed
    f.controller->setMarkerTick (2900.0);
    Access::menuBar (*f.panel).getModel()->menuItemSelected (EditorNavigateSelectNearestNote, 2);
    CHECK (Access::numSelected (*f.panel) == 1);
    CHECK (Access::isSelected (*f.panel, notes.getChild (1)));

    f.controller->clearMarker();
    f.controller->seekToTick (3500.0);
    f.panel->invokeMenuCommand (EditorNavigateSelectNearestNote);
    CHECK (Access::numSelected (*f.panel) == 1);
    CHECK (Access::isSelected (*f.panel, notes.getChild (2)));
    CHECK_FALSE (f.controller->getMarkerTick().has_value());   // the cursor stays put
    CHECK (f.controller->getPositionTicks() == Catch::Approx (3500.0).margin (1.0));
}

TEST_CASE ("TrackEditorPanel: Select Note Nearest Cursor is greyed with no notes or no track", "[track-editor-window][editor-menu][note-nav]")
{
    MarkerPanelFixture f;
    auto empty = f.doc.addTrack ("Empty", (int) 0xFF112233, 1, 0);
    f.panel->setTrack (empty);
    CHECK_FALSE (f.navigateEnabled (6));
    f.panel->invokeMenuCommand (EditorNavigateSelectNearestNote);   // nothing to select, no crash

    f.panel->setTrack (f.track);
    CHECK (f.navigateEnabled (6));
    f.panel->clearTrack();
    CHECK_FALSE (f.navigateEnabled (6));
}

TEST_CASE ("TrackEditorPanel: Navigate > Add Note Nearest Cursor to Selection adds the next nearest note until all are selected", "[track-editor-window][editor-menu][note-nav]")
{
    MarkerPanelFixture f;   // one long note at 0
    lotro::playbacktest::addNote (f.track, 72, 2000, 480);
    f.panel->setTrack (f.track);
    const auto notes = SongDocument::getNotesNode (f.track);
    using Access = TrackEditorPanelTestAccess;

    f.controller->setMarkerTick (1900.0);
    CHECK (f.navigateEnabled (7));
    Access::menuBar (*f.panel).getModel()->menuItemSelected (EditorNavigateAddNearestNote, 2);
    CHECK (Access::numSelected (*f.panel) == 1);
    CHECK (Access::isSelected (*f.panel, notes.getChild (1)));

    f.panel->invokeMenuCommand (EditorNavigateAddNearestNote);
    CHECK (Access::numSelected (*f.panel) == 2);
    CHECK (Access::isSelected (*f.panel, notes.getChild (0)));
    CHECK_FALSE (f.navigateEnabled (7));   // nothing left to add
    CHECK (f.navigateEnabled (6));         // Select Note Nearest Cursor still works

    f.panel->clearTrack();
    CHECK_FALSE (f.navigateEnabled (7));
}

TEST_CASE ("TrackEditorPanel: Navigate > Select Next / Previous Note select through the notes and move the marker to each start", "[track-editor-window][editor-menu][note-nav]")
{
    MarkerPanelFixture f;   // one long note at 0 (pitch 60)
    lotro::playbacktest::addNote (f.track, 72, 2000, 480);
    lotro::playbacktest::addNote (f.track, 48, 2000, 480);
    lotro::playbacktest::addNote (f.track, 55, 4000, 480);
    f.panel->setTrack (f.track);   // reload the note source
    const auto notes = SongDocument::getNotesNode (f.track);
    auto& roll = TrackEditorPanelTestAccess::roll (*f.panel);
    using Access = TrackEditorPanelTestAccess;

    f.controller->setMarkerTick (1000.0);
    CHECK (f.navigateEnabled (9));
    CHECK (f.navigateEnabled (10));
    Access::menuBar (*f.panel).getModel()->menuItemSelected (EditorNavigateSelectNextNote, 2);
    CHECK (Access::numSelected (*f.panel) == 1);
    CHECK (Access::isSelected (*f.panel, notes.getChild (2)));   // 48, the chord's lowest
    CHECK (*f.controller->getMarkerTick() == 2000.0);            // exact, though not on the 480 grid
    CHECK (roll.getActivePitch() == 48);

    f.panel->invokeMenuCommand (EditorNavigateSelectNextNote);
    CHECK (Access::isSelected (*f.panel, notes.getChild (1)));   // 72, same start
    CHECK (*f.controller->getMarkerTick() == 2000.0);
    f.panel->invokeMenuCommand (EditorNavigateSelectNextNote);
    CHECK (Access::isSelected (*f.panel, notes.getChild (3)));
    CHECK (*f.controller->getMarkerTick() == 4000.0);
    CHECK_FALSE (f.navigateEnabled (10));   // the last note
    f.panel->invokeMenuCommand (EditorNavigateSelectNextNote);   // nothing happens
    CHECK (Access::isSelected (*f.panel, notes.getChild (3)));
    CHECK (*f.controller->getMarkerTick() == 4000.0);

    f.panel->invokeMenuCommand (EditorNavigateSelectPreviousNote);
    CHECK (Access::isSelected (*f.panel, notes.getChild (1)));   // 72, the chord's highest
    CHECK (*f.controller->getMarkerTick() == 2000.0);
    f.panel->invokeMenuCommand (EditorNavigateSelectPreviousNote);
    f.panel->invokeMenuCommand (EditorNavigateSelectPreviousNote);
    CHECK (Access::isSelected (*f.panel, notes.getChild (0)));
    CHECK (*f.controller->getMarkerTick() == 0.0);
    CHECK (Access::numSelected (*f.panel) == 1);
    CHECK_FALSE (f.navigateEnabled (9));   // the first note
}

TEST_CASE ("TrackEditorPanel: Select Next Note with no marker steps from the playhead", "[track-editor-window][editor-menu][note-nav]")
{
    MarkerPanelFixture f;
    lotro::playbacktest::addNote (f.track, 72, 2000, 480);
    f.panel->setTrack (f.track);
    const auto notes = SongDocument::getNotesNode (f.track);
    using Access = TrackEditorPanelTestAccess;

    f.controller->clearMarker();
    f.controller->seekToTick (1500.0);
    f.panel->invokeMenuCommand (EditorNavigateSelectNextNote);
    CHECK (Access::isSelected (*f.panel, notes.getChild (1)));
    CHECK (*f.controller->getMarkerTick() == 2000.0);
}

TEST_CASE ("TrackEditorPanel: Select Previous / Next Note are greyed with no notes or no track", "[track-editor-window][editor-menu][note-nav]")
{
    MarkerPanelFixture f;
    auto empty = f.doc.addTrack ("Empty", (int) 0xFF112233, 1, 0);
    f.panel->setTrack (empty);
    CHECK_FALSE (f.navigateEnabled (9));
    CHECK_FALSE (f.navigateEnabled (10));
    f.panel->invokeMenuCommand (EditorNavigateSelectNextNote);       // nothing to select, no crash
    f.panel->invokeMenuCommand (EditorNavigateSelectPreviousNote);

    f.panel->clearTrack();
    CHECK_FALSE (f.navigateEnabled (9));
    CHECK_FALSE (f.navigateEnabled (10));
    f.panel->invokeMenuCommand (EditorNavigateSelectNextNote);
}

TEST_CASE ("TrackEditorPanel: Navigate > Select Next / Previous Note of the Same Pitch step along the selected note's pitch and move the marker", "[track-editor-window][editor-menu][same-pitch]")
{
    MarkerPanelFixture f;   // one long note at 0 (pitch 60)
    lotro::playbacktest::addNote (f.track, 72, 2000, 480);
    lotro::playbacktest::addNote (f.track, 60, 2000, 480);
    lotro::playbacktest::addNote (f.track, 55, 3000, 480);
    lotro::playbacktest::addNote (f.track, 60, 4000, 480);
    f.panel->setTrack (f.track);
    const auto notes = SongDocument::getNotesNode (f.track);
    auto& roll = TrackEditorPanelTestAccess::roll (*f.panel);
    using Access = TrackEditorPanelTestAccess;

    f.controller->setMarkerTick (0.0);
    CHECK_FALSE (f.navigateEnabled (12));   // nothing selected
    CHECK_FALSE (f.navigateEnabled (13));
    CHECK_FALSE (f.navigateEnabled (14));
    f.panel->invokeMenuCommand (EditorNavigateSelectNextSamePitch);   // nothing happens
    CHECK (Access::numSelected (*f.panel) == 0);

    f.panel->invokeMenuCommand (EditorNavigateSelectNextNote);   // the note at 0
    REQUIRE (Access::isSelected (*f.panel, notes.getChild (0)));
    CHECK_FALSE (f.navigateEnabled (12));   // the first 60
    CHECK (f.navigateEnabled (13));
    CHECK (f.navigateEnabled (14));

    Access::menuBar (*f.panel).getModel()->menuItemSelected (EditorNavigateSelectNextSamePitch, 2);
    CHECK (Access::numSelected (*f.panel) == 1);
    CHECK (Access::isSelected (*f.panel, notes.getChild (2)));   // 60 at 2000, not 72
    CHECK (*f.controller->getMarkerTick() == 2000.0);            // exact, though not on the 480 grid
    CHECK (roll.getActivePitch() == 60);

    f.panel->invokeMenuCommand (EditorNavigateSelectNextSamePitch);
    CHECK (Access::isSelected (*f.panel, notes.getChild (4)));
    CHECK (*f.controller->getMarkerTick() == 4000.0);
    CHECK_FALSE (f.navigateEnabled (13));   // the last 60
    f.panel->invokeMenuCommand (EditorNavigateSelectNextSamePitch);   // nothing happens
    CHECK (Access::isSelected (*f.panel, notes.getChild (4)));
    CHECK (*f.controller->getMarkerTick() == 4000.0);

    f.panel->invokeMenuCommand (EditorNavigateSelectPreviousSamePitch);
    CHECK (Access::isSelected (*f.panel, notes.getChild (2)));
    CHECK (*f.controller->getMarkerTick() == 2000.0);
    f.panel->invokeMenuCommand (EditorNavigateSelectPreviousSamePitch);
    CHECK (Access::isSelected (*f.panel, notes.getChild (0)));
    CHECK (*f.controller->getMarkerTick() == 0.0);
    CHECK (Access::numSelected (*f.panel) == 1);
}

TEST_CASE ("TrackEditorPanel: Navigate > Select All Notes of the Same Pitch selects the pitch and leaves the marker", "[track-editor-window][editor-menu][same-pitch]")
{
    MarkerPanelFixture f;   // one long note at 0 (pitch 60)
    lotro::playbacktest::addNote (f.track, 72, 2000, 480);
    lotro::playbacktest::addNote (f.track, 60, 4000, 480);
    f.panel->setTrack (f.track);
    const auto notes = SongDocument::getNotesNode (f.track);
    using Access = TrackEditorPanelTestAccess;

    f.controller->setMarkerTick (3900.0);
    f.panel->invokeMenuCommand (EditorNavigateSelectNearestNote);   // 60 at 4000
    REQUIRE (Access::isSelected (*f.panel, notes.getChild (2)));
    f.controller->setMarkerTick (1000.0);
    CHECK (f.navigateEnabled (14));
    Access::menuBar (*f.panel).getModel()->menuItemSelected (EditorNavigateSelectAllSamePitch, 2);
    CHECK (Access::numSelected (*f.panel) == 2);
    CHECK (Access::isSelected (*f.panel, notes.getChild (0)));
    CHECK (Access::isSelected (*f.panel, notes.getChild (2)));
    CHECK (*f.controller->getMarkerTick() == 1000.0);

    // Two selected now: the whole group greys out and does nothing.
    CHECK_FALSE (f.navigateEnabled (12));
    CHECK_FALSE (f.navigateEnabled (13));
    CHECK_FALSE (f.navigateEnabled (14));
    f.panel->invokeMenuCommand (EditorNavigateSelectPreviousSamePitch);
    CHECK (Access::numSelected (*f.panel) == 2);
    CHECK (*f.controller->getMarkerTick() == 1000.0);

    // The only note of its pitch: nothing else to select.
    f.controller->setMarkerTick (2000.0);
    f.panel->invokeMenuCommand (EditorNavigateSelectNearestNote);   // 72
    REQUIRE (Access::numSelected (*f.panel) == 1);
    CHECK_FALSE (f.navigateEnabled (14));

    f.panel->clearTrack();
    CHECK_FALSE (f.navigateEnabled (12));
    CHECK_FALSE (f.navigateEnabled (13));
    CHECK_FALSE (f.navigateEnabled (14));
    f.panel->invokeMenuCommand (EditorNavigateSelectAllSamePitch);   // no track, no crash
}

TEST_CASE ("TrackEditorPanel: Navigate > Add Next / Previous Note to Selection grow the selection and move the marker to each added start", "[track-editor-window][editor-menu][extend-selection]")
{
    MarkerPanelFixture f;   // one long note at 0 (pitch 60)
    lotro::playbacktest::addNote (f.track, 72, 2000, 480);
    lotro::playbacktest::addNote (f.track, 48, 2000, 480);
    lotro::playbacktest::addNote (f.track, 55, 4000, 480);
    f.panel->setTrack (f.track);
    const auto notes = SongDocument::getNotesNode (f.track);
    auto& roll = TrackEditorPanelTestAccess::roll (*f.panel);
    using Access = TrackEditorPanelTestAccess;

    f.controller->setMarkerTick (0.0);
    for (int i : { 16, 17, 18 })
        CHECK_FALSE (f.navigateEnabled (i));   // nothing selected
    f.panel->invokeMenuCommand (EditorNavigateAddNextNote);   // nothing happens
    CHECK (Access::numSelected (*f.panel) == 0);

    f.controller->setMarkerTick (2000.0);
    f.panel->invokeMenuCommand (EditorNavigateSelectNextNote);   // 48 at 2000
    REQUIRE (Access::isSelected (*f.panel, notes.getChild (2)));
    CHECK (f.navigateEnabled (16));
    CHECK (f.navigateEnabled (17));
    CHECK (f.navigateEnabled (18));

    Access::menuBar (*f.panel).getModel()->menuItemSelected (EditorNavigateAddNextNote, 2);
    CHECK (Access::numSelected (*f.panel) == 2);
    CHECK (Access::isSelected (*f.panel, notes.getChild (1)));   // 72, same start
    CHECK (*f.controller->getMarkerTick() == 2000.0);
    CHECK (roll.getActivePitch() == 72);
    f.panel->invokeMenuCommand (EditorNavigateAddNextNote);
    CHECK (Access::isSelected (*f.panel, notes.getChild (3)));
    CHECK (*f.controller->getMarkerTick() == 4000.0);
    CHECK_FALSE (f.navigateEnabled (17));   // the last note is selected

    f.panel->invokeMenuCommand (EditorNavigateAddPreviousNote);
    CHECK (Access::isSelected (*f.panel, notes.getChild (0)));
    CHECK (*f.controller->getMarkerTick() == 0.0);
    CHECK (Access::numSelected (*f.panel) == 4);
    CHECK_FALSE (f.navigateEnabled (16));   // the first note is selected
    f.panel->invokeMenuCommand (EditorNavigateAddPreviousNote);   // nothing happens
    CHECK (*f.controller->getMarkerTick() == 0.0);

    f.panel->clearTrack();
    for (int i : { 16, 17, 18 })
        CHECK_FALSE (f.navigateEnabled (i));
    f.panel->invokeMenuCommand (EditorNavigateAddNextNote);   // no track, no crash
}

TEST_CASE ("TrackEditorPanel: Navigate > Move Cursor to Start of Selection moves the marker to the earliest selected start", "[track-editor-window][editor-menu][extend-selection]")
{
    MarkerPanelFixture f;   // one long note at 0 (pitch 60)
    lotro::playbacktest::addNote (f.track, 72, 2000, 480);
    lotro::playbacktest::addNote (f.track, 48, 4010, 480);
    f.panel->setTrack (f.track);
    const auto notes = SongDocument::getNotesNode (f.track);
    using Access = TrackEditorPanelTestAccess;

    f.controller->setMarkerTick (2000.0);
    f.panel->invokeMenuCommand (EditorNavigateSelectNextNote);   // 72 at 2000
    f.panel->invokeMenuCommand (EditorNavigateAddNextNote);      // 48 at 4010; marker 4010
    REQUIRE (Access::numSelected (*f.panel) == 2);
    REQUIRE (*f.controller->getMarkerTick() == 4010.0);

    Access::menuBar (*f.panel).getModel()->menuItemSelected (EditorNavigateCursorToSelectionStart, 2);
    CHECK (*f.controller->getMarkerTick() == 2000.0);   // exact, though not on the 480 grid
    CHECK (Access::numSelected (*f.panel) == 2);         // the selection stays

    f.controller->clearMarker();
    f.panel->invokeMenuCommand (EditorNavigateCursorToSelectionStart);   // sets one when there is none
    CHECK (*f.controller->getMarkerTick() == 2000.0);
}

TEST_CASE ("TrackEditorPanel: the ruler's playhead cap follows the playhead", "[track-editor][playhead][theme]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    TrackEditorPanel window (doc);
    window.setPlayback (&controller);
    window.setSize (700, 400);

    using Access = TrackEditorPanelTestAccess;
    controller.seekToTick (0.0);
    const auto first = Access::ruler (window).playheadCapBounds();
    REQUIRE (first.has_value());
    const int repaints = Access::ruler (window).capRepaintsForTesting();
    controller.seekToTick (240.0);
    const auto later = Access::ruler (window).playheadCapBounds();
    REQUIRE (later.has_value());
    CHECK (later->getCentreX() > first->getCentreX());
    CHECK (Access::ruler (window).capRepaintsForTesting() > repaints);   // the owner told the ruler
}

namespace
{
    int noteCount (const juce::ValueTree& track) { return SongDocument::getNotesNode (track).getNumChildren(); }

    // Whether a double-click on `pitch`'s row adds a note.
    bool createsNoteOn (TrackEditorPanel& panel, const juce::ValueTree& track, int pitch)
    {
        const int before = noteCount (track);
        TrackEditorPanelTestAccess::doubleClickRow (panel, pitch);
        return noteCount (track) == before + 1;
    }

    // Options (menu 3) > Snap to Key (item 1) > Enabled (its first item), as the bar builds it.
    bool snapToKeyEnabledItemIsEnabled (TrackEditorPanel& panel)
    {
        const auto options = TrackEditorPanelTestAccess::menuBar (panel).getModel()->getMenuForIndex (3, "Options");
        juce::PopupMenu::MenuItemIterator it (options);
        REQUIRE (it.next());
        REQUIRE (it.next());
        REQUIRE (it.getItem().subMenu != nullptr);
        juce::PopupMenu::MenuItemIterator sub (*it.getItem().subMenu);
        REQUIRE (sub.next());
        return sub.getItem().isEnabled;
    }

    void pick (TrackEditorPanel& panel, int commandId) { panel.invokeMenuCommand (commandId); }
}

TEST_CASE ("TrackEditorPanel: Snap to Key starts off, so any pitch can be added", "[track-editor][snap-to-key]")
{
    MarkerPanelFixture f;
    f.doc.setSongKey (0);   // C major
    CHECK_FALSE (f.panel->getSnapToKey());
    CHECK (createsNoteOn (*f.panel, f.track, 61));
}

TEST_CASE ("TrackEditorPanel: Snap to Key Enabled refuses an off-key double-click and allows an in-key one", "[track-editor][snap-to-key]")
{
    MarkerPanelFixture f;
    f.doc.setSongKey (0);   // C major
    pick (*f.panel, EditorOptionsSnapToKeyEnabled);
    REQUIRE (f.panel->getSnapToKey());

    CHECK_FALSE (createsNoteOn (*f.panel, f.track, 61));
    CHECK (createsNoteOn (*f.panel, f.track, 62));
}

TEST_CASE ("TrackEditorPanel: Snap to Key follows the song key, through undo, until something is picked", "[track-editor][snap-to-key]")
{
    MarkerPanelFixture f;
    f.doc.setSongKey (0);   // C major
    pick (*f.panel, EditorOptionsSnapToKeyEnabled);
    REQUIRE (f.panel->currentSnapKey() == snapKeyForSongKey (0));

    f.doc.setSongKey (21);   // A minor
    CHECK (f.panel->currentSnapKey() == snapKeyForSongKey (21));
    CHECK (snapRoots()[(std::size_t) f.panel->currentSnapKey()->rootIndex].name == std::string ("A"));
    CHECK (f.panel->currentSnapKey()->scale == SnapScale::naturalMinor);

    f.doc.undo();
    CHECK (f.panel->currentSnapKey() == snapKeyForSongKey (0));
    CHECK_FALSE (createsNoteOn (*f.panel, f.track, 61));
}

TEST_CASE ("TrackEditorPanel: with no song key and nothing picked, Snap to Key is greyed and blocks nothing", "[track-editor][snap-to-key]")
{
    MarkerPanelFixture f;
    f.doc.setSongKey (0);
    pick (*f.panel, EditorOptionsSnapToKeyEnabled);
    f.doc.setSongKey (std::nullopt);

    CHECK_FALSE (f.panel->currentSnapKey().has_value());
    CHECK_FALSE (snapToKeyEnabledItemIsEnabled (*f.panel));
    CHECK (createsNoteOn (*f.panel, f.track, 61));

    f.doc.setSongKey (0);
    CHECK (snapToKeyEnabledItemIsEnabled (*f.panel));
}

TEST_CASE ("TrackEditorPanel: Enabled does nothing while there is no key", "[track-editor][snap-to-key]")
{
    MarkerPanelFixture f;
    pick (*f.panel, EditorOptionsSnapToKeyEnabled);
    CHECK_FALSE (f.panel->getSnapToKey());
}

TEST_CASE ("TrackEditorPanel: picking a Type freezes Snap to Key against later song key changes", "[track-editor][snap-to-key]")
{
    MarkerPanelFixture f;
    f.doc.setSongKey (0);   // C major
    pick (*f.panel, EditorOptionsSnapToKeyEnabled);
    pick (*f.panel, EditorSnapScaleBase + (int) SnapScale::majorTriad);

    for (int pitch : { 60, 64, 67, 72 })
        CHECK (createsNoteOn (*f.panel, f.track, pitch));
    for (int pitch : { 62, 65, 69 })
        CHECK_FALSE (createsNoteOn (*f.panel, f.track, pitch));

    f.doc.setSongKey (7);   // G major
    const auto key = f.panel->currentSnapKey();
    REQUIRE (key.has_value());
    CHECK (snapRoots()[(std::size_t) key->rootIndex].name == std::string ("C"));
    CHECK (key->scale == SnapScale::majorTriad);
}

TEST_CASE ("TrackEditorPanel: a Root picked with no song key uses Major and keeps its exact spelling", "[track-editor][snap-to-key]")
{
    MarkerPanelFixture f;
    pick (*f.panel, EditorSnapRootBase + 3);   // A♯
    const auto key = f.panel->currentSnapKey();
    REQUIRE (key.has_value());
    CHECK (key->rootIndex == 3);   // not B♭ (2)
    CHECK (key->scale == SnapScale::major);
    CHECK (snapToKeyEnabledItemIsEnabled (*f.panel));

    pick (*f.panel, EditorOptionsSnapToKeyEnabled);
    CHECK (f.panel->getSnapToKey());
    CHECK_FALSE (createsNoteOn (*f.panel, f.track, 59));   // B is not in A♯ major
    CHECK (createsNoteOn (*f.panel, f.track, 58));         // A♯
}

TEST_CASE ("TrackEditorPanel: a picked Root keeps the current type", "[track-editor][snap-to-key]")
{
    MarkerPanelFixture f;
    f.doc.setSongKey (21);   // A minor
    pick (*f.panel, EditorSnapRootBase + 7);   // G
    CHECK (f.panel->currentSnapKey() == SnapKey { 7, SnapScale::naturalMinor });
}

TEST_CASE ("TrackEditorPanel: clearTrack after a pick goes back to following the song key and keeps Enabled", "[track-editor][snap-to-key]")
{
    MarkerPanelFixture f;
    f.doc.setSongKey (0);
    pick (*f.panel, EditorOptionsSnapToKeyEnabled);
    pick (*f.panel, EditorSnapScaleBase + (int) SnapScale::minorBlues);

    f.panel->clearTrack();
    f.doc.setSongKey (7);   // G major
    CHECK (f.panel->currentSnapKey() == snapKeyForSongKey (7));
    CHECK (f.panel->getSnapToKey());
}

namespace
{
    // Renders one block and returns the NoteOn pitches the preview channels got in it.
    std::vector<int> previewNoteOns (MarkerPanelFixture& f)
    {
        const size_t before = f.sink.records.size();
        juce::AudioBuffer<float> buffer (2, 512);
        f.controller->engine().renderBlock (buffer, 0, 512);
        std::vector<int> pitches;
        for (size_t i = before; i < f.sink.records.size(); ++i)
        {
            const auto& e = f.sink.records[i].event;
            if (e.kind == PlaybackEventKind::NoteOn && e.virtualChannel >= kPreviewChannelBase)
                pitches.push_back (e.data1);
        }
        return pitches;
    }

    NotePreviewSettings previewSettings (bool insert, bool edit, bool velocity)
    {
        NotePreviewSettings s;
        s.onInsert = insert; s.onEdit = edit; s.onVelocity = velocity;
        return s;
    }

    using Pitches = std::vector<int>;
}

TEST_CASE ("TrackEditorPanel: Note Preview starts all off, and inserting is silent", "[track-editor][note-preview]")
{
    MarkerPanelFixture f;
    CHECK (f.panel->getNotePreview() == NotePreviewSettings {});
    TrackEditorPanelTestAccess::doubleClickRow (*f.panel, 62);
    REQUIRE (SongDocument::getNotesNode (f.track).getNumChildren() == 2);
    CHECK (previewNoteOns (f).empty());
}

TEST_CASE ("TrackEditorPanel: Preview notes when inserting sounds the inserted note and reports the change", "[track-editor][note-preview]")
{
    MarkerPanelFixture f;
    std::vector<NotePreviewSettings> reported;
    f.panel->onNotePreviewChanged = [&] (const NotePreviewSettings& s) { reported.push_back (s); };

    auto* model = TrackEditorPanelTestAccess::menuBar (*f.panel).getModel();
    model->menuItemSelected (EditorOptionsPreviewOnInsert, 3);
    REQUIRE (reported.size() == 1);
    CHECK (reported[0] == previewSettings (true, false, false));
    CHECK (f.panel->getNotePreview().onInsert);

    TrackEditorPanelTestAccess::doubleClickRow (*f.panel, 62);
    CHECK (previewNoteOns (f) == Pitches { 62 });

    f.panel->invokeMenuCommand (EditorOptionsPreviewOnInsert);
    REQUIRE (reported.size() == 2);
    CHECK (reported[1] == NotePreviewSettings {});
    TrackEditorPanelTestAccess::doubleClickRow (*f.panel, 64);
    CHECK (previewNoteOns (f).empty());
}

TEST_CASE ("TrackEditorPanel: Preview notes when editing sounds each new row of a drag", "[track-editor][note-preview]")
{
    MarkerPanelFixture f;
    f.panel->setNotePreview (previewSettings (false, true, false));
    std::vector<Pitches> heard;
    TrackEditorPanelTestAccess::dragNoteRows (*f.panel, 60, 240, 2, [&] { heard.push_back (previewNoteOns (f)); });
    CHECK (heard == std::vector<Pitches> { { 61 }, { 62 } });

    f.panel->setNotePreview (previewSettings (true, false, false));
    heard.clear();
    TrackEditorPanelTestAccess::dragNoteRows (*f.panel, 62, 240, 2, [&] { heard.push_back (previewNoteOns (f)); });
    CHECK (heard == std::vector<Pitches> { {}, {} });
    CHECK (previewNoteOns (f).empty());   // nothing on release either
}

TEST_CASE ("TrackEditorPanel: a drag of more than six notes, or of notes that do not start together, is silent", "[track-editor][note-preview]")
{
    SECTION ("seven notes starting together")
    {
        MarkerPanelFixture f;
        f.panel->setNotePreview (previewSettings (false, true, false));
        auto notes = SongDocument::getNotesNode (f.track);
        for (int p = 62; p <= 67; ++p)
            lotro::playbacktest::addNote (f.track, p, 0, 480);
        REQUIRE (notes.getNumChildren() == 7);
        for (int i = 0; i < notes.getNumChildren(); ++i)
            TrackEditorPanelTestAccess::addToSelection (*f.panel, notes.getChild (i));
        REQUIRE (TrackEditorPanelTestAccess::numSelected (*f.panel) == 7);

        Pitches heard;
        TrackEditorPanelTestAccess::dragNoteRows (*f.panel, 60, 240, 1, [&] { heard = previewNoteOns (f); });
        CHECK (heard.empty());
        CHECK ((int) notes.getChild (0).getProperty (SongIDs::pitch) == 61);   // the drag did move them
    }
    SECTION ("two notes with different starts")
    {
        MarkerPanelFixture f;
        f.panel->setNotePreview (previewSettings (false, true, false));
        auto notes = SongDocument::getNotesNode (f.track);
        lotro::playbacktest::addNote (f.track, 72, 480, 480);
        for (int i = 0; i < notes.getNumChildren(); ++i)
            TrackEditorPanelTestAccess::addToSelection (*f.panel, notes.getChild (i));
        REQUIRE (TrackEditorPanelTestAccess::numSelected (*f.panel) == 2);

        Pitches heard;
        TrackEditorPanelTestAccess::dragNoteRows (*f.panel, 60, 240, 1, [&] { heard = previewNoteOns (f); });
        CHECK (heard.empty());
        CHECK ((int) notes.getChild (1).getProperty (SongIDs::pitch) == 73);
    }
}

TEST_CASE ("TrackEditorPanel: without playback, a preview-enabled insert still adds the note", "[track-editor][note-preview]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCC, 0, 0);
    lotro::playbacktest::addNote (track, 60, 0, 480);
    TrackEditorPanel panel (doc);
    panel.setTrack (track);
    panel.setSize (800, 400);
    panel.setNotePreview (previewSettings (true, true, true));

    TrackEditorPanelTestAccess::doubleClickRow (panel, 62);
    CHECK (SongDocument::getNotesNode (track).getNumChildren() == 2);
}

TEST_CASE ("TrackEditorPanel: Preview notes when velocity changes toggles and reports but nothing triggers it yet", "[track-editor][note-preview]")
{
    MarkerPanelFixture f;
    std::vector<NotePreviewSettings> reported;
    f.panel->onNotePreviewChanged = [&] (const NotePreviewSettings& s) { reported.push_back (s); };

    f.panel->invokeMenuCommand (EditorOptionsPreviewOnVelocity);
    CHECK (f.panel->getNotePreview() == previewSettings (false, false, true));
    REQUIRE (reported.size() == 1);
    CHECK (reported[0] == previewSettings (false, false, true));

    f.panel->invokeMenuCommand (EditorOptionsPreviewOnEdit);
    CHECK (f.panel->getNotePreview() == previewSettings (false, true, true));
    REQUIRE (reported.size() == 2);
}

namespace
{
    // Whether the editor's Options menu ticks `id` (searching its submenus), and whether it is enabled.
    std::pair<bool, bool> optionsItemState (TrackEditorPanel& panel, int id)
    {
        const auto menu = TrackEditorPanelTestAccess::menuBar (panel).getModel()->getMenuForIndex (3, "Options");
        juce::PopupMenu::MenuItemIterator it (menu, true);
        while (it.next())
            if (it.getItem().itemID == id)
                return { it.getItem().isTicked, it.getItem().isEnabled };
        FAIL ("no Options item " << id);
        return {};
    }
}

TEST_CASE ("TrackEditorPanel: Options > Grid Length sets the editor's grid and is ticked by it", "[track-editor-window][editor-menu][grid-length]")
{
    MarkerPanelFixture f;   // 480 PPQ, 1/4 grid
    CHECK (f.panel->getGridLength() == GridLength::Quarter);
    CHECK (optionsItemState (*f.panel, EditorGridLengthBase + (int) GridLength::Quarter) == std::pair { true, true });

    f.panel->invokeMenuCommand (EditorGridLengthBase + (int) GridLength::SixtyFourth);
    CHECK (f.panel->getGridLength() == GridLength::SixtyFourth);
    CHECK (f.panel->getGridTicks() == 30);
    CHECK (optionsItemState (*f.panel, EditorGridLengthBase + (int) GridLength::SixtyFourth).first);
    CHECK_FALSE (optionsItemState (*f.panel, EditorGridLengthBase + (int) GridLength::Quarter).first);

    f.panel->invokeMenuCommand (EditorGridLengthBase + (int) GridLength::Whole);
    CHECK (f.panel->getGridTicks() == 1920);

    // Off: no grid, so the marker cannot step by it.
    f.panel->invokeMenuCommand (EditorGridLengthBase + (int) GridLength::Off);
    CHECK (f.panel->getGridTicks() == 0);
    CHECK_FALSE (f.navigateEnabled (0));
}

TEST_CASE ("TrackEditorPanel: Options > Grid Division sets triplet and dotted spacing on the current length, and a new length keeps it", "[track-editor-window][editor-menu][grid-length]")
{
    MarkerPanelFixture f;   // 480 PPQ, 1/4 grid
    CHECK (f.panel->getGridDivision() == GridDivision::Straight);
    CHECK (optionsItemState (*f.panel, EditorGridDivisionBase + (int) GridDivision::Straight) == std::pair { true, true });

    f.panel->invokeMenuCommand (EditorGridDivisionBase + (int) GridDivision::Triplet);
    CHECK (f.panel->getGridDivision() == GridDivision::Triplet);
    CHECK (f.panel->getGridTicks() == 320);
    CHECK (optionsItemState (*f.panel, EditorGridDivisionBase + (int) GridDivision::Triplet).first);

    f.panel->invokeMenuCommand (EditorGridLengthBase + (int) GridLength::Eighth);
    CHECK (f.panel->getGridDivision() == GridDivision::Triplet);
    CHECK (f.panel->getGridTicks() == 160);

    f.panel->invokeMenuCommand (EditorGridDivisionBase + (int) GridDivision::Dotted);
    CHECK (f.panel->getGridTicks() == 360);

    f.panel->setGridLength (GridLength::Sixteenth);   // as the main window's Grid Size does
    CHECK (f.panel->getGridDivision() == GridDivision::Dotted);
    CHECK (f.panel->getGridTicks() == 180);

    // Swing can be chosen and is ticked; for now it spaces the grid as Straight.
    f.panel->invokeMenuCommand (EditorGridDivisionBase + (int) GridDivision::Swing);
    CHECK (f.panel->getGridDivision() == GridDivision::Swing);
    CHECK (f.panel->getGridTicks() == 120);
    CHECK (optionsItemState (*f.panel, EditorGridDivisionBase + (int) GridDivision::Swing).first);
}

TEST_CASE ("TrackEditorPanel: the editor's grid moves the marker by the triplet spacing and snaps a ruler click to it", "[track-editor-window][grid-length][marker]")
{
    MarkerPanelFixture f;
    f.panel->setGridLength (GridLength::Eighth);
    f.panel->setGridDivision (GridDivision::Triplet);   // 160 ticks
    f.controller->setMarkerTick (0.0);
    f.panel->invokeMenuCommand (EditorNavigateMarkerRight);
    CHECK (*f.controller->getMarkerTick() == 160.0);

    rulerClick (*f.panel, 1100.0);   // nearest triplet line: 1120
    CHECK (*f.controller->getMarkerTick() == 1120.0);
}

TEST_CASE ("TrackEditorPanel: the grid follows the song's PPQ when a track is loaded", "[track-editor-window][grid-length]")
{
    MarkerPanelFixture f;
    f.panel->setGridDivision (GridDivision::Triplet);   // 1/4 triplet: 320 at 480 PPQ
    f.doc.getSourceMidiNode().setProperty (SongIDs::ticksPerQuarter, 96, nullptr);
    f.panel->setTrack (f.track);
    CHECK (f.panel->getGridLength() == GridLength::Quarter);
    CHECK (f.panel->getGridTicks() == 64);
}

TEST_CASE ("TrackEditorPanel: Grid Length and Grid Division are greyed with no track loaded", "[track-editor-window][editor-menu][grid-length]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    TrackEditorPanel panel (doc);
    CHECK_FALSE (optionsItemState (panel, EditorGridLengthBase + (int) GridLength::Quarter).second);
    CHECK_FALSE (optionsItemState (panel, EditorGridDivisionBase + (int) GridDivision::Triplet).second);
}

#include "PlaybackTestSupport.h"
#include "UI/Playback/PlaybackController.h"
#include "UI/Playback/PlayheadOverlay.h"
#include "UI/Playback/TimelineRuler.h"
#include "UI/SongsmithColours.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lotro;
using namespace lotro::playbacktest;

namespace
{
    juce::MouseEvent mouseAt (juce::Component& c, int x, int y = 3, juce::ModifierKeys mods = {})
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                 juce::Point<float> ((float) x, (float) y), mods,
                                 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, juce::Time::getCurrentTime(),
                                 juce::Point<float> ((float) x, (float) y), juce::Time::getCurrentTime(), 1, false);
    }

    const juce::ModifierKeys rightButton (juce::ModifierKeys::rightButtonModifier);
}

TEST_CASE ("PlayheadOverlay: ignores mouse clicks so it never blocks editing", "[playback][overlay]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    PlayheadOverlay overlay (controller, [] (double tick) { return (int) (tick * 0.1); });
    overlay.setSize (500, 100);
    // getInterceptsMouseClicks returns void and fills out-params; start both true so a no-op fails.
    bool onThis = true;
    bool onChildren = true;
    overlay.getInterceptsMouseClicks (onThis, onChildren);
    CHECK (! onThis);
    CHECK (! onChildren);
}

TEST_CASE ("PlayheadOverlay: x follows the controller's position through the supplied tick mapping", "[playback][overlay]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    addNote (addTrack (doc), 60, 0, 9600);
    controller.flushRebuild();
    PlayheadOverlay overlay (controller, [] (double tick) { return (int) (tick * 0.1); });
    overlay.setSize (500, 100);

    controller.seekToTick (1000.0);
    CHECK (overlay.currentX() == 100);
    controller.seekToTick (0.0);
    CHECK (overlay.currentX() == 0);
}

TEST_CASE ("TimelineRuler: left click and drag set the marker to the tick under the pointer, clamped at zero", "[playback][ruler][marker]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TimelineRuler ruler ([] (int x) { return (double) x * 10.0; });
    ruler.setSize (500, TimelineRuler::height);
    double markerTick = -1.0;
    bool seeked = false;
    ruler.onSetMarker = [&] (double t) { markerTick = t; };
    ruler.onSeek = [&] (double) { seeked = true; };

    ruler.mouseDown (mouseAt (ruler, 25, 20));
    CHECK (markerTick == Catch::Approx (250.0));
    ruler.mouseDrag (mouseAt (ruler, 40, 20));
    CHECK (markerTick == Catch::Approx (400.0));
    ruler.mouseDrag (mouseAt (ruler, -30, 20));
    CHECK (markerTick == Catch::Approx (0.0).margin (1e-9));
    CHECK (! seeked);   // a left click never moves the playhead
}

TEST_CASE ("TimelineRuler: right click and drag move the playhead instead", "[playback][ruler]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TimelineRuler ruler ([] (int x) { return (double) x * 10.0; });
    ruler.setSize (500, TimelineRuler::height);
    double seekTick = -1.0;
    bool markerSet = false;
    ruler.onSeek = [&] (double t) { seekTick = t; };
    ruler.onSetMarker = [&] (double) { markerSet = true; };

    ruler.mouseDown (mouseAt (ruler, 25, 20, rightButton));
    CHECK (seekTick == Catch::Approx (250.0));
    ruler.mouseDrag (mouseAt (ruler, 40, 20, rightButton));
    CHECK (seekTick == Catch::Approx (400.0));
    ruler.mouseDrag (mouseAt (ruler, -30, 20, rightButton));
    CHECK (seekTick == Catch::Approx (0.0).margin (1e-9));
    CHECK (! markerSet);
}

TEST_CASE ("TimelineRuler: the marker's triangle sits in the bar; pressing it clears the marker, anything else sets it", "[playback][ruler][marker]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TimelineRuler ruler ([] (int x) { return (double) x * 10.0; });
    ruler.setSize (500, TimelineRuler::height);
    std::optional<double> marker;
    ruler.setMarks ([] (double tick) { return (int) (tick / 10.0); },
                    [] { return RulerGrid { TempoMap ({}, 480), { 4, 4 } }; });
    ruler.setMarker ([&] { return marker; });
    ruler.onSetMarker = [&] (double t) { marker = t; };
    ruler.onClearMarker = [&] { marker.reset(); };

    // No marker yet: the same spot just sets one.
    CHECK (! ruler.markerHandleBounds().has_value());
    ruler.mouseDown (mouseAt (ruler, 100, 3));
    REQUIRE (marker.has_value());
    CHECK (*marker == Catch::Approx (1000.0));

    const auto handle = ruler.markerHandleBounds();
    REQUIRE (handle.has_value());
    CHECK (handle->getCentreX() == 100);
    CHECK (handle->getY() == 0);
    CHECK (handle->getWidth() >= 12);   // big enough to click
    CHECK (handle->getHeight() >= 8);

    // Elsewhere in the bar (even right next to the triangle's row) the marker moves...
    ruler.mouseDown (mouseAt (ruler, 300, 3));
    CHECK (*marker == Catch::Approx (3000.0));
    ruler.mouseDown (mouseAt (ruler, 100, TimelineRuler::height - 4));   // lower row: not the triangle
    CHECK (*marker == Catch::Approx (1000.0));

    // ...but pressing the triangle itself clears it, and a drag that follows does not set it again.
    ruler.mouseDown (mouseAt (ruler, 100, 3));
    CHECK (! marker.has_value());
    ruler.mouseDrag (mouseAt (ruler, 150, 3));
    CHECK (! marker.has_value());
}

TEST_CASE ("TimelineRuler: the marker's line and triangle are drawn through both rows", "[playback][ruler][marker]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TimelineRuler ruler ([] (int x) { return (double) x * 10.0; });
    ruler.setSize (500, TimelineRuler::height);
    std::optional<double> marker;
    ruler.setMarks ([] (double tick) { return (int) (tick / 10.0); },
                    [] { return RulerGrid { TempoMap ({}, 480), { 4, 4 } }; });
    ruler.setMarker ([&] { return marker; });

    const auto amber = juce::Colour (SongsmithColours::accentAmber);
    auto render = [&]
    {
        juce::Image image (juce::Image::ARGB, 500, TimelineRuler::height, true, juce::SoftwareImageType());
        juce::Graphics g (image);
        ruler.paintEntireComponent (g, false);
        return image;
    };

    marker.reset();
    CHECK (render().getPixelAt (100, 3) != amber);

    marker = 1000.0;   // x = 100
    const auto image = render();
    CHECK (image.getPixelAt (100, 3) == amber);                          // the triangle
    CHECK (image.getPixelAt (100, TimelineRuler::height - 3) == amber);  // the line, in the clock row
}

TEST_CASE ("MarkerOverlay: shows nothing until a marker is set", "[playback][overlay][marker]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    addNote (addTrack (doc), 60, 0, 9600);   // a MIDI is open: markers need something to mark
    controller.flushRebuild();
    MarkerOverlay overlay (controller, [] (double tick) { return (int) (tick * 0.1); });
    overlay.setSize (500, 100);

    CHECK (! overlay.currentX().has_value());
    controller.setMarkerTick (1000.0);
    REQUIRE (overlay.currentX().has_value());
    CHECK (*overlay.currentX() == 100);
    controller.clearMarker();
    CHECK (! overlay.currentX().has_value());
}

TEST_CASE ("MarkerOverlay: it is only a line and never takes clicks", "[playback][overlay][marker]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    addNote (addTrack (doc), 60, 0, 9600);   // a MIDI is open: markers need something to mark
    controller.flushRebuild();
    MarkerOverlay overlay (controller, [] (double tick) { return (int) (tick * 0.1); });
    overlay.setSize (500, 100);
    controller.setMarkerTick (1000.0);   // x = 100

    bool onThis = true;
    bool onChildren = true;
    overlay.getInterceptsMouseClicks (onThis, onChildren);
    CHECK (! onThis);
    CHECK (! onChildren);

    // The line runs the whole height, from the very top (the bar above carries the triangle).
    juce::Image image (juce::Image::ARGB, 500, 100, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    overlay.paintEntireComponent (g, false);
    const auto amber = juce::Colour (SongsmithColours::accentAmber);
    CHECK (image.getPixelAt (100, 0) == amber);
    CHECK (image.getPixelAt (100, 99) == amber);
    CHECK (image.getPixelAt (110, 50) != amber);
}

// ---- Loop range: the canvas overlay and the ruler's edge brackets ----

namespace
{
    juce::Image renderComponent (juce::Component& c)
    {
        juce::Image image (juce::Image::ARGB, c.getWidth(), c.getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g (image);
        c.paintEntireComponent (g, false);
        return image;
    }

    bool shaded (const juce::Image& image, int x, int y) { return image.getPixelAt (x, y).getAlpha() > 0; }
}

TEST_CASE ("LoopRangeOverlay: paints nothing without a range and shades exactly [x(start), x(end)) with one", "[playback][overlay][loop]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    addNote (addTrack (doc), 60, 0, 9600);
    controller.flushRebuild();
    LoopRangeOverlay overlay (controller, [] (double tick) { return (int) (tick * 0.1); });
    overlay.setSize (500, 100);

    CHECK (! overlay.currentXs().has_value());
    {
        const auto image = renderComponent (overlay);
        for (const int x : { 50, 100, 200, 299, 450 })
            CHECK (! shaded (image, x, 50));
    }

    controller.setLoopRange (1000.0, 3000.0);   // x 100 .. 300
    REQUIRE (overlay.currentXs().has_value());
    CHECK (overlay.currentXs()->first == 100);
    CHECK (overlay.currentXs()->second == 300);

    const auto image = renderComponent (overlay);
    CHECK (! shaded (image, 99, 50));
    CHECK (shaded (image, 100, 50));
    CHECK (shaded (image, 200, 50));
    CHECK (shaded (image, 299, 50));
    CHECK (! shaded (image, 300, 50));
    CHECK (shaded (image, 200, 0));    // the full height
    CHECK (shaded (image, 200, 99));
    // The edges are lines, stronger than the band between them.
    CHECK (image.getPixelAt (100, 50).getAlpha() > image.getPixelAt (200, 50).getAlpha());
    CHECK (image.getPixelAt (299, 50).getAlpha() > image.getPixelAt (200, 50).getAlpha());

    controller.clearLoopRange();
    CHECK (! overlay.currentXs().has_value());
    CHECK (! shaded (renderComponent (overlay), 200, 50));
}

TEST_CASE ("LoopRangeOverlay: a range running past the component is clipped to it", "[playback][overlay][loop]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    addNote (addTrack (doc), 60, 0, 9600);
    controller.flushRebuild();
    // Scrolled: tick 2000 sits at x 0, so the range starts left of the component and ends right of it.
    LoopRangeOverlay overlay (controller, [] (double tick) { return (int) ((tick - 2000.0) * 0.1); });
    overlay.setSize (500, 100);
    controller.setLoopRange (1000.0, 8000.0);   // x -100 .. 600

    REQUIRE (overlay.currentXs().has_value());
    CHECK (overlay.currentXs()->first == -100);
    CHECK (overlay.currentXs()->second == 600);
    const auto image = renderComponent (overlay);
    CHECK (shaded (image, 0, 50));
    CHECK (shaded (image, 499, 50));
    // No edge line is drawn at the component's border: both edges are off screen.
    CHECK (image.getPixelAt (0, 50).getAlpha() == image.getPixelAt (250, 50).getAlpha());
    CHECK (image.getPixelAt (499, 50).getAlpha() == image.getPixelAt (250, 50).getAlpha());
}

TEST_CASE ("LoopRangeOverlay: it never takes clicks", "[playback][overlay][loop]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    RecordingSink sink;
    PlaybackController controller (doc, sink);
    LoopRangeOverlay overlay (controller, [] (double tick) { return (int) (tick * 0.1); });
    overlay.setSize (500, 100);
    bool onThis = true;
    bool onChildren = true;
    overlay.getInterceptsMouseClicks (onThis, onChildren);
    CHECK (! onThis);
    CHECK (! onChildren);
}

namespace
{
    // A ruler at 10 ticks per pixel with marks on, reading its marker and loop range from the given optionals.
    void setUpLoopRuler (TimelineRuler& ruler, std::optional<double>& marker, std::optional<LoopRange>& loop)
    {
        ruler.setSize (500, TimelineRuler::height);
        ruler.setMarks ([] (double tick) { return (int) (tick / 10.0); },
                        [] { return RulerGrid { TempoMap ({}, 480), { 4, 4 } }; });
        ruler.setMarker ([&marker] { return marker; });
        ruler.setLoopRange ([&loop] { return loop; });
    }

    struct EdgeDrag
    {
        bool isStart = false;
        double tick = 0.0;
    };
}

TEST_CASE ("TimelineRuler: loop brackets sit in the top row at each edge, opening into the range", "[playback][ruler][loop]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TimelineRuler ruler ([] (int x) { return (double) x * 10.0; });
    std::optional<double> marker;
    std::optional<LoopRange> loop;
    setUpLoopRuler (ruler, marker, loop);

    CHECK (! ruler.loopHandleBounds (true).has_value());
    CHECK (! ruler.loopHandleBounds (false).has_value());

    loop = LoopRange { 1000.0, 3000.0 };   // x 100 .. 300
    const auto start = ruler.loopHandleBounds (true);
    const auto end = ruler.loopHandleBounds (false);
    REQUIRE (start.has_value());
    REQUIRE (end.has_value());
    CHECK (*start == juce::Rectangle<int> (100, 0, 8, 12));
    CHECK (*end == juce::Rectangle<int> (292, 0, 8, 12));

    // Without the tick -> x mapping there is nowhere to put them.
    TimelineRuler bare ([] (int x) { return (double) x * 10.0; });
    bare.setSize (500, TimelineRuler::height);
    bare.setLoopRange ([&loop] { return loop; });
    CHECK (! bare.loopHandleBounds (true).has_value());
}

TEST_CASE ("TimelineRuler: dragging a loop bracket moves that edge and never sets the marker", "[playback][ruler][loop]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TimelineRuler ruler ([] (int x) { return (double) x * 10.0; });
    std::optional<double> marker;
    std::optional<LoopRange> loop = LoopRange { 1000.0, 3000.0 };   // x 100 .. 300
    setUpLoopRuler (ruler, marker, loop);
    std::vector<EdgeDrag> drags;
    int clears = 0;
    int markerSets = 0;
    ruler.onLoopEdgeDragged = [&] (bool isStart, double tick) { drags.push_back ({ isStart, tick }); };
    ruler.onClearLoop = [&] { ++clears; };
    ruler.onSetMarker = [&] (double) { ++markerSets; };

    ruler.mouseDown (mouseAt (ruler, 102, 5));   // the start bracket
    ruler.mouseDrag (mouseAt (ruler, 142, 5));
    ruler.mouseUp (mouseAt (ruler, 142, 5));
    REQUIRE (drags.size() == 1);
    CHECK (drags[0].isStart);
    CHECK (drags[0].tick == Catch::Approx (1420.0));

    ruler.mouseDown (mouseAt (ruler, 297, 5));   // the end bracket
    ruler.mouseDrag (mouseAt (ruler, 257, 5));
    ruler.mouseUp (mouseAt (ruler, 257, 5));
    REQUIRE (drags.size() == 2);
    CHECK (! drags[1].isStart);
    CHECK (drags[1].tick == Catch::Approx (2570.0));

    CHECK (markerSets == 0);
    CHECK (! marker.has_value());
    CHECK (clears == 0);
}

TEST_CASE ("TimelineRuler: clicking a loop bracket without moving clears the range; a drag that comes back does not", "[playback][ruler][loop]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TimelineRuler ruler ([] (int x) { return (double) x * 10.0; });
    std::optional<double> marker;
    std::optional<LoopRange> loop = LoopRange { 1000.0, 3000.0 };
    setUpLoopRuler (ruler, marker, loop);
    int drags = 0;
    int clears = 0;
    int markerSets = 0;
    ruler.onLoopEdgeDragged = [&] (bool, double) { ++drags; };
    ruler.onClearLoop = [&] { ++clears; };
    ruler.onSetMarker = [&] (double) { ++markerSets; };

    ruler.mouseDown (mouseAt (ruler, 297, 5));   // end bracket, released in place
    CHECK (clears == 0);                         // clearing waits for the release
    ruler.mouseUp (mouseAt (ruler, 297, 5));
    CHECK (clears == 1);

    ruler.mouseDown (mouseAt (ruler, 102, 5));   // start bracket, a 2 px wobble still counts as a click
    ruler.mouseDrag (mouseAt (ruler, 104, 5));
    ruler.mouseUp (mouseAt (ruler, 104, 5));
    CHECK (clears == 2);

    ruler.mouseDown (mouseAt (ruler, 102, 5));   // out 40 px and back again: a drag, not a click
    ruler.mouseDrag (mouseAt (ruler, 142, 5));
    ruler.mouseDrag (mouseAt (ruler, 102, 5));
    ruler.mouseUp (mouseAt (ruler, 102, 5));
    CHECK (clears == 2);
    CHECK (drags >= 2);
    CHECK (markerSets == 0);

    // A later press elsewhere is an ordinary marker press again, and its release clears nothing.
    ruler.mouseDown (mouseAt (ruler, 200, 5));
    ruler.mouseUp (mouseAt (ruler, 200, 5));
    CHECK (clears == 2);
    CHECK (markerSets == 1);
}

TEST_CASE ("TimelineRuler: with a loop range, presses off the brackets still set the marker and right presses still seek", "[playback][ruler][loop]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TimelineRuler ruler ([] (int x) { return (double) x * 10.0; });
    std::optional<double> marker;
    std::optional<LoopRange> loop = LoopRange { 1000.0, 3000.0 };
    setUpLoopRuler (ruler, marker, loop);
    std::optional<double> seekTick;
    int drags = 0;
    int clears = 0;
    ruler.onSetMarker = [&] (double t) { marker = t; };
    ruler.onSeek = [&] (double t) { seekTick = t; };
    ruler.onLoopEdgeDragged = [&] (bool, double) { ++drags; };
    ruler.onClearLoop = [&] { ++clears; };

    ruler.mouseDown (mouseAt (ruler, 200, 5));                          // inside the range, off the brackets
    REQUIRE (marker.has_value());
    CHECK (*marker == Catch::Approx (2000.0));
    ruler.mouseUp (mouseAt (ruler, 200, 5));
    ruler.mouseDown (mouseAt (ruler, 102, TimelineRuler::height - 4));  // under the start bracket, in the lower row
    CHECK (*marker == Catch::Approx (1020.0));
    ruler.mouseUp (mouseAt (ruler, 102, TimelineRuler::height - 4));

    // A right press on a bracket moves the playhead; it neither grabs nor clears the edge.
    ruler.mouseDown (mouseAt (ruler, 102, 5, rightButton));
    REQUIRE (seekTick.has_value());
    CHECK (*seekTick == Catch::Approx (1020.0));
    ruler.mouseDrag (mouseAt (ruler, 142, 5, rightButton));
    CHECK (*seekTick == Catch::Approx (1420.0));
    ruler.mouseUp (mouseAt (ruler, 142, 5, rightButton));
    CHECK (*marker == Catch::Approx (1020.0));
    CHECK (drags == 0);
    CHECK (clears == 0);
}

TEST_CASE ("TimelineRuler: the marker's triangle wins over a loop bracket at the same place", "[playback][ruler][loop][marker]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TimelineRuler ruler ([] (int x) { return (double) x * 10.0; });
    std::optional<double> marker = 1000.0;
    std::optional<LoopRange> loop = LoopRange { 1000.0, 3000.0 };   // start bracket 100..108, triangle 92..108
    setUpLoopRuler (ruler, marker, loop);
    int drags = 0;
    int clears = 0;
    ruler.onClearMarker = [&] { marker.reset(); };
    ruler.onSetMarker = [&] (double t) { marker = t; };
    ruler.onLoopEdgeDragged = [&] (bool, double) { ++drags; };
    ruler.onClearLoop = [&] { ++clears; };

    ruler.mouseDown (mouseAt (ruler, 102, 5));
    CHECK (! marker.has_value());
    ruler.mouseDrag (mouseAt (ruler, 142, 5));
    ruler.mouseUp (mouseAt (ruler, 142, 5));
    CHECK (! marker.has_value());
    CHECK (drags == 0);
    CHECK (clears == 0);
}

TEST_CASE ("TimelineRuler: the loop range is shaded through both rows and its brackets are drawn", "[playback][ruler][loop]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TimelineRuler ruler ([] (int x) { return (double) x * 10.0; });
    std::optional<double> marker;
    std::optional<LoopRange> loop;
    setUpLoopRuler (ruler, marker, loop);

    const auto without = renderComponent (ruler);
    loop = LoopRange { 1000.0, 3000.0 };   // x 100 .. 300
    const auto with = renderComponent (ruler);

    constexpr int clockRow = TimelineRuler::height - 4;
    const auto amber = juce::Colour (SongsmithColours::accentAmber);
    CHECK (with.getPixelAt (150, 4) != without.getPixelAt (150, 4));                   // top row, inside
    CHECK (with.getPixelAt (150, clockRow) != without.getPixelAt (150, clockRow));     // clock row, inside
    CHECK (with.getPixelAt (50, clockRow) == without.getPixelAt (50, clockRow));       // outside, left
    CHECK (with.getPixelAt (350, clockRow) == without.getPixelAt (350, clockRow));     // outside, right
    CHECK (with.getPixelAt (100, 6) == amber);   // the start bracket's upright, at its edge
    CHECK (with.getPixelAt (299, 6) == amber);   // the end bracket's upright, just inside its edge
    CHECK (with.getPixelAt (106, 0) == amber);   // the start bracket's top arm points right...
    CHECK (with.getPixelAt (293, 0) == amber);   // ...the end bracket's points left
    CHECK (with.getPixelAt (94, 0) != amber);
    CHECK (with.getPixelAt (305, 0) != amber);
}

// ---- PlayheadDirtyTracker: which x strips need repainting when the playhead moves ----
//
// The overlay reads the live transport position when it paints, but is only told
// about moves by a 30 Hz timer. A repaint in between (e.g. a volume slider step
// repainting its row) must not make the next notification think "nothing moved".

namespace
{
    bool containsStrip (const PlayheadDirtyTracker::Dirty& d, int x)
    {
        return std::find (d.xs.begin(), d.xs.end(), x) != d.xs.end();
    }

    const juce::Rectangle<int> farAway (300, 0, 100, 50);
}

TEST_CASE ("PlayheadDirtyTracker: a repaint between timer ticks does not hide the erase of the old line", "[playback][overlay][ghost]")
{
    PlayheadDirtyTracker t;
    constexpr int width = 500;
    constexpr int height = 50;
    t.onPositionChanged (100, width);
    t.onPaint (100, width, height, { 99, 0, 4, 50 });          // the line is drawn at 100

    // The transport moves on; an unrelated repaint of this overlay runs before the timer fires.
    t.onPaint (110, width, height, farAway);

    const auto dirty = t.onPositionChanged (110, width);
    CHECK (containsStrip (dirty, 100));                 // the line drawn at 100 must be erased
    CHECK (containsStrip (dirty, 110));
}

TEST_CASE ("PlayheadDirtyTracker: nothing to repaint while the playhead is still and drawn", "[playback][overlay][ghost]")
{
    PlayheadDirtyTracker t;
    constexpr int width = 500;
    constexpr int height = 50;
    t.onPositionChanged (100, width);
    t.onPaint (100, width, height, { 0, 0, width, 50 });
    CHECK (t.onPositionChanged (100, width).xs.empty());
}

TEST_CASE ("PlayheadDirtyTracker: a playhead outside the view never causes repeated repaints", "[playback][overlay][ghost]")
{
    PlayheadDirtyTracker t;
    constexpr int width = 500;
    constexpr int height = 50;
    t.onPositionChanged (-40, width);
    t.onPaint (-40, width, height, { 0, 0, width, 50 });
    CHECK (t.onPositionChanged (-40, width).xs.empty());
    t.onPositionChanged (900, width);
    t.onPaint (900, width, height, { 0, 0, width, 50 });
    CHECK (t.onPositionChanged (900, width).xs.empty());
}

TEST_CASE ("PlayheadDirtyTracker: a repaint that covers the old line without redrawing it counts as erased", "[playback][overlay][ghost]")
{
    PlayheadDirtyTracker t;
    constexpr int width = 500;
    constexpr int height = 50;
    t.onPositionChanged (100, width);
    t.onPaint (100, width, height, { 99, 0, 4, 50 });
    t.onPaint (110, width, height, { 90, 0, 40, 50 });          // covers 100 and 110: 100 is wiped, 110 is drawn
    const auto dirty = t.onPositionChanged (110, width);
    CHECK (! containsStrip (dirty, 100));
    CHECK (dirty.xs.empty());
}

TEST_CASE ("PlayheadDirtyTracker: a one-row repaint at the live x does not make the tracker forget the full line drawn earlier", "[playback][overlay][ghost]")
{
    PlayheadDirtyTracker t;
    constexpr int width = 500;
    constexpr int height = 500;
    t.onPositionChanged (100, width);
    t.onPaint (100, width, height, { 99, 0, 4, height });      // the timer's strip: the whole line is drawn at 100

    // The transport moves; dragging a volume slider repaints just its row (clip covers x 110, not the whole height).
    t.onPaint (110, width, height, { 0, 100, 200, 50 });

    const auto dirty = t.onPositionChanged (110, width);
    CHECK (containsStrip (dirty, 100));                 // the line still on screen at 100, outside that row, must be erased
    CHECK (containsStrip (dirty, 110));                 // and 110 was only drawn in that row
}

TEST_CASE ("PlayheadDirtyTracker: several one-row repaints at different x leave every drawn line to be erased", "[playback][overlay][ghost]")
{
    PlayheadDirtyTracker t;
    constexpr int width = 500;
    constexpr int height = 500;
    t.onPositionChanged (100, width);
    t.onPaint (100, width, height, { 99, 0, 4, height });
    t.onPaint (110, width, height, { 0, 100, 200, 50 });   // row 3
    t.onPaint (120, width, height, { 0, 200, 200, 50 });   // row 5, later
    t.onPaint (130, width, height, { 0, 300, 200, 50 });   // row 7, later still

    const auto dirty = t.onPositionChanged (130, width);
    CHECK (containsStrip (dirty, 100));
    CHECK (containsStrip (dirty, 110));
    CHECK (containsStrip (dirty, 120));
    CHECK (containsStrip (dirty, 130));
}

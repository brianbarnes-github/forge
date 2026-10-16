// Verifies PianoRollComponent's Preview-role note border colours: a Normal
// note borders with SongsmithColours::previewNoteBorder, and a
// WillFold/Dropped note borders with SongsmithColours::outOfRangeBorder —
// not the generic fill.brighter(0.4f) used by Role::Source.

#include "PlaybackTestSupport.h"
#include "UI/PianoRollComponent.h"
#include "UI/Theme/PianoRollPalette.h"
#include "UI/Theme/ThemePaint.h"
#include "UI/Playback/PlaybackController.h"
#include "UI/PreviewNoteDiff.h"
#include "UI/PreviewNoteSource.h"
#include "UI/SongDocument.h"
#include "UI/SongsmithColours.h"
#include "UI/SourceTrackNoteSource.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace lotro;

namespace lotro
{
    // Grants PianoRollComponent_tests.cpp access to the private paintCanvas()
    // and the editor-gesture handlers, which stay private on the class
    // itself.
    struct PianoRollComponentTestAccess
    {
        static void paintCanvas (const PianoRollComponent& c, juce::Graphics& g, juce::Rectangle<int> clip)
        {
            c.paintCanvas (g, clip);
        }

        static bool mouseDown (PianoRollComponent& c, juce::Point<int> pos, juce::ModifierKeys mods, bool dbl)
        {
            return c.handleEditorMouseDown (pos, mods, dbl);
        }
        static bool mouseDrag (PianoRollComponent& c, juce::Point<int> pos, juce::ModifierKeys mods = {})
        {
            return c.handleEditorMouseDrag (pos, mods);
        }
        static bool mouseUp (PianoRollComponent& c, juce::Point<int> pos) { return c.handleEditorMouseUp (pos); }

        // Exposes the private Canvas member itself so tests can drive its
        // real JUCE mouseDown/mouseDoubleClick/mouseDrag/mouseUp/keyPressed
        // overrides directly, instead of only the handleEditor* forwarding
        // methods those overrides call into.
        static PianoRollComponent::Canvas& canvas (PianoRollComponent& c) { return c.canvas; }

        static const PianoRollGeometry& geometry (const PianoRollComponent& c) { return c.geometry; }
        static juce::Viewport& viewport (PianoRollComponent& c) { return c.viewport; }
        static void setActivePitch (PianoRollComponent& c, int pitch) { c.setActivePitch (pitch); }
        static juce::Component& gutter (PianoRollComponent& c) { return c.gutter; }
        static void zoom (PianoRollComponent& c, float wheelDeltaY) { c.zoom (wheelDeltaY); }
        // A wheel notch at `componentPos` (this component's own coordinates, i.e.
        // unscrolled), delivered through the real Canvas::mouseWheelMove the way
        // JUCE would: the event carries canvas-space coordinates.
        static void wheelAt (PianoRollComponent& c, float deltaY, juce::ModifierKeys mods, juce::Point<int> componentPos)
        {
            juce::MouseWheelDetails details {};
            details.deltaY = deltaY;
            auto& canvas = c.canvas;
            const auto pos = (componentPos + c.viewport.getViewPosition()).toFloat();
            canvas.mouseWheelMove (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, mods,
                                                     0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &canvas, &canvas,
                                                     juce::Time::getCurrentTime(), pos,
                                                     juce::Time::getCurrentTime(), 1, false),
                                   details);
        }
        // Over the note canvas (right of the gutter).
        static void wheel (PianoRollComponent& c, float deltaY, juce::ModifierKeys mods)
        {
            wheelAt (c, deltaY, mods, { 200, 100 });
        }
        static bool handleWheel (PianoRollComponent& c, PianoRollComponent::WheelRegion region, float deltaY,
                                 juce::ModifierKeys mods, int pointerY)
        {
            juce::MouseWheelDetails details {};
            details.deltaY = deltaY;
            return c.handleWheel (region, mods, details, pointerY);
        }
        static SourceRollEditor& editor (PianoRollComponent& c) { return *c.sourceEditor; }
        static void paintGutter (const PianoRollComponent& c, juce::Graphics& g) { c.paintGutter (g); }
        static int hoveredPitch (const PianoRollComponent& c) { return c.hoveredPitch; }
        static juce::Component* playhead (PianoRollComponent& c) { return c.playhead.get(); }
        static MarkerOverlay* markerOverlay (PianoRollComponent& c) { return c.markerOverlay.get(); }
        static LoopRangeOverlay* loopOverlay (PianoRollComponent& c) { return c.loopOverlay.get(); }
        static int playheadX (const PianoRollComponent& c) { return c.playhead->currentX(); }
        static int playheadRepaints (const PianoRollComponent& c) { return c.playheadRepaintCount; }
        static bool timelineFitted (const PianoRollComponent& c) { return c.timelineFitted; }
        static void follow (PianoRollComponent& c, bool playing) { c.followPlayhead (playing); }
    };
}

namespace
{
    using Access = PianoRollComponentTestAccess;
    constexpr int viewportWidth = 400;
    constexpr int viewportHeight = 200;
    constexpr int ticksPerQuarter = 480;

    // Builds a real juce::MouseEvent the way JUCE's own event dispatch would
    // (obtaining a MouseInputSource from the Desktop singleton), so tests can
    // call a Component's real mouseDown/mouseDrag/mouseUp/mouseDoubleClick
    // overrides directly rather than only a private forwarding method.
    juce::MouseEvent makeMouseEvent (juce::Component& comp, juce::Point<int> pos, juce::Point<int> mouseDownPos,
                                      int numberOfClicks, bool mouseWasDragged)
    {
        auto* source = juce::Desktop::getInstance().getMouseSource (0);
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (*source, pos.toFloat(), juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                  &comp, &comp, now, mouseDownPos.toFloat(), now, numberOfClicks, mouseWasDragged);
    }
}

TEST_CASE ("PianoRollComponent: Preview-role notes border with the state-specific preview colours", "[piano-roll]")
{
    // PianoRollComponent is a real juce::Component (owns a Viewport/Canvas);
    // constructing one lazily creates the Desktop/LookAndFeel singletons,
    // which need a matching teardown or they report as leaks (same
    // rationale as TrackListComponent_tests.cpp's real-import test).
    juce::ScopedJuceInitialiser_GUI juceInit;

    PreviewNote normalNote;
    normalNote.prePitch = 60;
    normalNote.startTick = 0;
    normalNote.durationTicks = ticksPerQuarter;
    normalNote.state = NoteState::Normal;

    PreviewNote droppedNote;
    droppedNote.prePitch = 64;
    droppedNote.startTick = ticksPerQuarter;
    droppedNote.durationTicks = ticksPerQuarter;
    droppedNote.state = NoteState::Dropped;

    PreviewNoteSource source ({ normalNote, droppedNote });

    PianoRollComponent roll (PianoRollComponent::Role::Preview);
    roll.setBounds (0, 0, viewportWidth, viewportHeight);
    roll.setNoteSource (&source, ticksPerQuarter, {});

    const auto geometry = PianoRollGeometry::fitToContent (source.getTickRange(), source.getPitchRange(),
                                                             ticksPerQuarter, viewportWidth, viewportHeight);
    const auto normalBounds = geometry.noteBounds (source.getNote (0));
    const auto droppedBounds = geometry.noteBounds (source.getNote (1));

    juce::Image image (juce::Image::ARGB, viewportWidth, viewportHeight, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    Access::paintCanvas (roll, g, { 0, 0, viewportWidth, viewportHeight });

    // drawNotes runs last in paintCanvas, so the top-border pixel (drawn
    // after the fill, at rect.y) is exactly the border colour, no blending
    // with row bands/gridlines painted earlier.
    const auto normalBorderPixel = image.getPixelAt (normalBounds.x + normalBounds.width / 2, normalBounds.y);
    const auto droppedBorderPixel = image.getPixelAt (droppedBounds.x + droppedBounds.width / 2, droppedBounds.y);

    CHECK (normalBorderPixel == juce::Colour (SongsmithColours::previewNoteBorder));
    CHECK (droppedBorderPixel == juce::Colour (SongsmithColours::outOfRangeBorder));
}

TEST_CASE ("PianoRollComponent: an upward-folding note's ghost and range band render on-canvas, not off-canvas/flooded (C1)", "[piano-roll]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // Reviewer's exact repro: pitch 20 folds to 44 (inside LuteOfAges's
    // range), with setPreviewRangeBand({36, 73}) called after
    // setNoteSource. Before the fix, topPitch was derived from prePitch
    // alone (20), so the ghost (drawn at postPitch 44) and the whole range
    // band landed off the top of the canvas (negative y), and the
    // below-range wash flooded the entire visible clip instead.
    PreviewNote foldingNote;
    foldingNote.prePitch = 20;
    foldingNote.postPitch = 44;
    foldingNote.startTick = 0;
    foldingNote.durationTicks = ticksPerQuarter;
    foldingNote.state = NoteState::WillFold;

    PreviewNoteSource source ({ foldingNote });

    PianoRollComponent roll (PianoRollComponent::Role::Preview);
    roll.setBounds (0, 0, viewportWidth, viewportHeight);
    roll.setNoteSource (&source, ticksPerQuarter, {});
    roll.setPreviewRangeBand ({ 36, 73 });

    // Independently derived by hand, not by calling the component's own
    // fitToContent: the effective pitch extent must cover both the note's
    // post-fold pitch (44) and the band (36..73), so topPitch = 72.
    const int expectedTopPitch = 72;
    const auto sourceGeometry = PianoRollGeometry::fitToContent (source.getTickRange(), source.getPitchRange(),
                                                                   ticksPerQuarter, viewportWidth, viewportHeight);
    const auto noteBounds = sourceGeometry.noteBounds (source.getNote (0));
    const int rowHeight = sourceGeometry.getRowHeight();
    const int sampleX = noteBounds.x + noteBounds.width / 2;
    const int ghostY = (expectedTopPitch - 44) * rowHeight;
    const int insideBandY = (expectedTopPitch - 50) * rowHeight; // pitch 50: inside the band (36..73)
    const int belowBandY = (expectedTopPitch - 30) * rowHeight;  // pitch 30: below the band's floor (36)

    const int imageHeight = (expectedTopPitch - 20 + 2) * rowHeight; // covers the whole effective pitch span
    juce::Image image (juce::Image::ARGB, viewportWidth, imageHeight, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    Access::paintCanvas (roll, g, { 0, 0, viewportWidth, imageHeight });

    // The ghost's top-edge outline is the last thing drawn at this pixel;
    // deriving the expected colour from the pixel one row beneath (which
    // has no ghost stroke, only the row band + range band tint already
    // painted there) and blending the ghost colour onto it via JUCE's own
    // Graphics compositing proves the ghost was actually painted on-canvas,
    // without hard-coding PianoRollComponent's private fill constants.
    const auto underlyingBeneathGhost = image.getPixelAt (sampleX, ghostY + 1);
    juce::Image reference (juce::Image::ARGB, 1, 1, true, juce::SoftwareImageType());
    {
        juce::Graphics rg (reference);
        rg.fillAll (underlyingBeneathGhost);
        rg.setColour (juce::Colour (SongsmithColours::accentCopper).withAlpha (0.7f));
        rg.fillRect (0, 0, 1, 1);
    }
    CHECK (image.getPixelAt (sampleX, ghostY) == reference.getPixelAt (0, 0));

    // The band's interior tint and the below-range wash must be visibly
    // distinct colours on-canvas — under the pre-fix bug, both coordinates
    // fell inside one giant "clip.withTop(negative bandBottom)" fill, so
    // this pixel pair would have been identical (the whole clip flooded
    // with the same wash).
    CHECK (image.getPixelAt (sampleX, insideBandY) != image.getPixelAt (sampleX, belowBandY));
}

TEST_CASE ("PianoRollComponent: a WillFold note's ghost still paints when a partial repaint clips out the solid note but not the ghost (I1)", "[piano-roll]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    PreviewNote foldingNote;
    foldingNote.prePitch = 80;
    foldingNote.postPitch = 40;
    foldingNote.startTick = 0;
    foldingNote.durationTicks = ticksPerQuarter;
    foldingNote.state = NoteState::WillFold;

    PreviewNoteSource source ({ foldingNote });

    PianoRollComponent roll (PianoRollComponent::Role::Preview);
    roll.setBounds (0, 0, viewportWidth, viewportHeight);
    roll.setNoteSource (&source, ticksPerQuarter, {});

    const auto geometry = PianoRollGeometry::fitToContent (source.getTickRange(), source.getPitchRange(),
                                                             ticksPerQuarter, viewportWidth, viewportHeight);
    const auto noteBounds = geometry.noteBounds (source.getNote (0));
    const int rowHeight = geometry.getRowHeight();
    const int ghostY = geometry.yForPitch (40);
    const int sampleX = noteBounds.x + noteBounds.width / 2;

    // A clip strip covering only the ghost's row — far from the solid
    // note's own row (drawn at prePitch 80) — simulating a Viewport scroll
    // repaint that exposes only a newly-visible strip.
    const juce::Rectangle<int> partialClip (0, ghostY, viewportWidth, rowHeight);

    juce::Image image (juce::Image::ARGB, viewportWidth, ghostY + rowHeight + 1, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    Access::paintCanvas (roll, g, partialClip);

    // Same independent-composition approach as the C1 test above: the
    // expected pixel is the row's own underlying colour (sampled one row
    // beneath, where no ghost stroke lands) with the ghost colour blended
    // on top via JUCE's own compositing — proving the ghost was actually
    // painted despite the clip excluding the note's solid rect.
    const auto underlyingBeneathGhost = image.getPixelAt (sampleX, ghostY + 1);
    juce::Image reference (juce::Image::ARGB, 1, 1, true, juce::SoftwareImageType());
    {
        juce::Graphics rg (reference);
        rg.fillAll (underlyingBeneathGhost);
        rg.setColour (juce::Colour (SongsmithColours::accentCopper).withAlpha (0.7f));
        rg.fillRect (0, 0, 1, 1);
    }

    CHECK (image.getPixelAt (sampleX, ghostY) == reference.getPixelAt (0, 0));
}

TEST_CASE ("PianoRollComponent: Canvas's real JUCE mouseDrag/mouseUp reach SourceRollEditor and mutate the real SongDocument end to end", "[piano-roll]")
{
    // Drives Canvas's own mouseDrag/mouseUp overrides with a real
    // juce::MouseEvent (obtained via Desktop::getMouseSource, as real JUCE
    // event dispatch would build one), so e.getPosition()'s coordinate
    // handling for those two entry points is actually exercised, not just
    // the private handleEditorMouseDrag/Up forwarding methods they call
    // into. The initial press still goes through handleEditorMouseDown
    // (not Canvas::mouseDown) deliberately: Canvas::mouseDown's first line
    // is grabKeyboardFocus(), which JUCE unconditionally asserts on
    // (`isShowing() || isOnDesktop()`) for a component that was never
    // added to a real window -- true of every Component in this headless
    // test binary, and this project's convention is to never add a real
    // GUI window from a test/subagent (see CLAUDE.md's "Do not launch the
    // GUI from subagents"). That one line of Canvas::mouseDown stays
    // covered only by inspection, not a real-event test; see the comment
    // on it in PianoRollComponent.cpp.
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Track A", 0xFF0000, 0, 1);
    juce::ValueTree note (SongIDs::NOTE);
    note.setProperty (SongIDs::pitch, 60, nullptr);
    note.setProperty (SongIDs::startTick, 0, nullptr);
    note.setProperty (SongIDs::durationTicks, ticksPerQuarter, nullptr);
    SongDocument::getNotesNode (track).addChild (note, -1, nullptr);

    SourceTrackNoteSource source (track);

    PianoRollComponent roll (PianoRollComponent::Role::Source, &doc);
    roll.setBounds (0, 0, viewportWidth, viewportHeight);
    roll.setNoteSource (&source, ticksPerQuarter, {});
    roll.setEditableTrack (track);

    const auto geometry = Access::geometry (roll);
    const auto bounds = geometry.noteBounds (source.getNote (0));
    const juce::Point<int> clickPos (bounds.x + bounds.width / 2, bounds.y + bounds.height / 2);
    const juce::Point<int> dragPos = clickPos.translated (40, 0);

    auto& canvas = Access::canvas (roll);
    REQUIRE (Access::mouseDown (roll, clickPos, {}, false));
    canvas.mouseDrag (makeMouseEvent (canvas, dragPos, clickPos, 1, true));
    canvas.mouseUp (makeMouseEvent (canvas, dragPos, clickPos, 1, true));

    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

    const int expectedDeltaTick = geometry.tickForX (dragPos.x) - geometry.tickForX (clickPos.x);
    CHECK ((int) SongDocument::getNotesNode (track).getChild (0).getProperty (SongIDs::startTick) == expectedDeltaTick);
    CHECK (doc.canUndo());

    doc.undo();
    CHECK ((int) SongDocument::getNotesNode (track).getChild (0).getProperty (SongIDs::startTick) == 0);
}

TEST_CASE ("PianoRollComponent: Canvas's real JUCE mouseDoubleClick and keyPressed reach SourceRollEditor end to end", "[piano-roll]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Track A", 0xFF0000, 0, 1);
    juce::ValueTree note (SongIDs::NOTE);
    note.setProperty (SongIDs::pitch, 60, nullptr);
    note.setProperty (SongIDs::startTick, 0, nullptr);
    note.setProperty (SongIDs::durationTicks, ticksPerQuarter, nullptr);
    SongDocument::getNotesNode (track).addChild (note, -1, nullptr);

    SourceTrackNoteSource source (track);

    PianoRollComponent roll (PianoRollComponent::Role::Source, &doc);
    roll.setBounds (0, 0, viewportWidth, viewportHeight);
    roll.setNoteSource (&source, ticksPerQuarter, {});
    roll.setEditableTrack (track);

    const auto geometry = Access::geometry (roll);
    const juce::Point<int> emptyCell (geometry.xForTick (ticksPerQuarter * 3), geometry.yForPitch (72));

    auto& canvas = Access::canvas (roll);
    REQUIRE (SongDocument::getNotesNode (track).getNumChildren() == 1);
    canvas.mouseDoubleClick (makeMouseEvent (canvas, emptyCell, emptyCell, 2, false));
    REQUIRE (SongDocument::getNotesNode (track).getNumChildren() == 2); // createNoteAt also selects the new note

    REQUIRE (canvas.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));
    CHECK (SongDocument::getNotesNode (track).getNumChildren() == 1);
}

TEST_CASE ("PianoRollComponent: a selected source-role note paints with the selection highlight border", "[piano-roll]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Track A", 0xFF0000, 0, 1);
    juce::ValueTree note (SongIDs::NOTE);
    note.setProperty (SongIDs::pitch, 60, nullptr);
    note.setProperty (SongIDs::startTick, 0, nullptr);
    note.setProperty (SongIDs::durationTicks, ticksPerQuarter, nullptr);
    SongDocument::getNotesNode (track).addChild (note, -1, nullptr);

    SourceTrackNoteSource source (track);

    PianoRollComponent roll (PianoRollComponent::Role::Source, &doc);
    roll.setBounds (0, 0, viewportWidth, viewportHeight);
    roll.setNoteSource (&source, ticksPerQuarter, {});
    roll.setEditableTrack (track);

    const auto geometry = Access::geometry (roll);
    const auto bounds = geometry.noteBounds (source.getNote (0));
    const juce::Point<int> centre (bounds.x + bounds.width / 2, bounds.y + bounds.height / 2);

    Access::mouseDown (roll, centre, {}, false);
    Access::mouseUp (roll, centre);

    // Tall enough to hold the note's row: a Source roll spans all 128 MIDI
    // pitches, so pitch 60 sits well below the first viewport-height of canvas.
    const int imageHeight = bounds.y + bounds.height + geometry.getRowHeight();
    juce::Image image (juce::Image::ARGB, viewportWidth, imageHeight, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    Access::paintCanvas (roll, g, { 0, 0, viewportWidth, imageHeight });

    const auto topBorderPixel = image.getPixelAt (bounds.x + bounds.width / 2, bounds.y);
    CHECK (topBorderPixel == juce::Colour (SongsmithColours::selectionHighlight));
}

TEST_CASE ("PianoRollComponent: ghost tracks render translucently and only for Role::Source", "[piano-roll][ghost-tracks]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Ghost Track", (int) 0xFFAABBCC, 0, 0);
    juce::ValueTree note (SongIDs::NOTE);
    note.setProperty (SongIDs::pitch, 60, nullptr);
    note.setProperty (SongIDs::startTick, 0, nullptr);
    note.setProperty (SongIDs::durationTicks, 480, nullptr);
    SongDocument::getNotesNode (track).appendChild (note, nullptr);

    // setNoteSource is never called on this roll, so PianoRollComponent's
    // own `geometry` member stays default-constructed -- mirroring that
    // here (rather than calling fitToContent) predicts the ghost rect's
    // pixel location without hardcoding PianoRollGeometry's private layout
    // constants.
    PianoRollGeometry defaultGeometry;
    const int ghostX = defaultGeometry.xForTick (0);
    const int ghostWidth = juce::jmax (1, defaultGeometry.xForTick (480) - ghostX);
    const int ghostY = defaultGeometry.yForPitch (60);
    const int rowHeight = defaultGeometry.getRowHeight();
    const int sampleX = ghostX + ghostWidth / 2; // clear of the tick-0 vertical gridline at ghostX itself
    const int imageWidth = juce::jmax (viewportWidth, ghostX + ghostWidth + 1);
    const int imageHeight = ghostY + rowHeight * 2;

    PianoRollComponent roll (PianoRollComponent::Role::Source, &doc);
    roll.setBounds (0, 0, imageWidth, imageHeight);
    roll.setGhostTracks ({ track });

    juce::Image image (juce::Image::ARGB, imageWidth, imageHeight, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    Access::paintCanvas (roll, g, { 0, 0, imageWidth, imageHeight });

    // The row immediately below the ghost's own row is not touched by its
    // fillRect (whose height is exactly one row), and -- like the ghost's
    // own row -- is a white-key row band (pitch 60 is C, pitch 59 is B), so
    // its rendered colour is exactly the ghost's own row's underlying
    // background before the ghost was painted on top. Compositing
    // accentCopper@0.25 onto that colour and comparing proves the ghost
    // renders as a translucent overlay, not a fully opaque note fill.
    const auto underlyingBeneathGhost = image.getPixelAt (sampleX, ghostY + rowHeight);
    juce::Image reference (juce::Image::ARGB, 1, 1, true, juce::SoftwareImageType());
    {
        juce::Graphics rg (reference);
        rg.fillAll (underlyingBeneathGhost);
        rg.setColour (juce::Colour (SongsmithColours::accentCopper).withAlpha (0.25f));
        rg.fillRect (0, 0, 1, 1);
    }

    CHECK (image.getPixelAt (sampleX, ghostY) == reference.getPixelAt (0, 0));
}

TEST_CASE ("PianoRollComponent: Preview role ignores setGhostTracks", "[piano-roll][ghost-tracks]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Ghost Track", (int) 0xFFAABBCC, 0, 0);
    juce::ValueTree note (SongIDs::NOTE);
    note.setProperty (SongIDs::pitch, 60, nullptr);
    note.setProperty (SongIDs::startTick, 0, nullptr);
    note.setProperty (SongIDs::durationTicks, 480, nullptr);
    SongDocument::getNotesNode (track).appendChild (note, nullptr);

    PianoRollGeometry defaultGeometry;
    const int ghostX = defaultGeometry.xForTick (0);
    const int ghostWidth = juce::jmax (1, defaultGeometry.xForTick (480) - ghostX);
    const int ghostY = defaultGeometry.yForPitch (60);
    const int rowHeight = defaultGeometry.getRowHeight();
    const int sampleX = ghostX + ghostWidth / 2;
    const int imageWidth = juce::jmax (viewportWidth, ghostX + ghostWidth + 1);
    const int imageHeight = ghostY + rowHeight * 2;

    PianoRollComponent roll (PianoRollComponent::Role::Preview);
    roll.setBounds (0, 0, imageWidth, imageHeight);

    // Must not crash even though Role::Preview never constructs a
    // SourceRollEditor and has no editable track concept.
    CHECK_NOTHROW (roll.setGhostTracks ({ track }));

    juce::Image image (juce::Image::ARGB, imageWidth, imageHeight, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    CHECK_NOTHROW (Access::paintCanvas (roll, g, { 0, 0, imageWidth, imageHeight }));

    // Same white-key-adjacent-row background argument as the Source-role
    // test above: if the Role::Preview gate in drawGhostTracks were broken
    // (inverted or removed), the ghost's own row would be tinted
    // accentCopper while the untouched row beneath it would not -- so the
    // two must be pixel-identical when the gate correctly suppresses ghost
    // rendering for this role.
    const auto ghostPixel = image.getPixelAt (sampleX, ghostY);
    const auto belowPixel = image.getPixelAt (sampleX, ghostY + rowHeight);
    CHECK (ghostPixel == belowPixel);
}

namespace
{
    // A Source-role roll over one MIDI_TRACK, sized and pointed at the track
    // the way TrackEditorPanel does it (bounds first, then setNoteSource).
    struct SourceRollFixture
    {
        explicit SourceRollFixture (std::vector<int> pitches, int width = viewportWidth, int height = viewportHeight)
        {
            track = doc.addTrack ("Track A", 0xFF0000, 0, 1);
            int startTick = 0;
            for (int pitch : pitches)
            {
                juce::ValueTree note (SongIDs::NOTE);
                note.setProperty (SongIDs::pitch, pitch, nullptr);
                note.setProperty (SongIDs::startTick, startTick, nullptr);
                note.setProperty (SongIDs::durationTicks, ticksPerQuarter, nullptr);
                SongDocument::getNotesNode (track).addChild (note, -1, nullptr);
                startTick += ticksPerQuarter;
            }
            source = std::make_unique<SourceTrackNoteSource> (track);
            roll.setBounds (0, 0, width, height);
            roll.setNoteSource (source.get(), ticksPerQuarter, {});
            roll.setEditableTrack (track);
        }

        // Canvas-space y at the middle of the viewport's visible area.
        int visibleCentreY()
        {
            auto& viewport = Access::viewport (roll);
            return viewport.getViewPositionY() + viewport.getMaximumVisibleHeight() / 2;
        }

        int rowCentreY (int pitch) const
        {
            const auto& geometry = Access::geometry (roll);
            return geometry.yForPitch (pitch) + geometry.getRowHeight() / 2;
        }

        SongDocument doc;
        juce::ValueTree track;
        std::unique_ptr<SourceTrackNoteSource> source;
        PianoRollComponent roll { PianoRollComponent::Role::Source, &doc };
    };
}

TEST_CASE ("PianoRollComponent: a Source roll lays out every MIDI pitch 0..127, whatever the track's own range", "[piano-roll][full-range]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 64 });

    const auto& geometry = Access::geometry (fixture.roll);
    auto& canvas = Access::canvas (fixture.roll);

    CHECK (geometry.getTopPitch() == 127);
    CHECK (canvas.getHeight() == 128 * geometry.getRowHeight());
    CHECK (geometry.yForPitch (127) == 0);
    CHECK (geometry.yForPitch (0) + geometry.getRowHeight() == canvas.getHeight());
    CHECK (Access::viewport (fixture.roll).getVerticalScrollBar().isVisible());
}

TEST_CASE ("PianoRollComponent: a Source roll can draw a new note at the extremes of the MIDI range", "[piano-roll][full-range]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60 });

    const auto& geometry = Access::geometry (fixture.roll);
    const int x = geometry.xForTick (ticksPerQuarter / 2);

    REQUIRE (Access::mouseDown (fixture.roll, { x, geometry.yForPitch (127) + 1 }, {}, true));
    REQUIRE (Access::mouseDown (fixture.roll, { x, geometry.yForPitch (0) + 1 }, {}, true));

    REQUIRE (SongDocument::getNotesNode (fixture.track).getNumChildren() == 3);
    CHECK ((int) SongDocument::getNotesNode (fixture.track).getChild (1).getProperty (SongIDs::pitch) == 127);
    CHECK ((int) SongDocument::getNotesNode (fixture.track).getChild (2).getProperty (SongIDs::pitch) == 0);
}

TEST_CASE ("PianoRollComponent: a Source roll opens scrolled so the track's notes are centred vertically", "[piano-roll][full-range]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SECTION ("notes around pitch 40")
    {
        SourceRollFixture fixture ({ 36, 44 });
        CHECK (std::abs (fixture.visibleCentreY() - fixture.rowCentreY (40)) <= Access::geometry (fixture.roll).getRowHeight());
    }

    SECTION ("an empty track centres on middle C")
    {
        SourceRollFixture fixture ({});
        CHECK (std::abs (fixture.visibleCentreY() - fixture.rowCentreY (60)) <= Access::geometry (fixture.roll).getRowHeight());
    }

    SECTION ("re-pointing at another track re-centres on that track")
    {
        SourceRollFixture fixture ({ 100 });
        auto other = fixture.doc.addTrack ("Track B", (int) 0xFF00FF00, 1, 0);
        juce::ValueTree note (SongIDs::NOTE);
        note.setProperty (SongIDs::pitch, 30, nullptr);
        note.setProperty (SongIDs::startTick, 0, nullptr);
        note.setProperty (SongIDs::durationTicks, ticksPerQuarter, nullptr);
        SongDocument::getNotesNode (other).addChild (note, -1, nullptr);
        SourceTrackNoteSource otherSource (other);

        fixture.roll.setNoteSource (&otherSource, ticksPerQuarter, {});
        CHECK (std::abs (fixture.visibleCentreY() - fixture.rowCentreY (30)) <= Access::geometry (fixture.roll).getRowHeight());
        fixture.roll.setNoteSource (nullptr, ticksPerQuarter, {});
    }
}

TEST_CASE ("PianoRollComponent: a fitted Source roll shows no horizontal scroll bar until zoomed in", "[piano-roll][full-range]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 62, 64, 65 });

    auto& viewport = Access::viewport (fixture.roll);
    auto& canvas = Access::canvas (fixture.roll);

    // The always-present vertical bar narrows the visible width -- the fit
    // must account for it, or the canvas overhangs by the bar's thickness.
    CHECK (canvas.getWidth() == viewport.getMaximumVisibleWidth());
    CHECK_FALSE (viewport.getHorizontalScrollBar().isVisible());

    Access::zoom (fixture.roll, 1.0f);
    CHECK (canvas.getWidth() > viewport.getMaximumVisibleWidth());
    CHECK (viewport.getHorizontalScrollBar().isVisible());
}

TEST_CASE ("PianoRollComponent: a Source roll refits the song to the width on resize, until the user zooms", "[piano-roll][full-range]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 62, 64, 65 });

    auto& viewport = Access::viewport (fixture.roll);
    const int songEndTick = 4 * ticksPerQuarter;

    fixture.roll.setBounds (0, 0, viewportWidth * 2, viewportHeight * 2); // e.g. the window was maximised
    CHECK (Access::geometry (fixture.roll).xForTick (songEndTick) == viewport.getMaximumVisibleWidth());
    CHECK_FALSE (viewport.getHorizontalScrollBar().isVisible());

    Access::zoom (fixture.roll, 1.0f);
    const double zoomed = Access::geometry (fixture.roll).getPixelsPerQuarterNote();

    fixture.roll.setBounds (0, 0, viewportWidth, viewportHeight);
    CHECK_THAT (Access::geometry (fixture.roll).getPixelsPerQuarterNote(), Catch::Matchers::WithinAbs (zoomed, 1e-9));
    CHECK (viewport.getHorizontalScrollBar().isVisible());
}

TEST_CASE ("PianoRollComponent: the keyboard gutter stops above the horizontal scroll bar once zooming shows it", "[piano-roll][full-range]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 62, 64, 65 });

    auto& viewport = Access::viewport (fixture.roll);
    Access::zoom (fixture.roll, 1.0f);

    REQUIRE (viewport.getHorizontalScrollBar().isVisible());
    CHECK (Access::gutter (fixture.roll).getBottom() <= viewport.getHorizontalScrollBar().getY());
}

namespace
{
    // Paints the keyboard gutter of `roll` into an image the gutter's size.
    juce::Image paintKeyboard (PianoRollComponent& roll)
    {
        auto& gutter = Access::gutter (roll);
        juce::Image image (juce::Image::ARGB, gutter.getWidth(), gutter.getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g (image);
        Access::paintGutter (roll, g);
        return image;
    }

    // Gutter-space y of `pitch`'s row top, allowing for vertical scroll.
    int keyTopY (PianoRollComponent& roll, int pitch)
    {
        return Access::geometry (roll).yForPitch (pitch) - Access::viewport (roll).getViewPositionY();
    }

    bool isLight (juce::Colour c) { return c.getBrightness() > 0.8f; }
    bool isDark (juce::Colour c)  { return c.getBrightness() < 0.2f; }
}

TEST_CASE ("PianoRollComponent: the keyboard gutter draws white keys full width and shorter black keys for sharps/flats", "[piano-roll][keyboard]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 64 });
    const auto image = paintKeyboard (fixture.roll);
    const int width = Access::gutter (fixture.roll).getWidth();
    const int rowMiddle = Access::geometry (fixture.roll).getRowHeight() / 2;
    const int leftX = 3;
    const int rightX = width - 4;

    // D4 (62): a white key, light across its whole width.
    CHECK (isLight (image.getPixelAt (leftX, keyTopY (fixture.roll, 62) + 2)));
    CHECK (isLight (image.getPixelAt (rightX, keyTopY (fixture.roll, 62) + 2)));

    // C#4 (61) and A#3 (58): black keys on the left, with the white keys
    // they sit between still showing to their right, like a real keyboard.
    for (int blackPitch : { 61, 58 })
    {
        CHECK (isDark (image.getPixelAt (leftX, keyTopY (fixture.roll, blackPitch) + rowMiddle)));
        CHECK (isLight (image.getPixelAt (rightX, keyTopY (fixture.roll, blackPitch) + 2)));
    }
}

TEST_CASE ("PianoRollComponent: adjacent white keys with no black key between them (B/C, E/F) get a full-width divider", "[piano-roll][keyboard]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 64 });
    const auto image = paintKeyboard (fixture.roll);
    const int leftX = 3;

    // Top edge of B3 (59) is where it meets C4; top edge of E4 (64) is where it meets F4.
    for (int lowerWhite : { 59, 64 })
    {
        const auto divider = image.getPixelAt (leftX, keyTopY (fixture.roll, lowerWhite));
        const auto keyFace = image.getPixelAt (leftX, keyTopY (fixture.roll, lowerWhite) + 3);
        CHECK (divider.getBrightness() < keyFace.getBrightness());
    }
}

namespace
{
    // Canvas-space point in the middle of `pitch`'s row.
    juce::Point<int> canvasRowPoint (PianoRollComponent& roll, int x, int pitch)
    {
        const auto& geometry = Access::geometry (roll);
        return { x, geometry.yForPitch (pitch) + geometry.getRowHeight() / 2 };
    }

    // Number of pixels in one key's row (gutter space) that differ from the
    // key face colour sampled at its left edge -- i.e. how much text/marking
    // is drawn on it.
    int markedPixelsInKeyRow (const juce::Image& image, PianoRollComponent& roll, int pitch)
    {
        const int top = keyTopY (roll, pitch);
        const int rowHeight = Access::geometry (roll).getRowHeight();
        const int blackKeyRight = juce::roundToInt ((float) image.getWidth() * 0.6f);
        int marked = 0;
        for (int x = blackKeyRight + 1; x < image.getWidth() - 2; ++x)
        {
            // White keys shade horizontally, so each column has its own face colour.
            const auto face = image.getPixelAt (x, top + 1);
            for (int y = top + 2; y < top + rowHeight - 2; ++y)
                if (image.getPixelAt (x, y) != face)
                    ++marked;
        }
        return marked;
    }
}

TEST_CASE ("PianoRollComponent: moving the mouse over a row hovers that row's pitch; leaving clears it", "[piano-roll][keyboard][hover]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 64 });
    auto& canvas = Access::canvas (fixture.roll);

    CHECK (Access::hoveredPitch (fixture.roll) == -1);

    const auto overKeyboard = canvasRowPoint (fixture.roll, 5, 66);
    canvas.mouseMove (makeMouseEvent (canvas, overKeyboard, overKeyboard, 0, false));
    CHECK (Access::hoveredPitch (fixture.roll) == 66);

    const auto overNotes = canvasRowPoint (fixture.roll, 200, 63);
    canvas.mouseMove (makeMouseEvent (canvas, overNotes, overNotes, 0, false));
    CHECK (Access::hoveredPitch (fixture.roll) == 63);

    canvas.mouseExit (makeMouseEvent (canvas, overNotes, overNotes, 0, false));
    CHECK (Access::hoveredPitch (fixture.roll) == -1);
}

TEST_CASE ("PianoRollComponent: dragging keeps the hovered pitch following the mouse", "[piano-roll][keyboard][hover]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 64 });
    auto& canvas = Access::canvas (fixture.roll);

    const auto from = canvasRowPoint (fixture.roll, 200, 60);
    const auto to = canvasRowPoint (fixture.roll, 200, 71);
    canvas.mouseDrag (makeMouseEvent (canvas, to, from, 1, true));
    CHECK (Access::hoveredPitch (fixture.roll) == 71);
}

TEST_CASE ("PianoRollComponent: the hovered key is tinted and labelled with its note name", "[piano-roll][keyboard][hover]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 64 });
    auto& canvas = Access::canvas (fixture.roll);
    const int leftX = 3;
    const int rowMiddle = Access::geometry (fixture.roll).getRowHeight() / 2;

    const auto unhovered = paintKeyboard (fixture.roll);
    const auto hover = canvasRowPoint (fixture.roll, 5, 62); // D4, a white key
    canvas.mouseMove (makeMouseEvent (canvas, hover, hover, 0, false));
    const auto hovered = paintKeyboard (fixture.roll);

    const auto face = hovered.getPixelAt (leftX, keyTopY (fixture.roll, 62) + rowMiddle);
    CHECK (face != unhovered.getPixelAt (leftX, keyTopY (fixture.roll, 62) + rowMiddle));
    CHECK (isLight (face)); // a subtle tint, still reads as a white key

    CHECK (markedPixelsInKeyRow (unhovered, fixture.roll, 62) == 0);
    CHECK (markedPixelsInKeyRow (hovered, fixture.roll, 62) > 0);

    // Other keys are untouched.
    CHECK (hovered.getPixelAt (leftX, keyTopY (fixture.roll, 64) + rowMiddle)
           == unhovered.getPixelAt (leftX, keyTopY (fixture.roll, 64) + rowMiddle));
}

TEST_CASE ("PianoRollComponent: a hovered black key is labelled in the white-key area to its right", "[piano-roll][keyboard][hover]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 64 });
    auto& canvas = Access::canvas (fixture.roll);

    const auto unhovered = paintKeyboard (fixture.roll);
    const auto hover = canvasRowPoint (fixture.roll, 5, 66); // F#4
    canvas.mouseMove (makeMouseEvent (canvas, hover, hover, 0, false));
    const auto hovered = paintKeyboard (fixture.roll);

    // The divider behind the black key already marks a few pixels; the label adds more.
    CHECK (markedPixelsInKeyRow (hovered, fixture.roll, 66) > markedPixelsInKeyRow (unhovered, fixture.roll, 66));
    CHECK (isDark (hovered.getPixelAt (3, keyTopY (fixture.roll, 66) + Access::geometry (fixture.roll).getRowHeight() / 2)));
}

TEST_CASE ("PianoRollComponent: hovering B or E keeps the divider to the white key above it", "[piano-roll][keyboard][hover]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 64 });
    auto& canvas = Access::canvas (fixture.roll);

    for (int lowerWhite : { 59, 64 })
    {
        const auto hover = canvasRowPoint (fixture.roll, 5, lowerWhite);
        canvas.mouseMove (makeMouseEvent (canvas, hover, hover, 0, false));
        const auto image = paintKeyboard (fixture.roll);

        const auto divider = image.getPixelAt (3, keyTopY (fixture.roll, lowerWhite));
        const auto tintedFace = image.getPixelAt (3, keyTopY (fixture.roll, lowerWhite) + 3);
        CHECK (divider.getBrightness() < tintedFace.getBrightness());
    }
}

namespace
{
    using lotro::playbacktest::addNote;

    // A Source roll over one long note, wide enough that zooming in scrolls.
    struct PlayheadRollFixture
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        SongDocument doc;
        juce::ValueTree track = doc.addTrack ("A", (int) 0xFFAABBCC, 0, 0);
        lotro::playbacktest::RecordingSink sink;
        PlaybackController controller { doc, sink };
        std::unique_ptr<SourceTrackNoteSource> source;
        PianoRollComponent roll { PianoRollComponent::Role::Source, &doc };

        PlayheadRollFixture()
        {
            addNote (track, 60, 0, 9600);
            addNote (track, 64, 1920, 480);
            controller.flushRebuild();
            source = std::make_unique<SourceTrackNoteSource> (track);
            roll.setSize (800, 400);
            roll.setNoteSource (source.get(), 480, doc.getMeterMapNode());
        }
    };
}

TEST_CASE ("PianoRollComponent: tick<->x mapping is component-local and scroll-aware", "[piano-roll][playhead]")
{
    PlayheadRollFixture f;
    const int gutterWidth = Access::geometry (f.roll).getKeyboardGutterWidth();

    CHECK (f.roll.xForTickInComponent (0.0) == gutterWidth);   // right of the gutter, never inside it
    // Fitted, there are more ticks than pixels: a round trip is only good to one pixel.
    const double ticksPerPixel = 480.0 / Access::geometry (f.roll).getPixelsPerQuarterNote();
    CHECK (f.roll.tickForXInComponent (f.roll.xForTickInComponent (480.0))
           == Catch::Approx (480.0).margin (ticksPerPixel + 1.0));

    for (int i = 0; i < 15; ++i)
        Access::zoom (f.roll, 1.0f);
    Access::viewport (f.roll).setViewPosition (300, Access::viewport (f.roll).getViewPositionY());
    const int scrollX = Access::viewport (f.roll).getViewPositionX();
    REQUIRE (scrollX > 0);

    CHECK (f.roll.xForTickInComponent (1920.0) == Access::geometry (f.roll).xForTick (1920) - scrollX);
    CHECK (f.roll.tickForXInComponent (f.roll.xForTickInComponent (1920.0)) == Catch::Approx (1920.0).margin (1.0));
}

TEST_CASE ("PianoRollComponent: the playhead overlay is Source-role only and sits under the gutter", "[piano-roll][playhead]")
{
    PlayheadRollFixture f;
    PianoRollComponent preview (PianoRollComponent::Role::Preview);
    preview.setPlayback (&f.controller);
    CHECK (! preview.hasPlayheadForTesting());

    f.roll.setPlayback (&f.controller);
    REQUIRE (f.roll.hasPlayheadForTesting());
    auto* playhead = Access::playhead (f.roll);
    const int viewportIndex = f.roll.getIndexOfChildComponent (&Access::viewport (f.roll));
    const int playheadIndex = f.roll.getIndexOfChildComponent (playhead);
    const int gutterIndex = f.roll.getIndexOfChildComponent (&Access::gutter (f.roll));
    CHECK (viewportIndex < playheadIndex);
    CHECK (playheadIndex < gutterIndex);

    f.roll.setPlayback (nullptr);
    CHECK (! f.roll.hasPlayheadForTesting());
}

TEST_CASE ("PianoRollComponent: the playhead line lands on the x where the note at that tick is drawn", "[piano-roll][playhead]")
{
    PlayheadRollFixture f;
    f.roll.setPlayback (&f.controller);
    f.controller.seekToTick (1920.0);

    const auto noteBounds = Access::geometry (f.roll).noteBounds (f.source->getNote (1));
    CHECK (Access::playheadX (f.roll) == noteBounds.x);
}

TEST_CASE ("PianoRollComponent: the playhead repaints when the view changes under an unchanged position", "[piano-roll][playhead]")
{
    PlayheadRollFixture f;
    f.roll.setPlayback (&f.controller);
    f.controller.seekToTick (1920.0);

    int before = Access::playheadRepaints (f.roll);
    Access::zoom (f.roll, 1.0f);
    CHECK (Access::playheadRepaints (f.roll) > before);

    for (int i = 0; i < 15; ++i)
        Access::zoom (f.roll, 1.0f);

    before = Access::playheadRepaints (f.roll);
    const int xBefore = Access::playheadX (f.roll);
    Access::viewport (f.roll).setViewPosition (200, Access::viewport (f.roll).getViewPositionY());
    CHECK (Access::playheadX (f.roll) != xBefore);
    CHECK (Access::playheadRepaints (f.roll) > before);

    before = Access::playheadRepaints (f.roll);
    f.roll.setSize (600, 300);
    CHECK (Access::playheadRepaints (f.roll) > before);
}

TEST_CASE ("PianoRollComponent: following a playing playhead off-screen scrolls so it sits at the gutter edge", "[piano-roll][playhead]")
{
    PlayheadRollFixture f;
    f.roll.setPlayback (&f.controller);
    for (int i = 0; i < 15; ++i)
        Access::zoom (f.roll, 1.0f);
    auto& viewport = Access::viewport (f.roll);
    viewport.setViewPosition (0, viewport.getViewPositionY());

    f.controller.seekToTick (6000.0);
    REQUIRE (f.roll.xForTickInComponent (6000.0) >= viewport.getMaximumVisibleWidth());

    Access::follow (f.roll, /*playing*/ false);
    CHECK (viewport.getViewPositionX() == 0);

    Access::follow (f.roll, /*playing*/ true);
    const int gutterWidth = Access::geometry (f.roll).getKeyboardGutterWidth();
    CHECK (viewport.getViewPositionX() > 0);
    CHECK (f.roll.xForTickInComponent (6000.0) == gutterWidth);
    CHECK (! Access::timelineFitted (f.roll));
}

TEST_CASE ("PianoRollComponent: following a playhead at the end of a fitted view keeps fitted mode", "[piano-roll][playhead]")
{
    PlayheadRollFixture f;
    f.roll.setPlayback (&f.controller);
    REQUIRE (Access::timelineFitted (f.roll));

    // The long note ends at tick 9600: the playhead sits on the right edge (so
    // follow's "off-screen" test fires), but the clamped scroll range is empty,
    // so there is nothing to flip.
    f.controller.seekToTick (9600.0);
    REQUIRE (f.roll.xForTickInComponent (9600.0) >= Access::viewport (f.roll).getMaximumVisibleWidth());
    const int repaints = Access::playheadRepaints (f.roll);
    Access::follow (f.roll, /*playing*/ true);
    CHECK (Access::viewport (f.roll).getViewPositionX() == 0);
    CHECK (Access::timelineFitted (f.roll));
    CHECK (Access::playheadRepaints (f.roll) == repaints);
}

TEST_CASE ("PianoRollComponent: with follow switched off a playing playhead never scrolls the view; switching it on restores the page-flip", "[piano-roll][playhead]")
{
    PlayheadRollFixture f;
    f.roll.setPlayback (&f.controller);
    for (int i = 0; i < 15; ++i)
        Access::zoom (f.roll, 1.0f);
    auto& viewport = Access::viewport (f.roll);
    viewport.setViewPosition (0, viewport.getViewPositionY());
    f.controller.seekToTick (6000.0);
    REQUIRE (f.roll.xForTickInComponent (6000.0) >= viewport.getMaximumVisibleWidth());
    CHECK (f.roll.getFollowPlayhead());   // default: on

    f.roll.setFollowPlayhead (false);
    Access::follow (f.roll, /*playing*/ true);
    CHECK (viewport.getViewPositionX() == 0);

    f.roll.setFollowPlayhead (true);
    Access::follow (f.roll, /*playing*/ true);
    CHECK (viewport.getViewPositionX() > 0);
}

TEST_CASE ("PianoRollComponent: a plain click on empty canvas sets the shared start marker; notes, drags, double-clicks and right-clicks do not", "[piano-roll][marker]")
{
    PlayheadRollFixture f;
    f.roll.setEditableTrack (f.track);
    f.roll.setPlayback (&f.controller);
    const auto& geometry = Access::geometry (f.roll);
    const int emptyY = geometry.yForPitch (40) + geometry.getRowHeight() / 2;   // no note at pitch 40
    const int noteY = geometry.yForPitch (60) + geometry.getRowHeight() / 2;    // inside the long note

    // Empty space: press and release in place.
    const juce::Point<int> empty { geometry.xForTick (2400), emptyY };
    Access::mouseDown (f.roll, empty, {}, false);
    Access::mouseUp (f.roll, empty);
    REQUIRE (f.controller.getMarkerTick().has_value());
    CHECK (*f.controller.getMarkerTick() == Catch::Approx ((double) geometry.tickForX (empty.x)));

    f.controller.clearMarker();

    // On a note: selects it, no marker.
    const juce::Point<int> onNote { geometry.xForTick (2400), noteY };
    Access::mouseDown (f.roll, onNote, {}, false);
    Access::mouseUp (f.roll, onNote);
    CHECK (! f.controller.getMarkerTick().has_value());

    // A drag across empty space: no marker.
    Access::mouseDown (f.roll, empty, {}, false);
    Access::mouseDrag (f.roll, empty.translated (60, 0));
    Access::mouseUp (f.roll, empty.translated (60, 0));
    CHECK (! f.controller.getMarkerTick().has_value());

    // A double-click creates a note: no marker.
    Access::mouseDown (f.roll, empty, {}, true);
    Access::mouseUp (f.roll, empty);
    CHECK (! f.controller.getMarkerTick().has_value());

    // A right-click picks a pitch: no marker.
    Access::mouseDown (f.roll, empty, juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier), false);
    Access::mouseUp (f.roll, empty);
    CHECK (! f.controller.getMarkerTick().has_value());
}

TEST_CASE ("PianoRollComponent: with snap on a click on empty canvas sets the marker on the nearest grid line; off or with no grid, exactly", "[piano-roll][marker]")
{
    PlayheadRollFixture f;
    f.roll.setEditableTrack (f.track);
    f.roll.setPlayback (&f.controller);
    f.roll.setGridTicks (480);
    const auto& geometry = Access::geometry (f.roll);
    const juce::Point<int> empty { geometry.xForTick (1100), geometry.yForPitch (40) + geometry.getRowHeight() / 2 };
    const double exact = (double) geometry.tickForX (empty.x);
    REQUIRE (exact != 960.0);

    CHECK_FALSE (f.roll.getSnapToGrid());   // off unless the host turns it on
    Access::mouseDown (f.roll, empty, {}, false);
    Access::mouseUp (f.roll, empty);
    CHECK (*f.controller.getMarkerTick() == exact);

    f.roll.setSnapToGrid (true);
    Access::mouseDown (f.roll, empty, {}, false);
    Access::mouseUp (f.roll, empty);
    CHECK (*f.controller.getMarkerTick() == 960.0);

    f.roll.setGridTicks (0);
    Access::mouseDown (f.roll, empty, {}, false);
    Access::mouseUp (f.roll, empty);
    CHECK (*f.controller.getMarkerTick() == exact);
}

TEST_CASE ("PianoRollComponent: Snap to Grid reaches the note editor, and Alt on a drag moves freely", "[piano-roll][grid-snap]")
{
    PlayheadRollFixture f;   // pitch 64 at 1920..2400
    f.roll.setEditableTrack (f.track);
    f.roll.setGridTicks (480);
    f.roll.setSnapToGrid (true);
    const auto& geometry = Access::geometry (f.roll);
    auto note = SongDocument::getNotesNode (f.track).getChild (1);
    const juce::Point<int> press { geometry.xForTick (2160), geometry.yForPitch (64) + geometry.getRowHeight() / 2 };
    const auto end = press.translated (geometry.xForTick (300) - geometry.xForTick (0), 0);

    Access::mouseDown (f.roll, press, {}, false);
    Access::mouseDrag (f.roll, end);
    CHECK ((int) note.getProperty (SongIDs::startTick) == 2400);

    Access::mouseDrag (f.roll, end, juce::ModifierKeys (juce::ModifierKeys::altModifier));
    const int free = 1920 + geometry.tickForX (end.x) - geometry.tickForX (press.x);
    REQUIRE (free != 2400);
    CHECK ((int) note.getProperty (SongIDs::startTick) == free);
    Access::mouseUp (f.roll, end);
}

TEST_CASE ("PianoRollComponent: the start marker is drawn by an overlay between the canvas and the gutter", "[piano-roll][marker]")
{
    PlayheadRollFixture f;
    PianoRollComponent preview (PianoRollComponent::Role::Preview);
    preview.setPlayback (&f.controller);
    CHECK (! preview.hasMarkerOverlayForTesting());

    f.roll.setPlayback (&f.controller);
    REQUIRE (f.roll.hasMarkerOverlayForTesting());
    f.controller.setMarkerTick (1920.0);
    CHECK (Access::markerOverlay (f.roll)->currentX() == f.roll.xForTickInComponent (1920.0));
    CHECK (f.roll.getIndexOfChildComponent (Access::markerOverlay (f.roll))
           < f.roll.getIndexOfChildComponent (&Access::gutter (f.roll)));

    f.roll.setPlayback (nullptr);
    CHECK (! f.roll.hasMarkerOverlayForTesting());
}

TEST_CASE ("PianoRollComponent: the loop range is shaded by an overlay under the playhead and marker, over the notes only", "[piano-roll][loop]")
{
    PlayheadRollFixture f;
    PianoRollComponent preview (PianoRollComponent::Role::Preview);
    preview.setPlayback (&f.controller);
    CHECK (! preview.hasLoopOverlayForTesting());

    CHECK (! f.roll.hasLoopOverlayForTesting());
    f.roll.setPlayback (&f.controller);
    REQUIRE (f.roll.hasLoopOverlayForTesting());
    auto* overlay = Access::loopOverlay (f.roll);
    CHECK (! overlay->currentXs().has_value());

    REQUIRE (f.controller.setLoopRange (1440.0, 3360.0));
    const auto xs = overlay->currentXs();
    REQUIRE (xs.has_value());
    CHECK (xs->first == f.roll.xForTickInComponent (1440.0));
    CHECK (xs->second == f.roll.xForTickInComponent (3360.0));

    // Below the playhead and marker lines, and the gutter above them all.
    CHECK (f.roll.getIndexOfChildComponent (overlay) < f.roll.getIndexOfChildComponent (Access::playhead (f.roll)));
    CHECK (f.roll.getIndexOfChildComponent (overlay) < f.roll.getIndexOfChildComponent (Access::markerOverlay (f.roll)));
    CHECK (f.roll.getIndexOfChildComponent (overlay) < f.roll.getIndexOfChildComponent (&Access::gutter (f.roll)));
    // Laid out like the playhead: over the scrolling canvas, not the scroll bars.
    CHECK (overlay->getBounds() == Access::playhead (f.roll)->getBounds());
    CHECK (! overlay->getBounds().isEmpty());

    f.roll.setPlayback (nullptr);
    CHECK (! f.roll.hasLoopOverlayForTesting());
}

TEST_CASE ("PianoRollComponent: a loop range change tells the timing bar above to repaint", "[piano-roll][loop]")
{
    PlayheadRollFixture f;
    f.roll.setPlayback (&f.controller);
    int viewChanges = 0;
    f.roll.onViewChanged = [&viewChanges] { ++viewChanges; };

    REQUIRE (f.controller.setLoopRange (1440.0, 3360.0));
    CHECK (viewChanges == 1);
    f.controller.clearLoopRange();
    CHECK (viewChanges == 2);
}

namespace
{
    // x (component-local) of the middle of the area the notes are visible in:
    // the keyboard gutter hides the left of the viewport.
    int notesCentreX (PianoRollComponent& roll)
    {
        const int gutter = Access::geometry (roll).getKeyboardGutterWidth();
        return gutter + (Access::viewport (roll).getMaximumVisibleWidth() - gutter) / 2;
    }
}

TEST_CASE ("PianoRollComponent: plain wheel zooms about the start marker, centring it first", "[piano-roll][wheel]")
{
    PlayheadRollFixture f;
    f.roll.setPlayback (&f.controller);
    for (int i = 0; i < 5; ++i)
        Access::wheel (f.roll, 1.0f, {});   // zoomed in enough for the marker to be able to reach the centre

    f.controller.setMarkerTick (4800.0);
    const double zoomBefore = Access::geometry (f.roll).getPixelsPerQuarterNote();

    Access::wheel (f.roll, 1.0f, {});
    CHECK (Access::geometry (f.roll).getPixelsPerQuarterNote() > zoomBefore);
    CHECK (std::abs (f.roll.xForTickInComponent (4800.0) - notesCentreX (f.roll)) <= 2);

    // Scrolled away, the next notch jumps back to the marker.
    Access::viewport (f.roll).setViewPosition (0, Access::viewport (f.roll).getViewPositionY());
    Access::wheel (f.roll, -1.0f, {});
    CHECK (std::abs (f.roll.xForTickInComponent (4800.0) - notesCentreX (f.roll)) <= 2);
}

TEST_CASE ("PianoRollComponent: with no marker, plain wheel zooms about the middle of the view", "[piano-roll][wheel]")
{
    PlayheadRollFixture f;
    f.roll.setPlayback (&f.controller);
    for (int i = 0; i < 5; ++i)
        Access::wheel (f.roll, 1.0f, {});

    const int centreX = notesCentreX (f.roll);
    const double centreTick = f.roll.tickForXInComponent (centreX);
    Access::wheel (f.roll, 1.0f, {});
    CHECK (std::abs (f.roll.tickForXInComponent (centreX) - centreTick) <= 20.0);
}

namespace
{
    constexpr int gutterPointX = 20;   // inside the 60 px keyboard gutter
    constexpr int canvasPointX = 300;
    const auto ctrl = juce::ModifierKeys::ctrlModifier;
}

TEST_CASE ("PianoRollComponent: plain wheel over the canvas zooms and leaves vertical scroll alone", "[piano-roll][wheel]")
{
    PlayheadRollFixture f;
    f.roll.setEditableTrack (f.track);
    auto& viewport = Access::viewport (f.roll);
    REQUIRE (viewport.getVerticalScrollBar().isVisible());
    const int yBefore = viewport.getViewPositionY();
    const double zoomBefore = Access::geometry (f.roll).getPixelsPerQuarterNote();

    Access::wheelAt (f.roll, 1.0f, {}, { canvasPointX, 100 });

    CHECK (Access::geometry (f.roll).getPixelsPerQuarterNote() > zoomBefore);
    CHECK (viewport.getViewPositionY() == yBefore);
    CHECK (f.roll.getRowHeight() == 14);
}

TEST_CASE ("PianoRollComponent: plain wheel over the keyboard gutter scrolls vertically and never zooms", "[piano-roll][wheel]")
{
    PlayheadRollFixture f;
    f.roll.setEditableTrack (f.track);
    auto& viewport = Access::viewport (f.roll);
    REQUIRE (viewport.getVerticalScrollBar().isVisible());
    const double zoomBefore = Access::geometry (f.roll).getPixelsPerQuarterNote();
    const int xBefore = viewport.getViewPositionX();
    const int start = viewport.getViewPositionY();

    Access::wheelAt (f.roll, -1.0f, {}, { gutterPointX, 100 });   // wheel down: towards lower pitches
    CHECK (viewport.getViewPositionY() == start + 50);
    Access::wheelAt (f.roll, 0.5f, {}, { gutterPointX, 100 });    // half a notch up
    CHECK (viewport.getViewPositionY() == start + 25);

    for (int i = 0; i < 100; ++i)
        Access::wheelAt (f.roll, 1.0f, {}, { gutterPointX, 100 });
    CHECK (viewport.getViewPositionY() == 0);                      // clamped at the top
    for (int i = 0; i < 100; ++i)
        Access::wheelAt (f.roll, -1.0f, {}, { gutterPointX, 100 });
    CHECK (viewport.getViewPositionY() == Access::canvas (f.roll).getHeight() - viewport.getMaximumVisibleHeight());

    CHECK (Access::geometry (f.roll).getPixelsPerQuarterNote() == Catch::Approx (zoomBefore));
    CHECK (viewport.getViewPositionX() == xBefore);
}

TEST_CASE ("PianoRollComponent: gutter wheel does nothing when the whole pitch range fits", "[piano-roll][wheel]")
{
    PlayheadRollFixture f;
    f.roll.setSize (800, 3000);   // taller than 128 rows
    auto& viewport = Access::viewport (f.roll);
    REQUIRE (! viewport.getVerticalScrollBar().isVisible());
    const double zoomBefore = Access::geometry (f.roll).getPixelsPerQuarterNote();

    Access::wheelAt (f.roll, -1.0f, {}, { gutterPointX, 100 });
    Access::wheelAt (f.roll, 1.0f, {}, { gutterPointX, 100 });

    CHECK (viewport.getViewPositionY() == 0);
    CHECK (Access::geometry (f.roll).getPixelsPerQuarterNote() == Catch::Approx (zoomBefore));
}

TEST_CASE ("PianoRollComponent: ctrl+wheel resizes the pitch rows, one pixel per notch, wherever the pointer is", "[piano-roll][wheel][row-height]")
{
    PlayheadRollFixture f;
    f.roll.setEditableTrack (f.track);
    REQUIRE (f.roll.getRowHeight() == 14);
    const double zoomBefore = Access::geometry (f.roll).getPixelsPerQuarterNote();
    auto& viewport = Access::viewport (f.roll);
    viewport.setViewPosition (0, viewport.getViewPositionY());   // pin X; only Y may move on a row resize

    Access::wheelAt (f.roll, 1.0f, ctrl, { canvasPointX, 100 });
    CHECK (f.roll.getRowHeight() == 15);
    CHECK (Access::geometry (f.roll).getRowHeight() == 15);
    Access::wheelAt (f.roll, -2.0f, ctrl, { gutterPointX, 100 });   // over the gutter too
    CHECK (f.roll.getRowHeight() == 13);

    CHECK (Access::geometry (f.roll).getPixelsPerQuarterNote() == Catch::Approx (zoomBefore));
    CHECK (viewport.getViewPositionX() == 0);
    CHECK (Access::canvas (f.roll).getHeight() == 128 * 13);       // content height tracks the rows
}

TEST_CASE ("PianoRollComponent: fractional ctrl+wheel deltas accumulate; the limits clamp and discard the excess", "[piano-roll][wheel][row-height]")
{
    PlayheadRollFixture f;
    Access::wheelAt (f.roll, 0.4f, ctrl, { canvasPointX, 100 });
    CHECK (f.roll.getRowHeight() == 14);   // 14.4
    Access::wheelAt (f.roll, 0.4f, ctrl, { canvasPointX, 100 });
    CHECK (f.roll.getRowHeight() == 15);   // 14.8

    Access::wheelAt (f.roll, 1000.0f, ctrl, { canvasPointX, 100 });
    CHECK (f.roll.getRowHeight() == PianoRollComponent::maxRowHeight);
    Access::wheelAt (f.roll, -1.0f, ctrl, { canvasPointX, 100 });
    CHECK (f.roll.getRowHeight() == PianoRollComponent::maxRowHeight - 1);   // the 1000 did not pile up

    Access::wheelAt (f.roll, -1000.0f, ctrl, { canvasPointX, 100 });
    CHECK (f.roll.getRowHeight() == PianoRollComponent::minRowHeight);
    Access::wheelAt (f.roll, 1.0f, ctrl, { canvasPointX, 100 });
    CHECK (f.roll.getRowHeight() == PianoRollComponent::minRowHeight + 1);
    CHECK (PianoRollComponent::maxRowHeight == 40);
}

TEST_CASE ("PianoRollComponent: resizing the rows keeps the pitch under the pointer in place", "[piano-roll][wheel][row-height]")
{
    PlayheadRollFixture f;
    auto& viewport = Access::viewport (f.roll);
    viewport.setViewPosition (0, 700);
    const int h = f.roll.getRowHeight();
    // The middle of a row, so a one-pixel rounding wobble cannot change which row it is.
    const int pointerY = ((viewport.getViewPositionY() + 150) / h) * h + h / 2 - viewport.getViewPositionY();
    const int pitch = Access::geometry (f.roll).pitchForY (viewport.getViewPositionY() + pointerY);

    for (int i = 0; i < 12; ++i)
    {
        Access::wheelAt (f.roll, 1.0f, ctrl, { canvasPointX, pointerY });
        CHECK (Access::geometry (f.roll).pitchForY (viewport.getViewPositionY() + pointerY) == pitch);
    }
    CHECK (f.roll.getRowHeight() == 26);
    for (int i = 0; i < 12; ++i)
    {
        Access::wheelAt (f.roll, -1.0f, ctrl, { gutterPointX, pointerY });
        CHECK (Access::geometry (f.roll).pitchForY (viewport.getViewPositionY() + pointerY) == pitch);
    }
    CHECK (f.roll.getRowHeight() == 14);
}

TEST_CASE ("PianoRollComponent: the editor's hit-testing follows the row height", "[piano-roll][row-height]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 61 });   // adjacent rows: a stale row height lands on the wrong pitch
    auto& editor = Access::editor (fixture.roll);
    auto notes = SongDocument::getNotesNode (fixture.track);

    for (const float delta : { -100.0f, 100.0f })   // min, then max
    {
        Access::wheelAt (fixture.roll, delta, ctrl, { canvasPointX, 100 });
        const auto& geometry = Access::geometry (fixture.roll);
        const int h = fixture.roll.getRowHeight();
        REQUIRE (h == (delta < 0 ? PianoRollComponent::minRowHeight : PianoRollComponent::maxRowHeight));

        const int x = geometry.xForTick (ticksPerQuarter + ticksPerQuarter / 2);
        for (const int dy : { h / 2, h - 2 })
        {
            const juce::Point<int> pos { x, geometry.yForPitch (61) + dy };
            Access::mouseDown (fixture.roll, pos, {}, false);
            Access::mouseUp (fixture.roll, pos);
            CHECK (editor.getNumSelected() == 1);
            CHECK (editor.isSelected (notes.getChild (1)));
            Access::mouseDown (fixture.roll, { x, geometry.yForPitch (127) }, {}, false);   // empty canvas deselects
            Access::mouseUp (fixture.roll, { x, geometry.yForPitch (127) });
        }
        CHECK (Access::canvas (fixture.roll).getHeight() == 128 * h);
    }

    // An in-between height too.
    Access::wheelAt (fixture.roll, -100.0f, ctrl, { canvasPointX, 100 });
    Access::wheelAt (fixture.roll, 4.0f, ctrl, { canvasPointX, 100 });
    REQUIRE (fixture.roll.getRowHeight() == PianoRollComponent::minRowHeight + 4);
    const auto& geometry = Access::geometry (fixture.roll);
    Access::mouseDown (fixture.roll, { geometry.xForTick (240), geometry.yForPitch (60) + 7 }, {}, false);
    CHECK (editor.isSelected (notes.getChild (0)));
    CHECK (! editor.isSelected (notes.getChild (1)));
}

TEST_CASE ("PianoRollComponent: the row height survives new sources and tracks, and stays per instance and per role", "[piano-roll][row-height]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 36, 44 });
    Access::wheelAt (fixture.roll, 16.0f, ctrl, { canvasPointX, 100 });
    REQUIRE (fixture.roll.getRowHeight() == 30);

    fixture.roll.setNoteSource (nullptr, ticksPerQuarter, {});
    CHECK (fixture.roll.getRowHeight() == 30);
    fixture.roll.setNoteSource (fixture.source.get(), ticksPerQuarter, {});
    fixture.roll.setEditableTrack (fixture.track);
    CHECK (fixture.roll.getRowHeight() == 30);
    // The initial vertical centring uses the current height.
    CHECK (std::abs (fixture.visibleCentreY() - fixture.rowCentreY (40)) <= 30);

    fixture.roll.setBounds (0, 0, 500, 300);   // a window resize refits the timeline
    CHECK (fixture.roll.getRowHeight() == 30);
    CHECK (Access::canvas (fixture.roll).getHeight() == 128 * 30);

    SourceRollFixture other ({ 60 });
    CHECK (other.roll.getRowHeight() == 14);
}

TEST_CASE ("PianoRollComponent: Source wheel mapping decided by region and modifiers", "[piano-roll][wheel]")
{
    using Region = PianoRollComponent::WheelRegion;
    PlayheadRollFixture f;
    f.roll.setEditableTrack (f.track);
    auto& viewport = Access::viewport (f.roll);
    const double zoom0 = Access::geometry (f.roll).getPixelsPerQuarterNote();

    // Shift pans horizontally from either region and leaves zoom and rows alone.
    for (int i = 0; i < 10; ++i)
        Access::handleWheel (f.roll, Region::Canvas, 1.0f, {}, 100);
    const double zoomed = Access::geometry (f.roll).getPixelsPerQuarterNote();
    REQUIRE (zoomed > zoom0);
    viewport.setViewPosition (0, viewport.getViewPositionY());
    CHECK (Access::handleWheel (f.roll, Region::Gutter, -1.0f, juce::ModifierKeys::shiftModifier, 100));
    CHECK (viewport.getViewPositionX() > 0);
    CHECK (Access::geometry (f.roll).getPixelsPerQuarterNote() == Catch::Approx (zoomed));
    CHECK (f.roll.getRowHeight() == 14);
}

TEST_CASE ("PianoRollComponent: the Preview role keeps ctrl+wheel = zoom and plain wheel = scroll", "[piano-roll][wheel]")
{
    using Region = PianoRollComponent::WheelRegion;
    juce::ScopedJuceInitialiser_GUI juceInit;
    PreviewNote note;
    note.prePitch = 60;
    note.startTick = 0;
    note.durationTicks = ticksPerQuarter;
    PreviewNoteSource source ({ note });
    PianoRollComponent roll (PianoRollComponent::Role::Preview);
    roll.setBounds (0, 0, viewportWidth, viewportHeight);
    roll.setNoteSource (&source, ticksPerQuarter, {});
    const double zoomBefore = Access::geometry (roll).getPixelsPerQuarterNote();

    CHECK (Access::handleWheel (roll, Region::Canvas, 1.0f, ctrl, 50));
    CHECK (Access::geometry (roll).getPixelsPerQuarterNote() > zoomBefore);
    CHECK (roll.getRowHeight() == 14);

    const double zoomed = Access::geometry (roll).getPixelsPerQuarterNote();
    CHECK (! Access::handleWheel (roll, Region::Canvas, 1.0f, {}, 50));   // left to the viewport's own scrolling
    CHECK (! Access::handleWheel (roll, Region::Gutter, 1.0f, {}, 50));
    CHECK (Access::geometry (roll).getPixelsPerQuarterNote() == Catch::Approx (zoomed));
}

TEST_CASE ("PianoRollComponent: shift+wheel pans horizontally", "[piano-roll][wheel]")
{
    PlayheadRollFixture f;
    for (int i = 0; i < 10; ++i)
        Access::wheel (f.roll, 1.0f, {});
    auto& viewport = Access::viewport (f.roll);
    viewport.setViewPosition (0, viewport.getViewPositionY());
    const double zoomBefore = Access::geometry (f.roll).getPixelsPerQuarterNote();

    Access::wheel (f.roll, -1.0f, juce::ModifierKeys::shiftModifier);
    CHECK (viewport.getViewPositionX() > 0);
    CHECK (Access::geometry (f.roll).getPixelsPerQuarterNote() == Catch::Approx (zoomBefore));
}

TEST_CASE ("PianoRollComponent: grid lines run from the bar down to the note values at least 8 px wide", "[piano-roll][grid-lines]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    PianoRollComponent roll (PianoRollComponent::Role::Source, &doc);
    constexpr int width = 400;
    constexpr int height = 100;
    roll.setBounds (0, 0, width, height);

    // Default geometry: 480 PPQ at 40 px per quarter, so a 1/16 note is 10 px and a 1/32 is 5 px.
    PianoRollGeometry geometry;

    juce::Image image (juce::Image::ARGB, width, height, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    Access::paintCanvas (roll, g, { 0, 0, width, height });

    auto lineAt = [&] (int tick)
    {
        const int x = geometry.xForTick (tick);
        return image.getPixelAt (x, 5) != image.getPixelAt (x + 1, 5);
    };

    CHECK (lineAt (1920));          // bar
    CHECK (lineAt (480));           // quarter
    CHECK (lineAt (240));           // eighth
    CHECK (lineAt (120));           // sixteenth
    CHECK_FALSE (lineAt (60));      // thirty-second: only 5 px
}

namespace
{
    // Paints the canvas rows from `topPitch` down to `bottomPitch` into an image
    // of the visible width, origin at `topPitch`'s row top.
    juce::Image paintRows (PianoRollComponent& roll, int topPitch, int bottomPitch)
    {
        const auto& geometry = Access::geometry (roll);
        const int top = geometry.yForPitch (topPitch);
        const int height = geometry.yForPitch (bottomPitch) + geometry.getRowHeight() - top;
        juce::Image image (juce::Image::ARGB, viewportWidth, height, true, juce::SoftwareImageType());
        juce::Graphics g (image);
        g.setOrigin (0, -top);
        Access::paintCanvas (roll, g, { 0, top, viewportWidth, height });
        return image;
    }

    // Pixels in `pitch`'s row (image from paintRows, origin at `topPitch`) of exactly `colour`.
    int pixelsOfColourInRow (const juce::Image& image, PianoRollComponent& roll, int topPitch, int pitch, juce::Colour colour)
    {
        const auto& geometry = Access::geometry (roll);
        const int y = geometry.yForPitch (pitch) - geometry.yForPitch (topPitch) + geometry.getRowHeight() / 2;
        int count = 0;
        for (int x = 0; x < image.getWidth(); ++x)
            if (image.getPixelAt (x, y) == colour)
                ++count;
        return count;
    }

    const juce::uint32 activeRowColour = juce::Colour (PianoRollPalette::rowBandLight)
                                             .overlaidWith (juce::Colour (PianoRollPalette::activeRowTint)).getARGB();
}

TEST_CASE ("PianoRollComponent: a click on the canvas makes the row under it the active row, on a note or not", "[piano-roll][active-row]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 64 });
    const auto& geometry = Access::geometry (fixture.roll);
    CHECK (fixture.roll.getActivePitch() == -1);

    const int emptyX = geometry.xForTick (ticksPerQuarter * 6);
    Access::mouseDown (fixture.roll, canvasRowPoint (fixture.roll, emptyX, 66), {}, false);
    Access::mouseUp (fixture.roll, canvasRowPoint (fixture.roll, emptyX, 66));
    CHECK (fixture.roll.getActivePitch() == 66);

    const int onFirstNote = geometry.xForTick (ticksPerQuarter / 2);
    Access::mouseDown (fixture.roll, canvasRowPoint (fixture.roll, onFirstNote, 60), {}, false);
    Access::mouseUp (fixture.roll, canvasRowPoint (fixture.roll, onFirstNote, 60));
    CHECK (fixture.roll.getActivePitch() == 60);

    // A right-click lands on a row too.
    Access::mouseDown (fixture.roll, canvasRowPoint (fixture.roll, emptyX, 70), juce::ModifierKeys::rightButtonModifier, false);
    CHECK (fixture.roll.getActivePitch() == 70);
}

TEST_CASE ("PianoRollComponent: the active row is painted lighter, the same grey for sharps/flats and naturals", "[piano-roll][active-row]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 64 });
    const int emptyX = Access::geometry (fixture.roll).xForTick (ticksPerQuarter * 6);
    const juce::Colour active (activeRowColour);

    const auto before = paintRows (fixture.roll, 72, 66);
    CHECK (pixelsOfColourInRow (before, fixture.roll, 72, 69, active) == 0);

    Access::mouseDown (fixture.roll, canvasRowPoint (fixture.roll, emptyX, 69), {}, false);   // A, a natural
    const auto natural = paintRows (fixture.roll, 72, 66);
    CHECK (pixelsOfColourInRow (natural, fixture.roll, 72, 69, active) > viewportWidth / 2);
    CHECK (pixelsOfColourInRow (natural, fixture.roll, 72, 68, active) == 0);   // neighbours untouched
    CHECK (pixelsOfColourInRow (natural, fixture.roll, 72, 70, active) == 0);

    Access::mouseDown (fixture.roll, canvasRowPoint (fixture.roll, emptyX, 70), {}, false);   // A#, a sharp
    const auto sharp = paintRows (fixture.roll, 72, 66);
    CHECK (pixelsOfColourInRow (sharp, fixture.roll, 72, 70, active) > viewportWidth / 2);
    CHECK (pixelsOfColourInRow (sharp, fixture.roll, 72, 69, active) == 0);
}

TEST_CASE ("PianoRollComponent: stepActivePitch moves the active row a semitone, clamped to 0..127", "[piano-roll][active-row]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 64 });
    const int emptyX = Access::geometry (fixture.roll).xForTick (ticksPerQuarter * 6);

    CHECK_FALSE (fixture.roll.stepActivePitch (+1));   // nothing active yet
    CHECK (fixture.roll.getActivePitch() == -1);

    Access::mouseDown (fixture.roll, canvasRowPoint (fixture.roll, emptyX, 60), {}, false);
    CHECK (fixture.roll.stepActivePitch (+1));
    CHECK (fixture.roll.getActivePitch() == 61);
    CHECK (fixture.roll.stepActivePitch (-1));
    CHECK (fixture.roll.stepActivePitch (-1));
    CHECK (fixture.roll.getActivePitch() == 59);

    Access::mouseDown (fixture.roll, canvasRowPoint (fixture.roll, emptyX, 127), {}, false);
    CHECK (fixture.roll.stepActivePitch (+1));   // still claimed at the top, just no further
    CHECK (fixture.roll.getActivePitch() == 127);
    Access::mouseDown (fixture.roll, canvasRowPoint (fixture.roll, emptyX, 0), {}, false);
    CHECK (fixture.roll.stepActivePitch (-1));
    CHECK (fixture.roll.getActivePitch() == 0);
}

TEST_CASE ("PianoRollComponent: stepping the active row off screen scrolls just enough to show it", "[piano-roll][active-row]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 64 });
    const auto& geometry = Access::geometry (fixture.roll);
    auto& viewport = Access::viewport (fixture.roll);
    const int rowHeight = geometry.getRowHeight();
    const int emptyX = geometry.xForTick (ticksPerQuarter * 6);

    // The topmost fully visible row.
    const int topVisible = geometry.getTopPitch() - (viewport.getViewPositionY() + rowHeight - 1) / rowHeight;
    Access::mouseDown (fixture.roll, canvasRowPoint (fixture.roll, emptyX, topVisible), {}, false);
    const int scrollBefore = viewport.getViewPositionY();
    REQUIRE (scrollBefore > 0);

    fixture.roll.stepActivePitch (+1);
    CHECK (viewport.getViewPositionY() == geometry.yForPitch (topVisible + 1));   // row now at the top edge

    // The bottommost fully visible row, stepping down past it.
    const int visibleBottom = viewport.getViewPositionY() + viewport.getMaximumVisibleHeight();
    const int bottomVisible = geometry.getTopPitch() - visibleBottom / rowHeight + 1;
    Access::mouseDown (fixture.roll, canvasRowPoint (fixture.roll, emptyX, bottomVisible), {}, false);
    const int scrollAtBottom = viewport.getViewPositionY();
    fixture.roll.stepActivePitch (-1);
    CHECK (viewport.getViewPositionY() + viewport.getMaximumVisibleHeight()
           == geometry.yForPitch (bottomVisible - 1) + rowHeight);   // row now at the bottom edge

    // A step that stays on screen does not scroll.
    const int scrollAfter = viewport.getViewPositionY();
    REQUIRE (scrollAfter != scrollAtBottom);
    fixture.roll.stepActivePitch (+1);
    CHECK (viewport.getViewPositionY() == scrollAfter);
}

TEST_CASE ("PianoRollComponent: the active row's keyboard key is tinted and named like a hovered one", "[piano-roll][active-row][keyboard]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 64 });
    const int emptyX = Access::geometry (fixture.roll).xForTick (ticksPerQuarter * 6);
    const int leftX = 3;
    const int rowMiddle = Access::geometry (fixture.roll).getRowHeight() / 2;

    const auto plain = paintKeyboard (fixture.roll);
    Access::mouseDown (fixture.roll, canvasRowPoint (fixture.roll, emptyX, 62), {}, false);   // D4, a white key
    auto& canvas = Access::canvas (fixture.roll);
    const auto away = canvasRowPoint (fixture.roll, emptyX, 66);
    canvas.mouseMove (makeMouseEvent (canvas, away, away, 0, false));   // the mouse hovers elsewhere
    const auto active = paintKeyboard (fixture.roll);

    const auto face = active.getPixelAt (leftX, keyTopY (fixture.roll, 62) + rowMiddle);
    CHECK (face != plain.getPixelAt (leftX, keyTopY (fixture.roll, 62) + rowMiddle));
    CHECK (isLight (face));
    CHECK (markedPixelsInKeyRow (active, fixture.roll, 62) > 0);
}

TEST_CASE ("PianoRollComponent: the active row survives a track change and clears with no track", "[piano-roll][active-row]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 64 });
    const int emptyX = Access::geometry (fixture.roll).xForTick (ticksPerQuarter * 6);
    Access::mouseDown (fixture.roll, canvasRowPoint (fixture.roll, emptyX, 66), {}, false);

    auto other = fixture.doc.addTrack ("Track B", 0x00FF00, 1, 1);
    SourceTrackNoteSource otherSource (other);
    fixture.roll.setNoteSource (&otherSource, ticksPerQuarter, {});
    fixture.roll.setEditableTrack (other);
    CHECK (fixture.roll.getActivePitch() == 66);

    fixture.roll.setEditableTrack ({});
    fixture.roll.setNoteSource (nullptr, ticksPerQuarter, {});
    CHECK (fixture.roll.getActivePitch() == -1);
    CHECK_FALSE (fixture.roll.stepActivePitch (+1));
}

TEST_CASE ("PianoRollComponent: a Preview roll has no active row", "[piano-roll][active-row]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    PianoRollComponent roll (PianoRollComponent::Role::Preview);
    PreviewNoteSource source ({});
    roll.setBounds (0, 0, viewportWidth, viewportHeight);
    roll.setNoteSource (&source, ticksPerQuarter, {});
    Access::mouseDown (roll, { 200, 20 }, {}, false);
    CHECK (roll.getActivePitch() == -1);
    CHECK_FALSE (roll.stepActivePitch (+1));
}

TEST_CASE ("PianoRollComponent: dragging a note moves the active row with it, row by row", "[piano-roll][active-row]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 64 });
    const int onFirstNote = Access::geometry (fixture.roll).xForTick (ticksPerQuarter / 2);

    Access::mouseDown (fixture.roll, canvasRowPoint (fixture.roll, onFirstNote, 60), {}, false);
    Access::mouseDrag (fixture.roll, canvasRowPoint (fixture.roll, onFirstNote, 61));
    CHECK (fixture.roll.getActivePitch() == 61);
    Access::mouseDrag (fixture.roll, canvasRowPoint (fixture.roll, onFirstNote, 63));
    CHECK (fixture.roll.getActivePitch() == 63);
    Access::mouseUp (fixture.roll, canvasRowPoint (fixture.roll, onFirstNote, 63));
    CHECK ((int) SongDocument::getNotesNode (fixture.track).getChild (0).getProperty (SongIDs::pitch) == 63);
    CHECK (fixture.roll.getActivePitch() == 63);

    // Undoing the move leaves the display-only active row where it is.
    fixture.doc.getUndoManager().undo();
    CHECK (fixture.roll.getActivePitch() == 63);
}

TEST_CASE ("PianoRollComponent: dragging a selection moves the active row with the grabbed note", "[piano-roll][active-row]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 64 });   // starts 0 and 480
    const auto& geometry = Access::geometry (fixture.roll);
    const int onFirstNote = geometry.xForTick (ticksPerQuarter / 2);
    const int onSecondNote = geometry.xForTick (ticksPerQuarter + ticksPerQuarter / 2);

    Access::mouseDown (fixture.roll, canvasRowPoint (fixture.roll, onFirstNote, 60), {}, false);
    Access::mouseUp (fixture.roll, canvasRowPoint (fixture.roll, onFirstNote, 60));
    Access::mouseDown (fixture.roll, canvasRowPoint (fixture.roll, onSecondNote, 64), juce::ModifierKeys::ctrlModifier, false);
    Access::mouseUp (fixture.roll, canvasRowPoint (fixture.roll, onSecondNote, 64));
    REQUIRE (Access::editor (fixture.roll).getNumSelected() == 2);

    // Grab the lower note: the row follows it, not the upper one.
    Access::mouseDown (fixture.roll, canvasRowPoint (fixture.roll, onFirstNote, 60), {}, false);
    Access::mouseDrag (fixture.roll, canvasRowPoint (fixture.roll, onFirstNote, 62));
    CHECK (fixture.roll.getActivePitch() == 62);
    Access::mouseUp (fixture.roll, canvasRowPoint (fixture.roll, onFirstNote, 62));
    CHECK ((int) SongDocument::getNotesNode (fixture.track).getChild (1).getProperty (SongIDs::pitch) == 66);
}

TEST_CASE ("PianoRollComponent: resizing a note leaves the active row on the pressed row", "[piano-roll][active-row]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 64 });
    const auto& geometry = Access::geometry (fixture.roll);
    const int rightEdge = geometry.xForTick (ticksPerQuarter) - 2;
    const auto originalDuration = (int) SongDocument::getNotesNode (fixture.track).getChild (0).getProperty (SongIDs::durationTicks);

    Access::mouseDown (fixture.roll, canvasRowPoint (fixture.roll, rightEdge, 60), {}, false);
    Access::mouseDrag (fixture.roll, canvasRowPoint (fixture.roll, rightEdge + 40, 63));
    CHECK (fixture.roll.getActivePitch() == 60);
    Access::mouseUp (fixture.roll, canvasRowPoint (fixture.roll, rightEdge + 40, 63));
    CHECK ((int) SongDocument::getNotesNode (fixture.track).getChild (0).getProperty (SongIDs::durationTicks) > originalDuration);
    CHECK (fixture.roll.getActivePitch() == 60);
}

TEST_CASE ("PianoRollComponent: a rubber-band drag leaves the active row on the pressed row", "[piano-roll][active-row]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 64 });
    const int emptyX = Access::geometry (fixture.roll).xForTick (ticksPerQuarter * 6);

    Access::mouseDown (fixture.roll, canvasRowPoint (fixture.roll, emptyX, 66), juce::ModifierKeys::rightButtonModifier, false);
    Access::mouseDrag (fixture.roll, canvasRowPoint (fixture.roll, emptyX + 40, 62));
    CHECK (fixture.roll.getActivePitch() == 66);
    Access::mouseUp (fixture.roll, canvasRowPoint (fixture.roll, emptyX + 40, 62));
    CHECK (fixture.roll.getActivePitch() == 66);
}

namespace
{
    juce::ValueTree noteAt (SourceRollFixture& f, int index) { return SongDocument::getNotesNode (f.track).getChild (index); }
}

TEST_CASE ("PianoRollComponent: selectNoteNearest selects only the note starting nearest the tick, with no undo step", "[piano-roll][note-nav]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 62, 64, 65 });   // starts 0, 480, 960, 1440
    auto& editor = Access::editor (fixture.roll);
    fixture.doc.getUndoManager().clearUndoHistory();

    CHECK (fixture.roll.selectNoteNearest (1000.0));
    CHECK (editor.getNumSelected() == 1);
    CHECK (editor.isSelected (noteAt (fixture, 2)));

    CHECK (fixture.roll.selectNoteNearest (300.0));
    CHECK (editor.getNumSelected() == 1);   // replaces the selection
    CHECK (editor.isSelected (noteAt (fixture, 1)));
    CHECK_FALSE (fixture.doc.getUndoManager().canUndo());
}

TEST_CASE ("PianoRollComponent: selectNoteNearest breaks a tie on start by the active row", "[piano-roll][note-nav]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60 });
    juce::ValueTree note (SongIDs::NOTE);
    note.setProperty (SongIDs::pitch, 67, nullptr);
    note.setProperty (SongIDs::startTick, 0, nullptr);
    note.setProperty (SongIDs::durationTicks, ticksPerQuarter, nullptr);
    SongDocument::getNotesNode (fixture.track).addChild (note, -1, nullptr);

    const int emptyX = Access::geometry (fixture.roll).xForTick (ticksPerQuarter * 6);
    Access::mouseDown (fixture.roll, canvasRowPoint (fixture.roll, emptyX, 61), {}, false);
    Access::mouseUp (fixture.roll, canvasRowPoint (fixture.roll, emptyX, 61));
    REQUIRE (fixture.roll.selectNoteNearest (0.0));
    CHECK (Access::editor (fixture.roll).isSelected (noteAt (fixture, 0)));
    CHECK (fixture.roll.getActivePitch() == 61);   // the cursor stays put
}

TEST_CASE ("PianoRollComponent: selectNoteNearest does nothing with no notes, no track, or on a Preview roll", "[piano-roll][note-nav]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture empty ({});
    CHECK_FALSE (empty.roll.hasNotes());
    CHECK_FALSE (empty.roll.selectNoteNearest (0.0));

    SourceRollFixture fixture ({ 60 });
    CHECK (fixture.roll.hasNotes());
    fixture.roll.setEditableTrack ({});
    fixture.roll.setNoteSource (nullptr, ticksPerQuarter, {});
    CHECK_FALSE (fixture.roll.hasNotes());
    CHECK_FALSE (fixture.roll.selectNoteNearest (0.0));

    PianoRollComponent preview (PianoRollComponent::Role::Preview);
    PreviewNote previewNote;
    previewNote.prePitch = 60;
    previewNote.durationTicks = ticksPerQuarter;
    PreviewNoteSource source ({ previewNote });
    preview.setBounds (0, 0, viewportWidth, viewportHeight);
    preview.setNoteSource (&source, ticksPerQuarter, {});
    CHECK_FALSE (preview.selectNoteNearest (0.0));
}

TEST_CASE ("PianoRollComponent: selecting a nearest note off screen scrolls just enough to show it", "[piano-roll][note-nav]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 30, 100 });   // starts 0 and 480, far apart in pitch
    const auto& geometry = Access::geometry (fixture.roll);
    auto& viewport = Access::viewport (fixture.roll);
    const int rowHeight = geometry.getRowHeight();
    const int gutterWidth = geometry.getKeyboardGutterWidth();

    auto rowVisible = [&] (int pitch)
    {
        const int top = geometry.yForPitch (pitch);
        return top >= viewport.getViewPositionY()
            && top + rowHeight <= viewport.getViewPositionY() + viewport.getMaximumVisibleHeight();
    };
    REQUIRE_FALSE (rowVisible (30));
    REQUIRE_FALSE (rowVisible (100));

    REQUIRE (fixture.roll.selectNoteNearest (0.0));
    CHECK (rowVisible (30));
    CHECK (geometry.yForPitch (30) + rowHeight == viewport.getViewPositionY() + viewport.getMaximumVisibleHeight());

    // Zoomed in just until the second (shortened) note starts past the right edge.
    noteAt (fixture, 1).setProperty (SongIDs::durationTicks, ticksPerQuarter / 8, nullptr);
    PianoRollNote second;
    second.pitch = 100;
    second.startTick = ticksPerQuarter;
    second.durationTicks = ticksPerQuarter / 8;
    for (int i = 0; i < 40 && geometry.noteBounds (second).x < viewport.getMaximumVisibleWidth(); ++i)
    {
        Access::zoom (fixture.roll, 1.0f);
        viewport.setViewPosition (0, viewport.getViewPositionY());
    }
    const auto bounds = geometry.noteBounds (second);
    REQUIRE (bounds.x >= viewport.getMaximumVisibleWidth());
    REQUIRE (bounds.width <= viewport.getMaximumVisibleWidth() - gutterWidth);

    REQUIRE (fixture.roll.selectNoteNearest ((double) ticksPerQuarter));
    CHECK (rowVisible (100));
    const int viewX = viewport.getViewPositionX();
    CHECK (bounds.x >= viewX + gutterWidth);   // clear of the pinned keyboard
    CHECK (bounds.x + bounds.width <= viewX + viewport.getMaximumVisibleWidth());
}

TEST_CASE ("PianoRollComponent: addNoteNearestToSelection grows the selection outward from the tick, skipping selected notes", "[piano-roll][note-nav]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 62, 64, 65 });   // starts 0, 480, 960, 1440
    auto& editor = Access::editor (fixture.roll);
    fixture.doc.getUndoManager().clearUndoHistory();
    CHECK (fixture.roll.hasUnselectedNotes());

    // Nothing selected: the nearest is added, like a plain select.
    CHECK (fixture.roll.addNoteNearestToSelection (1000.0));
    CHECK (editor.getNumSelected() == 1);
    CHECK (editor.isSelected (noteAt (fixture, 2)));

    CHECK (fixture.roll.addNoteNearestToSelection (1000.0));   // 1440 (440 away) beats 480 (520 away)
    CHECK (editor.isSelected (noteAt (fixture, 3)));
    CHECK (fixture.roll.addNoteNearestToSelection (1000.0));
    CHECK (editor.isSelected (noteAt (fixture, 1)));
    CHECK (fixture.roll.addNoteNearestToSelection (1000.0));
    CHECK (editor.getNumSelected() == 4);

    CHECK_FALSE (fixture.roll.hasUnselectedNotes());
    CHECK_FALSE (fixture.roll.addNoteNearestToSelection (1000.0));   // every note selected
    CHECK (editor.getNumSelected() == 4);
    CHECK_FALSE (fixture.doc.getUndoManager().canUndo());
}

TEST_CASE ("PianoRollComponent: addNoteNearestToSelection keeps a selection made by clicking", "[piano-roll][note-nav]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 62, 64, 65 });
    auto& editor = Access::editor (fixture.roll);
    fixture.roll.selectNoteNearest (0.0);
    REQUIRE (editor.isSelected (noteAt (fixture, 0)));

    CHECK (fixture.roll.addNoteNearestToSelection (1500.0));
    CHECK (editor.getNumSelected() == 2);
    CHECK (editor.isSelected (noteAt (fixture, 0)));
    CHECK (editor.isSelected (noteAt (fixture, 3)));
}

TEST_CASE ("PianoRollComponent: addNoteNearestToSelection does nothing with no notes or on a Preview roll", "[piano-roll][note-nav]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture empty ({});
    CHECK_FALSE (empty.roll.hasUnselectedNotes());
    CHECK_FALSE (empty.roll.addNoteNearestToSelection (0.0));

    PianoRollComponent preview (PianoRollComponent::Role::Preview);
    CHECK_FALSE (preview.hasUnselectedNotes());
    CHECK_FALSE (preview.addNoteNearestToSelection (0.0));
}

namespace
{
    void addNoteToTrack (SourceRollFixture& f, int pitch, int startTick)
    {
        juce::ValueTree note (SongIDs::NOTE);
        note.setProperty (SongIDs::pitch, pitch, nullptr);
        note.setProperty (SongIDs::startTick, startTick, nullptr);
        note.setProperty (SongIDs::durationTicks, ticksPerQuarter, nullptr);
        SongDocument::getNotesNode (f.track).addChild (note, -1, nullptr);
    }
}

TEST_CASE ("PianoRollComponent: selectSteppedNote walks a chord lowest first, moves the active row, and returns each start", "[piano-roll][note-nav]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 72 });   // starts 0
    addNoteToTrack (fixture, 67, 480);
    addNoteToTrack (fixture, 60, 480);
    addNoteToTrack (fixture, 64, 480);
    auto& editor = Access::editor (fixture.roll);
    fixture.doc.getUndoManager().clearUndoHistory();

    CHECK (fixture.roll.hasSteppedNote (1.0, +1));
    auto start = fixture.roll.selectSteppedNote (1.0, +1);
    REQUIRE (start.has_value());
    CHECK (*start == 480);
    CHECK (editor.getNumSelected() == 1);
    CHECK (editor.isSelected (noteAt (fixture, 2)));   // 60, the chord's lowest
    CHECK (fixture.roll.getActivePitch() == 60);

    // The caller moves the cursor to the returned start; the selection anchors the next step.
    CHECK (fixture.roll.selectSteppedNote (480.0, +1) == std::optional<int> (480));
    CHECK (editor.isSelected (noteAt (fixture, 3)));   // 64
    CHECK (fixture.roll.selectSteppedNote (480.0, +1) == std::optional<int> (480));
    CHECK (editor.isSelected (noteAt (fixture, 1)));   // 67
    CHECK (fixture.roll.getActivePitch() == 67);
    CHECK_FALSE (fixture.roll.hasSteppedNote (480.0, +1));   // nothing after the chord

    CHECK (fixture.roll.selectSteppedNote (480.0, -1) == std::optional<int> (480));
    CHECK (editor.isSelected (noteAt (fixture, 3)));   // back to 64
    fixture.roll.selectSteppedNote (480.0, -1);
    CHECK (fixture.roll.selectSteppedNote (480.0, -1) == std::optional<int> (0));
    CHECK (editor.isSelected (noteAt (fixture, 0)));
    CHECK (editor.getNumSelected() == 1);
    CHECK_FALSE (fixture.doc.getUndoManager().canUndo());
}

TEST_CASE ("PianoRollComponent: selectSteppedNote past either end leaves the selection alone", "[piano-roll][note-nav]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 62 });   // starts 0, 480
    auto& editor = Access::editor (fixture.roll);
    REQUIRE (fixture.roll.selectSteppedNote (480.0, +1).has_value());
    REQUIRE (editor.isSelected (noteAt (fixture, 1)));

    CHECK_FALSE (fixture.roll.hasSteppedNote (480.0, +1));
    CHECK_FALSE (fixture.roll.selectSteppedNote (480.0, +1).has_value());
    CHECK_FALSE (fixture.roll.selectSteppedNote (0.0, -1).has_value());
    CHECK (editor.getNumSelected() == 1);
    CHECK (editor.isSelected (noteAt (fixture, 1)));
    CHECK (fixture.roll.getActivePitch() == 62);
}

TEST_CASE ("PianoRollComponent: selectSteppedNote does nothing with no notes or on a Preview roll", "[piano-roll][note-nav]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture empty ({});
    CHECK_FALSE (empty.roll.hasSteppedNote (0.0, +1));
    CHECK_FALSE (empty.roll.selectSteppedNote (0.0, +1).has_value());

    PianoRollComponent preview (PianoRollComponent::Role::Preview);
    CHECK_FALSE (preview.hasSteppedNote (0.0, +1));
    CHECK_FALSE (preview.selectSteppedNote (0.0, -1).has_value());
}

TEST_CASE ("PianoRollComponent: selectSamePitchNote steps the one selected note along its pitch, moves the active row, and returns each start", "[piano-roll][same-pitch]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 62, 60, 64, 60 });   // starts 0, 480, 960, 1440, 1920
    auto& editor = Access::editor (fixture.roll);
    fixture.doc.getUndoManager().clearUndoHistory();
    editor.selectOnly (noteAt (fixture, 0));
    Access::setActivePitch (fixture.roll, 70);

    CHECK_FALSE (fixture.roll.hasSamePitchNote (-1));
    CHECK (fixture.roll.hasSamePitchNote (+1));
    CHECK (fixture.roll.selectSamePitchNote (+1) == std::optional<int> (960));
    CHECK (editor.getNumSelected() == 1);
    CHECK (editor.isSelected (noteAt (fixture, 2)));
    CHECK (fixture.roll.getActivePitch() == 60);
    CHECK (fixture.roll.selectSamePitchNote (+1) == std::optional<int> (1920));
    CHECK (editor.isSelected (noteAt (fixture, 4)));
    CHECK_FALSE (fixture.roll.hasSamePitchNote (+1));
    CHECK_FALSE (fixture.roll.selectSamePitchNote (+1).has_value());   // past the end: unchanged
    CHECK (editor.isSelected (noteAt (fixture, 4)));
    CHECK (fixture.roll.selectSamePitchNote (-1) == std::optional<int> (960));
    CHECK (editor.isSelected (noteAt (fixture, 2)));
    CHECK (editor.getNumSelected() == 1);
    CHECK_FALSE (fixture.doc.getUndoManager().canUndo());
}

TEST_CASE ("PianoRollComponent: same-pitch commands do nothing unless exactly one note is selected", "[piano-roll][same-pitch]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 60, 60 });
    auto& editor = Access::editor (fixture.roll);

    CHECK_FALSE (fixture.roll.hasSamePitchNote (+1));   // nothing selected
    CHECK_FALSE (fixture.roll.canSelectAllOfSamePitch());
    CHECK_FALSE (fixture.roll.selectSamePitchNote (+1).has_value());
    CHECK_FALSE (fixture.roll.selectAllOfSamePitch());
    CHECK (editor.getNumSelected() == 0);

    editor.selectOnly (noteAt (fixture, 0));
    editor.addToSelection (noteAt (fixture, 1));
    CHECK_FALSE (fixture.roll.hasSamePitchNote (+1));   // two selected
    CHECK_FALSE (fixture.roll.hasSamePitchNote (-1));
    CHECK_FALSE (fixture.roll.canSelectAllOfSamePitch());
    CHECK_FALSE (fixture.roll.selectSamePitchNote (+1).has_value());
    CHECK_FALSE (fixture.roll.selectAllOfSamePitch());
    CHECK (editor.getNumSelected() == 2);
    CHECK_FALSE (editor.isSelected (noteAt (fixture, 2)));

    PianoRollComponent preview (PianoRollComponent::Role::Preview);
    CHECK_FALSE (preview.hasSamePitchNote (+1));
    CHECK_FALSE (preview.canSelectAllOfSamePitch());
    CHECK_FALSE (preview.selectAllOfSamePitch());
}

TEST_CASE ("PianoRollComponent: selectAllOfSamePitch selects every note of the selected note's pitch and leaves the view alone", "[piano-roll][same-pitch]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 62, 60, 64, 60 });
    for (int i = 0; i < 40; ++i)
        addNoteToTrack (fixture, 60, 20000 + i * 480);   // far off to the right
    fixture.roll.setNoteSource (fixture.source.get(), ticksPerQuarter, {});
    fixture.roll.setEditableTrack (fixture.track);
    auto& editor = Access::editor (fixture.roll);
    fixture.doc.getUndoManager().clearUndoHistory();

    editor.selectOnly (noteAt (fixture, 3));   // 64, the only one
    CHECK_FALSE (fixture.roll.canSelectAllOfSamePitch());
    CHECK_FALSE (fixture.roll.selectAllOfSamePitch());
    CHECK (editor.getNumSelected() == 1);

    editor.selectOnly (noteAt (fixture, 2));
    Access::setActivePitch (fixture.roll, 70);
    const auto viewBefore = Access::viewport (fixture.roll).getViewPosition();
    CHECK (fixture.roll.canSelectAllOfSamePitch());
    CHECK (fixture.roll.selectAllOfSamePitch());
    CHECK (editor.getNumSelected() == 43);
    CHECK_FALSE (editor.isSelected (noteAt (fixture, 1)));
    CHECK_FALSE (editor.isSelected (noteAt (fixture, 3)));
    CHECK (Access::viewport (fixture.roll).getViewPosition() == viewBefore);
    CHECK (fixture.roll.getActivePitch() == 70);
    CHECK_FALSE (fixture.doc.getUndoManager().canUndo());

    CHECK_FALSE (fixture.roll.canSelectAllOfSamePitch());   // many selected now
}

TEST_CASE ("PianoRollComponent: addSteppedNoteToSelection grows the selection at either end, moves the active row, and returns each start", "[piano-roll][extend-selection]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 62 });   // starts 0, 480
    addNoteToTrack (fixture, 67, 960);
    addNoteToTrack (fixture, 64, 960);
    addNoteToTrack (fixture, 55, 1440);
    auto& editor = Access::editor (fixture.roll);
    fixture.doc.getUndoManager().clearUndoHistory();
    editor.selectOnly (noteAt (fixture, 1));   // 62 at 480

    CHECK (fixture.roll.hasExtendableNote (-1));
    CHECK (fixture.roll.addSteppedNoteToSelection (+1) == std::optional<int> (960));
    CHECK (editor.isSelected (noteAt (fixture, 3)));   // 64, the chord's lowest
    CHECK (fixture.roll.getActivePitch() == 64);
    CHECK (fixture.roll.addSteppedNoteToSelection (+1) == std::optional<int> (960));
    CHECK (editor.isSelected (noteAt (fixture, 2)));   // 67
    CHECK (fixture.roll.addSteppedNoteToSelection (+1) == std::optional<int> (1440));
    CHECK_FALSE (fixture.roll.hasExtendableNote (+1));
    CHECK_FALSE (fixture.roll.addSteppedNoteToSelection (+1).has_value());
    CHECK (editor.getNumSelected() == 4);

    CHECK (fixture.roll.addSteppedNoteToSelection (-1) == std::optional<int> (0));
    CHECK (editor.getNumSelected() == 5);
    CHECK (fixture.roll.getActivePitch() == 60);
    CHECK_FALSE (fixture.roll.hasExtendableNote (-1));
    CHECK_FALSE (fixture.doc.getUndoManager().canUndo());
}

TEST_CASE ("PianoRollComponent: addSteppedNoteToSelection and selectionStart need a selection", "[piano-roll][extend-selection]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 62 });
    CHECK_FALSE (fixture.roll.hasExtendableNote (+1));
    CHECK_FALSE (fixture.roll.addSteppedNoteToSelection (+1).has_value());
    CHECK_FALSE (fixture.roll.selectionStart().has_value());
    CHECK (Access::editor (fixture.roll).getNumSelected() == 0);

    PianoRollComponent preview (PianoRollComponent::Role::Preview);
    CHECK_FALSE (preview.hasExtendableNote (+1));
    CHECK_FALSE (preview.addSteppedNoteToSelection (-1).has_value());
    CHECK_FALSE (preview.selectionStart().has_value());
}

TEST_CASE ("PianoRollComponent: selectionStart is the earliest selected start, and scrollTickIntoView brings it on screen", "[piano-roll][extend-selection]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 62, 64 });   // starts 0, 480, 960
    addNoteToTrack (fixture, 60, 40000);
    fixture.roll.setNoteSource (fixture.source.get(), ticksPerQuarter, {});
    fixture.roll.setEditableTrack (fixture.track);
    auto& editor = Access::editor (fixture.roll);
    editor.selectOnly (noteAt (fixture, 2));
    editor.addToSelection (noteAt (fixture, 1));
    CHECK (fixture.roll.selectionStart() == std::optional<int> (480));

    editor.selectOnly (noteAt (fixture, 3));
    CHECK (fixture.roll.selectionStart() == std::optional<int> (40000));
    for (int i = 0; i < 15; ++i)
        Access::zoom (fixture.roll, 1.0f);   // fitted, the whole track would be on screen
    auto& viewport = Access::viewport (fixture.roll);
    REQUIRE (fixture.roll.xForTickInComponent (40000.0) > viewport.getMaximumVisibleWidth());
    const int yBefore = viewport.getViewPositionY();
    fixture.roll.scrollTickIntoView (40000.0);
    const int x = fixture.roll.xForTickInComponent (40000.0);
    CHECK (x >= Access::geometry (fixture.roll).getKeyboardGutterWidth());
    CHECK (x < viewport.getMaximumVisibleWidth());
    CHECK (viewport.getViewPositionY() == yBefore);   // time only
}

TEST_CASE ("PianoRollComponent: a right-click on a keyboard key selects every note of that semitone; shift adds", "[piano-roll][pitch-select]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 64, 60 });
    auto& editor = Access::editor (fixture.roll);
    const juce::ModifierKeys right (juce::ModifierKeys::rightButtonModifier);
    const int keyX = Access::viewport (fixture.roll).getViewPositionX() + 5;

    Access::mouseDown (fixture.roll, canvasRowPoint (fixture.roll, keyX, 60), right, false);
    CHECK (editor.getNumSelected() == 2);   // on the press
    CHECK (editor.isSelected (noteAt (fixture, 0)));
    CHECK (editor.isSelected (noteAt (fixture, 2)));
    CHECK (fixture.roll.getActivePitch() == 60);
    Access::mouseUp (fixture.roll, canvasRowPoint (fixture.roll, keyX, 60));
    CHECK (editor.getNumSelected() == 2);

    Access::mouseDown (fixture.roll, canvasRowPoint (fixture.roll, keyX, 64),
                       juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier | juce::ModifierKeys::shiftModifier), false);
    Access::mouseUp (fixture.roll, canvasRowPoint (fixture.roll, keyX, 64));
    CHECK (editor.getNumSelected() == 3);

    // A plain right-click on a key with no notes clears it; a drag from a key draws no lasso.
    Access::mouseDown (fixture.roll, canvasRowPoint (fixture.roll, keyX, 70), right, false);
    Access::mouseDrag (fixture.roll, canvasRowPoint (fixture.roll, keyX + 300, 55));
    CHECK (editor.getRubberBandRect().isEmpty());
    Access::mouseUp (fixture.roll, canvasRowPoint (fixture.roll, keyX + 300, 55));
    CHECK (editor.getNumSelected() == 0);
}

TEST_CASE ("PianoRollComponent: the keyboard is found by where it sits on screen, after a horizontal scroll too", "[piano-roll][pitch-select]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 64, 60 });
    auto& editor = Access::editor (fixture.roll);
    const juce::ModifierKeys right (juce::ModifierKeys::rightButtonModifier);
    for (int i = 0; i < 15; ++i)
        Access::zoom (fixture.roll, 1.0f);
    auto& viewport = Access::viewport (fixture.roll);
    viewport.setViewPosition (300, viewport.getViewPositionY());
    const int scrollX = viewport.getViewPositionX();
    REQUIRE (scrollX > Access::geometry (fixture.roll).getKeyboardGutterWidth());

    // Canvas x 5 is scrolled off to the left: that is the roll, not the keyboard.
    Access::mouseDown (fixture.roll, canvasRowPoint (fixture.roll, 5, 60), right, false);
    Access::mouseUp (fixture.roll, canvasRowPoint (fixture.roll, 5, 60));
    CHECK (editor.getNumSelected() == 0);

    Access::mouseDown (fixture.roll, canvasRowPoint (fixture.roll, scrollX + 5, 60), right, false);
    Access::mouseUp (fixture.roll, canvasRowPoint (fixture.roll, scrollX + 5, 60));
    CHECK (editor.getNumSelected() == 2);
}

TEST_CASE ("PianoRollComponent: a right-click on the roll itself selects nothing in its row", "[piano-roll][pitch-select]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 64, 60 });
    auto& editor = Access::editor (fixture.roll);
    const juce::ModifierKeys right (juce::ModifierKeys::rightButtonModifier);
    const auto& geometry = Access::geometry (fixture.roll);

    for (const int x : { geometry.xForTick (ticksPerQuarter * 6), geometry.xForTick (ticksPerQuarter / 2) })   // empty, on a C
    {
        Access::mouseDown (fixture.roll, canvasRowPoint (fixture.roll, x, 60), right, false);
        Access::mouseUp (fixture.roll, canvasRowPoint (fixture.roll, x, 60));
        CHECK (editor.getNumSelected() == 0);
        CHECK (fixture.roll.getActivePitch() == 60);
    }
}

namespace
{
    // Paints the roll's canvas under `clip` into a canvas-sized image (black ground).
    juce::Image paintCanvasClipped (PianoRollComponent& roll, juce::Rectangle<int> clip)
    {
        auto& canvas = Access::canvas (roll);
        juce::Image image (juce::Image::ARGB, canvas.getWidth(), canvas.getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g (image);
        g.fillAll (juce::Colours::black);
        g.reduceClipRegion (clip);
        Access::paintCanvas (roll, g, clip);
        return image;
    }
}

TEST_CASE ("PianoRollComponent: a partial repaint covering only a note's glow still draws the glow", "[piano-roll][theme]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60 });
    fixture.track.setProperty (SongIDs::colorArgb, (int) 0xFFE0E0E0, nullptr);   // bright, opaque
    const auto bounds = Access::geometry (fixture.roll).noteBounds (fixture.source->getNote (0));
    REQUIRE (bounds.height >= ThemePaint::glowMinHeight);
    REQUIRE (bounds.y >= ThemePaint::noteOverhang);   // else the strip above the note is off-canvas

    // Clip: the overhang rows directly above the note, nothing of the note itself.
    const juce::Rectangle<int> glowStrip (bounds.x, bounds.y - ThemePaint::noteOverhang,
                                          bounds.width, ThemePaint::noteOverhang);
    const juce::Point<int> sample (bounds.x + bounds.width / 2 + 3, bounds.y - 1);
    const auto withNote = paintCanvasClipped (fixture.roll, glowStrip).getPixelAt (sample.x, sample.y);

    // The same paint with the note invisible: same bands and grid lines, no glow.
    fixture.track.setProperty (SongIDs::colorArgb, 0, nullptr);
    const auto withoutNote = paintCanvasClipped (fixture.roll, glowStrip).getPixelAt (sample.x, sample.y);
    CHECK (withNote.getBrightness() > withoutNote.getBrightness());
}

TEST_CASE ("PianoRollComponent: notes paint in their track's stored colour", "[piano-roll][theme]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60 });
    constexpr juce::uint32 legacy = 0xFF7FA8D0;   // an old-palette colour as saved in an existing song
    fixture.track.setProperty (SongIDs::colorArgb, (int) legacy, nullptr);
    const auto bounds = Access::geometry (fixture.roll).noteBounds (fixture.source->getNote (0));
    const auto image = paintCanvasClipped (fixture.roll, Access::canvas (fixture.roll).getLocalBounds());
    const auto centre = image.getPixelAt (bounds.x + bounds.width / 2, bounds.y + bounds.height / 2);
    const juce::Colour stored (legacy);
    CHECK (std::abs (centre.getRed() - stored.getRed()) <= 16);
    CHECK (std::abs (centre.getGreen() - stored.getGreen()) <= 16);
    CHECK (std::abs (centre.getBlue() - stored.getBlue()) <= 16);
}

TEST_CASE ("PianoRollComponent: white keys shade toward the right edge; black keys are glossy", "[piano-roll][keyboard][theme]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SourceRollFixture fixture ({ 60, 64 });
    const auto image = paintKeyboard (fixture.roll);
    const int width = Access::gutter (fixture.roll).getWidth();
    const int rowHeight = Access::geometry (fixture.roll).getRowHeight();
    const int whiteY = keyTopY (fixture.roll, 62) + 2;
    CHECK (image.getPixelAt (2, whiteY).getBrightness() > image.getPixelAt (width - 3, whiteY).getBrightness());
    const int blackTop = keyTopY (fixture.roll, 61);
    CHECK (image.getPixelAt (3, blackTop + rowHeight - 2).getBrightness()
           > image.getPixelAt (3, blackTop + 2).getBrightness());
}

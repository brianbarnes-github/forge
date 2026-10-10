// Verifies PianoRollComponent's Preview-role note border colours: a Normal
// note borders with SongsmithColours::previewNoteBorder, and a
// WillFold/Dropped note borders with SongsmithColours::outOfRangeBorder —
// not the generic fill.brighter(0.4f) used by Role::Source.

#include "PlaybackTestSupport.h"
#include "UI/PianoRollComponent.h"
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
        static bool mouseDrag (PianoRollComponent& c, juce::Point<int> pos) { return c.handleEditorMouseDrag (pos); }
        static bool mouseUp (PianoRollComponent& c, juce::Point<int> pos) { return c.handleEditorMouseUp (pos); }

        // Exposes the private Canvas member itself so tests can drive its
        // real JUCE mouseDown/mouseDoubleClick/mouseDrag/mouseUp/keyPressed
        // overrides directly, instead of only the handleEditor* forwarding
        // methods those overrides call into.
        static PianoRollComponent::Canvas& canvas (PianoRollComponent& c) { return c.canvas; }

        static const PianoRollGeometry& geometry (const PianoRollComponent& c) { return c.geometry; }
        static juce::Viewport& viewport (PianoRollComponent& c) { return c.viewport; }
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
        rg.setColour (juce::Colour (SongsmithColours::accentAmber).withAlpha (0.7f));
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
        rg.setColour (juce::Colour (SongsmithColours::accentAmber).withAlpha (0.7f));
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
    // accentAmber@0.25 onto that colour and comparing proves the ghost
    // renders as a translucent overlay, not a fully opaque note fill.
    const auto underlyingBeneathGhost = image.getPixelAt (sampleX, ghostY + rowHeight);
    juce::Image reference (juce::Image::ARGB, 1, 1, true, juce::SoftwareImageType());
    {
        juce::Graphics rg (reference);
        rg.fillAll (underlyingBeneathGhost);
        rg.setColour (juce::Colour (SongsmithColours::accentAmber).withAlpha (0.25f));
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
    // accentAmber while the untouched row beneath it would not -- so the
    // two must be pixel-identical when the gate correctly suppresses ghost
    // rendering for this role.
    const auto ghostPixel = image.getPixelAt (sampleX, ghostY);
    const auto belowPixel = image.getPixelAt (sampleX, ghostY + rowHeight);
    CHECK (ghostPixel == belowPixel);
}

namespace
{
    // A Source-role roll over one MIDI_TRACK, sized and pointed at the track
    // the way TrackEditorWindow does it (bounds first, then setNoteSource).
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
        const auto face = image.getPixelAt (image.getWidth() - 3, top + 1);
        int marked = 0;
        for (int y = top + 2; y < top + rowHeight - 2; ++y)
            for (int x = blackKeyRight + 1; x < image.getWidth() - 2; ++x)
                if (image.getPixelAt (x, y) != face)
                    ++marked;
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

    // A drag (rubber band across empty space): no marker.
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

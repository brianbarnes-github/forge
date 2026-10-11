// Tests/TrackNotePreview_tests.cpp
#include "UI/TrackNotePreview.h"
#include "UI/SongDocument.h"
#include "UI/SongsmithColours.h"
#include "UI/SectionEdit.h"
#include "UI/SectionViewState.h"
#include "PlaybackTestSupport.h"
#include "MidiTestBytes.h"
#include "UI/SongModelBridge.h"
#include "UI/TempoEdit.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace
{
    constexpr int previewWidth = 300;
    constexpr int previewHeight = 34;
}

TEST_CASE ("TrackNotePreview: paints a note as a bar at its mapped tick position, in the track's colour", "[track-note-preview]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    constexpr juce::uint32 trackColour = 0xFF80C878;
    juce::ValueTree track (SongIDs::MIDI_TRACK);
    track.setProperty (SongIDs::colorArgb, (int) trackColour, nullptr);
    juce::ValueTree note (SongIDs::NOTE);
    note.setProperty (SongIDs::pitch, 60, nullptr);
    note.setProperty (SongIDs::startTick, 0, nullptr);
    note.setProperty (SongIDs::durationTicks, 480, nullptr);
    juce::ValueTree notes (SongIDs::NOTES);
    notes.appendChild (note, nullptr);
    track.appendChild (notes, nullptr);

    TimelineViewState viewState;
    viewState.setPixelsPerTick (0.1);
    viewState.setScrollOffsetTicks (0.0);

    TrackNotePreview preview (track, viewState);
    preview.setBounds (0, 0, previewWidth, previewHeight);

    juce::Image image (juce::Image::ARGB, previewWidth, previewHeight, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    preview.paint (g);

    const auto expectedX = viewState.xForTick (0);
    const auto pixel = image.getPixelAt (expectedX + 1, previewHeight - 2);
    CHECK (pixel == juce::Colour (trackColour));
}

TEST_CASE ("TrackNotePreview: an empty track paints only the background, no note bars", "[track-note-preview]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::ValueTree track (SongIDs::MIDI_TRACK);
    TimelineViewState viewState;
    viewState.setPixelsPerTick (0.1);

    TrackNotePreview preview (track, viewState);
    preview.setBounds (0, 0, previewWidth, previewHeight);

    juce::Image image (juce::Image::ARGB, previewWidth, previewHeight, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    preview.paint (g);

    CHECK (image.getPixelAt (previewWidth / 2, previewHeight / 2) == juce::Colour (SongsmithColours::background));
}

TEST_CASE ("TrackNotePreview: toggleGhostIfHit flips visibility only inside the toggle bounds and fires onGhostToggled", "[track-note-preview]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::ValueTree track (SongIDs::MIDI_TRACK);
    TimelineViewState viewState;

    TrackNotePreview preview (track, viewState);
    preview.setBounds (0, 0, previewWidth, previewHeight);

    bool fired = false;
    bool firedState = false;
    preview.onGhostToggled = [&] (bool visible) { fired = true; firedState = visible; };

    CHECK_FALSE (preview.toggleGhostIfHit ({ 0, 0 }));
    CHECK_FALSE (fired);
    CHECK_FALSE (preview.isGhostVisible());

    const auto toggle = preview.ghostToggleBounds();
    CHECK (preview.toggleGhostIfHit (toggle.getCentre()));
    CHECK (fired);
    CHECK (firedState);
    CHECK (preview.isGhostVisible());
}

TEST_CASE ("TrackNotePreview: draws ghost toggle as filled amber when visible, outlined muted when invisible", "[track-note-preview]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::ValueTree track (SongIDs::MIDI_TRACK);
    TimelineViewState viewState;
    viewState.setPixelsPerTick (0.1);

    TrackNotePreview preview (track, viewState);
    preview.setBounds (0, 0, previewWidth, previewHeight);

    const auto toggleBounds = preview.ghostToggleBounds();
    const auto toggleCentre = toggleBounds.getCentre();

    // When invisible (default), draw outlined toggle
    juce::Image imageInvisible (juce::Image::ARGB, previewWidth, previewHeight, true, juce::SoftwareImageType());
    juce::Graphics gInvisible (imageInvisible);
    preview.paint (gInvisible);
    auto pixelAtCentreInvisible = imageInvisible.getPixelAt (toggleCentre.x, toggleCentre.y);
    CHECK (pixelAtCentreInvisible != juce::Colour (SongsmithColours::accentAmber));

    // When visible, draw filled toggle
    preview.setGhostVisible (true);
    juce::Image imageVisible (juce::Image::ARGB, previewWidth, previewHeight, true, juce::SoftwareImageType());
    juce::Graphics gVisible (imageVisible);
    preview.paint (gVisible);
    auto pixelAtCentreVisible = imageVisible.getPixelAt (toggleCentre.x, toggleCentre.y);
    CHECK (pixelAtCentreVisible == juce::Colour (SongsmithColours::accentAmber));
}

namespace
{
    // The pixel at the grid line for `tick` differs from the empty pixel beside it.
    bool gridLineDrawnAt (juce::Image& image, const TimelineViewState& viewState, int tick)
    {
        const int x = viewState.xForTick (tick);
        return image.getPixelAt (x, previewHeight / 2) != image.getPixelAt (x + 1, previewHeight / 2);
    }
}

TEST_CASE ("TrackNotePreview: grid lines appear for each note value once it is at least 8 px wide", "[track-note-preview]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::ValueTree track (SongIDs::MIDI_TRACK);
    TimelineViewState viewState;
    viewState.setPixelsPerTick (0.1);   // 480-tick quarter = 48 px; 1/16 = 12 px; 1/32 = 6 px

    TrackNotePreview preview (track, viewState);
    preview.setBounds (0, 0, previewWidth, previewHeight);

    juce::Image image (juce::Image::ARGB, previewWidth, previewHeight, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    preview.paint (g);

    CHECK (gridLineDrawnAt (image, viewState, 480));        // quarter
    CHECK (gridLineDrawnAt (image, viewState, 240));        // eighth
    CHECK (gridLineDrawnAt (image, viewState, 120));        // sixteenth
    CHECK_FALSE (gridLineDrawnAt (image, viewState, 60));   // thirty-second: too close
}

TEST_CASE ("TrackNotePreview: zoomed far in the grid goes down to 1/64", "[track-note-preview]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::ValueTree track (SongIDs::MIDI_TRACK);
    TimelineViewState viewState;
    viewState.setPixelsPerTick (1.0);   // 1/64 = 30 ticks = 30 px

    TrackNotePreview preview (track, viewState);
    preview.setBounds (0, 0, previewWidth, previewHeight);

    juce::Image image (juce::Image::ARGB, previewWidth, previewHeight, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    preview.paint (g);

    CHECK (gridLineDrawnAt (image, viewState, 30));
    CHECK (gridLineDrawnAt (image, viewState, 60));
}

TEST_CASE ("TrackNotePreview: a finer division is fainter than a bar line", "[track-note-preview]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::ValueTree track (SongIDs::MIDI_TRACK);
    TimelineViewState viewState;
    viewState.setPixelsPerTick (0.1);

    TrackNotePreview preview (track, viewState);
    preview.setBounds (0, 0, previewWidth, previewHeight);

    juce::Image image (juce::Image::ARGB, previewWidth, previewHeight, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    preview.paint (g);

    const auto barPixel = image.getPixelAt (viewState.xForTick (1920), previewHeight / 2);
    const auto sixteenthPixel = image.getPixelAt (viewState.xForTick (120), previewHeight / 2);
    CHECK (barPixel.getBrightness() > sixteenthPixel.getBrightness());
}

namespace
{
    juce::MouseEvent previewMouseAt (juce::Component& c, int x, int y = 20, juce::ModifierKeys mods = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier))
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                 juce::Point<float> ((float) x, (float) y), mods,
                                 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, juce::Time::getCurrentTime(),
                                 juce::Point<float> ((float) x, (float) y), juce::Time::getCurrentTime(), 1, false);
    }
}

TEST_CASE ("TrackNotePreview: sections are drawn as blocks with edges, selected ones highlighted", "[track-note-preview][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = playbacktest::addTrack (doc);
    playbacktest::addNote (track, 60, 0, 960);
    splitAt (doc, { (juce::int64) track.getProperty (SongIDs::trackId) }, 480);
    const auto sections = sectionsOf (track);
    REQUIRE (sections.size() == 2);

    TimelineViewState viewState;
    viewState.setPixelsPerTick (0.1);
    SectionViewState sectionView;
    sectionView.selected.insert ({ (juce::int64) track.getProperty (SongIDs::trackId), sections[1].id });

    TrackNotePreview preview (track, viewState);
    preview.setSectionView (&sectionView, (juce::int64) track.getProperty (SongIDs::trackId));
    preview.setBounds (0, 0, previewWidth, previewHeight);

    juce::Image image (juce::Image::ARGB, previewWidth, previewHeight, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    preview.paint (g);

    const int y = previewHeight - 4;   // below the note line
    const auto unselected = image.getPixelAt (viewState.xForTick (240), y);
    const auto selected = image.getPixelAt (viewState.xForTick (720), y);
    CHECK (selected != unselected);                                   // highlighted
    CHECK (unselected != juce::Colour (SongsmithColours::background)); // a block is drawn
    CHECK (gridLineDrawnAt (image, viewState, 480));                  // a visible edge at the split
}

TEST_CASE ("TrackNotePreview: with no section view nothing section-related is drawn", "[track-note-preview][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::ValueTree track (SongIDs::MIDI_TRACK);
    TimelineViewState viewState;
    viewState.setPixelsPerTick (0.1);
    TrackNotePreview preview (track, viewState);
    preview.setBounds (0, 0, previewWidth, previewHeight);

    juce::Image image (juce::Image::ARGB, previewWidth, previewHeight, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    preview.paint (g);
    CHECK (image.getPixelAt (50, previewHeight - 4) == juce::Colour (SongsmithColours::background));
}

TEST_CASE ("TrackNotePreview: pressing in a section reports the hit and the tick", "[track-note-preview][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = playbacktest::addTrack (doc);
    playbacktest::addNote (track, 60, 0, 1920);
    TimelineViewState viewState;
    viewState.setPixelsPerTick (0.1);
    SectionViewState sectionView;
    TrackNotePreview preview (track, viewState);
    preview.setSectionView (&sectionView, (juce::int64) track.getProperty (SongIDs::trackId));
    preview.setBounds (0, 0, previewWidth, previewHeight);

    SectionHit hit;
    int pressedTick = -1;
    preview.onSectionPressed = [&] (const SectionHit& h, int tick, const juce::ModifierKeys&) { hit = h; pressedTick = tick; };

    const int x = viewState.xForTick (960);
    preview.mouseDown (previewMouseAt (preview, x));

    CHECK (hit.zone == SectionZone::Body);
    CHECK (pressedTick == 960);
}

TEST_CASE ("TrackNotePreview: a section edge off the grid is drawn, and a block fills an off-grid pixel", "[track-note-preview][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = playbacktest::addTrack (doc);
    playbacktest::addNote (track, 60, 0, 960);
    splitAt (doc, { (juce::int64) track.getProperty (SongIDs::trackId) }, 530);   // x = 53: no grid line there

    TimelineViewState viewState;
    viewState.setPixelsPerTick (0.1);
    SectionViewState sectionView;
    TrackNotePreview preview (track, viewState);
    preview.setBounds (0, 0, previewWidth, previewHeight);

    juce::Image bare (juce::Image::ARGB, previewWidth, previewHeight, true, juce::SoftwareImageType());
    {
        juce::Graphics g (bare);
        preview.paint (g);
    }
    preview.setSectionView (&sectionView, (juce::int64) track.getProperty (SongIDs::trackId));
    juce::Image image (juce::Image::ARGB, previewWidth, previewHeight, true, juce::SoftwareImageType());
    {
        juce::Graphics g (image);
        preview.paint (g);
    }

    CHECK_FALSE (gridLineDrawnAt (bare, viewState, 530));
    CHECK (gridLineDrawnAt (image, viewState, 530));
    CHECK (bare.getPixelAt (26, previewHeight - 4) == juce::Colour (SongsmithColours::background));
    CHECK (image.getPixelAt (26, previewHeight - 4) != juce::Colour (SongsmithColours::background));
}

TEST_CASE ("TrackNotePreview: a drag preview moves only the selected block, clamped like the commit", "[track-note-preview][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = playbacktest::addTrack (doc);
    const auto id = (juce::int64) track.getProperty (SongIDs::trackId);
    playbacktest::addNote (track, 60, 0, 1500);
    splitAt (doc, { id }, 530);
    const auto sections = sectionsOf (track);   // [0, 530) and [530, 1500)

    TimelineViewState viewState;
    viewState.setPixelsPerTick (0.1);
    SectionViewState sectionView;
    sectionView.selected.insert ({ id, sections[1].id });
    TrackNotePreview preview (track, viewState);
    preview.setSectionView (&sectionView, id);
    preview.setBounds (0, 0, previewWidth, previewHeight);

    const auto paintAt = [&] (int x)
    {
        juce::Image image (juce::Image::ARGB, previewWidth, previewHeight, true, juce::SoftwareImageType());
        juce::Graphics g (image);
        preview.paint (g);
        return image.getPixelAt (x, previewHeight - 4);
    };
    const auto selectedFill = paintAt (100);   // x = 100: inside the selected block, off the grid
    const auto plainFill = paintAt (26);

    sectionView.drag = SectionDragPreview { SectionDragPreview::Kind::Move, 200 };   // [730, 1700)
    const auto background = juce::Colour (SongsmithColours::background);
    CHECK (paintAt (63) == background);      // the gap it left is empty
    CHECK (paintAt (77) == selectedFill);
    CHECK (paintAt (26) == plainFill);       // the unselected block stays put

    sectionView.drag = SectionDragPreview { SectionDragPreview::Kind::ResizeRight, -1400 };   // past the start: 1 tick
    CHECK (paintAt (63) == background);
    CHECK (paintAt (100) == background);

    sectionView.drag = SectionDragPreview { SectionDragPreview::Kind::ResizeLeft, 1470 };   // past the end: [1499, 1500)
    CHECK (paintAt (100) == background);
    CHECK (paintAt (200) == background);   // nothing drawn out at the pointer
}

TEST_CASE ("TrackNotePreview: only a left press picks up a section", "[track-note-preview][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = playbacktest::addTrack (doc);
    playbacktest::addNote (track, 60, 0, 1920);
    TimelineViewState viewState;
    viewState.setPixelsPerTick (0.1);
    SectionViewState sectionView;
    TrackNotePreview preview (track, viewState);
    preview.setSectionView (&sectionView, (juce::int64) track.getProperty (SongIDs::trackId));
    preview.setBounds (0, 0, previewWidth, previewHeight);

    int pressed = 0, dragged = 0, released = 0, markerTick = -1;
    preview.onSectionPressed = [&] (const SectionHit&, int, const juce::ModifierKeys&) { ++pressed; };
    preview.onSectionDragged = [&] (int) { ++dragged; };
    preview.onSectionReleased = [&] (int) { ++released; };
    preview.onTimelineClicked = [&] (int tick) { markerTick = tick; };

    const juce::ModifierKeys right (juce::ModifierKeys::rightButtonModifier);
    preview.mouseDown (previewMouseAt (preview, 96, 20, right));
    preview.mouseDrag (previewMouseAt (preview, 130, 20, right));
    preview.mouseUp (previewMouseAt (preview, 130, 20, right));
    CHECK (markerTick == 960);   // the marker still moves
    CHECK (pressed == 0);
    CHECK (dragged == 0);
    CHECK (released == 0);

    preview.mouseUp (previewMouseAt (preview, 130));   // a stray mouse-up with no press
    CHECK (released == 0);

    preview.mouseDown (previewMouseAt (preview, 96));
    preview.mouseDrag (previewMouseAt (preview, 130));
    preview.mouseUp (previewMouseAt (preview, 130));
    CHECK (pressed == 1);
    CHECK (dragged == 1);
    CHECK (released == 1);
}

TEST_CASE ("TrackNotePreview: an empty track and the conductor paint no sections and press as None", "[track-note-preview][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto empty = playbacktest::addTrack (doc, "E");
    auto conductor = playbacktest::addTrack (doc, "C");
    playbacktest::addNote (conductor, 60, 0, 1920);
    conductor.setProperty (SongIDs::isConductor, true, nullptr);

    for (auto track : { empty, conductor })
    {
        TimelineViewState viewState;
        viewState.setPixelsPerTick (0.1);
        SectionViewState sectionView;
        TrackNotePreview preview (track, viewState);
        preview.setSectionView (&sectionView, (juce::int64) track.getProperty (SongIDs::trackId));
        preview.setBounds (0, 0, previewWidth, previewHeight);

        juce::Image image (juce::Image::ARGB, previewWidth, previewHeight, true, juce::SoftwareImageType());
        {
            juce::Graphics g (image);
            preview.paint (g);
        }
        CHECK (image.getPixelAt (26, previewHeight - 4) == juce::Colour (SongsmithColours::background));

        SectionHit hit { 99, SectionZone::Body };
        preview.onSectionPressed = [&] (const SectionHit& h, int, const juce::ModifierKeys&) { hit = h; };
        preview.mouseDown (previewMouseAt (preview, 96));
        CHECK (hit.zone == SectionZone::None);
    }
}

TEST_CASE ("TrackNotePreview: the section press reports the click's modifier keys", "[track-note-preview][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = playbacktest::addTrack (doc);
    playbacktest::addNote (track, 60, 0, 1920);
    TimelineViewState viewState;
    viewState.setPixelsPerTick (0.1);
    SectionViewState sectionView;
    TrackNotePreview preview (track, viewState);
    preview.setSectionView (&sectionView, (juce::int64) track.getProperty (SongIDs::trackId));
    preview.setBounds (0, 0, previewWidth, previewHeight);

    juce::ModifierKeys seen;
    preview.onSectionPressed = [&] (const SectionHit&, int, const juce::ModifierKeys& m) { seen = m; };
    preview.mouseDown (previewMouseAt (preview, viewState.xForTick (960), 20,
                                       juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier)));
    CHECK (seen.isShiftDown());
    CHECK_FALSE (seen.isCtrlDown());
    preview.mouseDown (previewMouseAt (preview, viewState.xForTick (960)));
    CHECK_FALSE (seen.isShiftDown());
}

TEST_CASE ("TrackNotePreview: a resize preview moves each selected edge by the delta, not to one tick", "[track-note-preview][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = playbacktest::addTrack (doc);
    const auto id = (juce::int64) track.getProperty (SongIDs::trackId);
    playbacktest::addNote (track, 60, 0, 5000);   // virtual [0, 5000)

    TimelineViewState viewState;
    viewState.setPixelsPerTick (0.05);   // 5000 ticks = 250 px
    SectionViewState sectionView;
    sectionView.selected.insert ({ id, 0 });
    TrackNotePreview preview (track, viewState);
    preview.setSectionView (&sectionView, id);
    preview.setBounds (0, 0, previewWidth, previewHeight);

    // Dragged from another track's edge at 960 to -1040: this section ends at 3000.
    sectionView.drag = SectionDragPreview { SectionDragPreview::Kind::ResizeRight, -2000 };
    juce::Image image (juce::Image::ARGB, previewWidth, previewHeight, true, juce::SoftwareImageType());
    {
        juce::Graphics g (image);
        preview.paint (g);
    }
    const auto background = juce::Colour (SongsmithColours::background);
    CHECK (image.getPixelAt (101, previewHeight - 4) != background);   // tick 2020: still inside
    CHECK (image.getPixelAt (171, previewHeight - 4) == background);   // tick 3420: given up
}

namespace
{
    constexpr juce::uint32 dragTrackColour = 0xFF80C878;
    constexpr juce::int64 dragTrackId = 7;

    // One note at tick 0, 480 long, on the track's virtual (id 0) section.
    juce::ValueTree makeTrackWithNoteAtZero()
    {
        juce::ValueTree track (SongIDs::MIDI_TRACK);
        track.setProperty (SongIDs::colorArgb, (int) dragTrackColour, nullptr);
        juce::ValueTree note (SongIDs::NOTE);
        note.setProperty (SongIDs::pitch, 60, nullptr);
        note.setProperty (SongIDs::startTick, 0, nullptr);
        note.setProperty (SongIDs::durationTicks, 480, nullptr);
        juce::ValueTree notes (SongIDs::NOTES);
        notes.appendChild (note, nullptr);
        track.appendChild (notes, nullptr);
        return track;
    }

    // Paints the preview and returns whether the note bar's colour is at `tick`.
    bool noteBarAt (juce::ValueTree track, const SectionViewState& state, int tick)
    {
        TimelineViewState viewState;
        viewState.setPixelsPerTick (0.1);
        viewState.setScrollOffsetTicks (0.0);
        TrackNotePreview preview (track, viewState);
        preview.setBounds (0, 0, previewWidth, previewHeight);
        preview.setSectionView (&state, dragTrackId);

        juce::Image image (juce::Image::ARGB, previewWidth, previewHeight, true, juce::SoftwareImageType());
        juce::Graphics g (image);
        preview.paint (g);
        return image.getPixelAt (viewState.xForTick (tick) + 1, previewHeight - 2) == juce::Colour (dragTrackColour);
    }
}

TEST_CASE ("TrackNotePreview: a Move drag draws the selected section's notes at the dragged position", "[track-note-preview][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto track = makeTrackWithNoteAtZero();

    SectionViewState state;
    state.selected.insert ({ dragTrackId, 0 });
    state.drag = SectionDragPreview { SectionDragPreview::Kind::Move, 960 };

    CHECK (noteBarAt (track, state, 960));        // moved with its section
    CHECK_FALSE (noteBarAt (track, state, 0));    // and gone from where it was
}

TEST_CASE ("TrackNotePreview: notes stay put for an unselected section, a resize drag, or no drag", "[track-note-preview][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto track = makeTrackWithNoteAtZero();

    SectionViewState unselected;
    unselected.drag = SectionDragPreview { SectionDragPreview::Kind::Move, 960 };
    CHECK (noteBarAt (track, unselected, 0));

    SectionViewState resizing;
    resizing.selected.insert ({ dragTrackId, 0 });
    resizing.drag = SectionDragPreview { SectionDragPreview::Kind::ResizeRight, 960 };
    CHECK (noteBarAt (track, resizing, 0));

    SectionViewState idle;
    idle.selected.insert ({ dragTrackId, 0 });
    CHECK (noteBarAt (track, idle, 0));
}

TEST_CASE ("TrackNotePreview: an Alt press starts a merge instead of a section gesture", "[track-note-preview][merge]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = playbacktest::addTrack (doc);
    playbacktest::addNote (track, 60, 0, 960);
    TimelineViewState viewState;
    viewState.setPixelsPerTick (0.1);
    SectionViewState sectionView;
    TrackNotePreview preview (track, viewState);
    preview.setSectionView (&sectionView, (juce::int64) track.getProperty (SongIDs::trackId));
    preview.setBounds (0, 0, previewWidth, previewHeight);

    int sectionPresses = 0, mergePresses = 0, mergeDrags = 0, mergeReleases = 0;
    preview.onSectionPressed = [&] (const SectionHit&, int, const juce::ModifierKeys&) { ++sectionPresses; };
    preview.onMergePressed = [&] (const SectionHit& h, const juce::ModifierKeys&) { ++mergePresses; CHECK (h.zone == SectionZone::Body); };
    preview.onMergeDragged = [&] (juce::Point<int>, const juce::ModifierKeys&) { ++mergeDrags; return true; };
    preview.onMergeReleased = [&] (juce::Point<int>, const juce::ModifierKeys&) { ++mergeReleases; };

    const auto alt = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier);
    const juce::Point<int> p (50, previewHeight / 2);   // inside the 0..960 section (96 px)
    preview.mouseDown (previewMouseAt (preview, p.x, p.y, alt));
    CHECK (mergePresses == 1);
    CHECK (sectionPresses == 0);

    preview.mouseDrag (previewMouseAt (preview, p.x, p.y + 1, alt));   // under the threshold
    CHECK (mergeDrags == 0);
    preview.mouseDrag (previewMouseAt (preview, p.x, p.y + 10, alt));
    CHECK (mergeDrags == 1);

    preview.mouseUp (previewMouseAt (preview, p.x, p.y + 10, alt));
    CHECK (mergeReleases == 1);
}

TEST_CASE ("TrackNotePreview: a press without Alt still starts a section gesture", "[track-note-preview][merge]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = playbacktest::addTrack (doc);
    playbacktest::addNote (track, 60, 0, 960);
    TimelineViewState viewState;
    viewState.setPixelsPerTick (0.1);
    SectionViewState sectionView;
    TrackNotePreview preview (track, viewState);
    preview.setSectionView (&sectionView, (juce::int64) track.getProperty (SongIDs::trackId));
    preview.setBounds (0, 0, previewWidth, previewHeight);

    int sectionPresses = 0, mergePresses = 0;
    preview.onSectionPressed = [&] (const SectionHit&, int, const juce::ModifierKeys&) { ++sectionPresses; };
    preview.onMergePressed = [&] (const SectionHit&, const juce::ModifierKeys&) { ++mergePresses; };

    preview.mouseDown (previewMouseAt (preview, 200, previewHeight / 2));
    CHECK (sectionPresses == 1);
    CHECK (mergePresses == 0);
}
TEST_CASE ("TrackNotePreview: the merge target row is outlined and shows ghosts; other rows are not", "[track-note-preview][merge]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = playbacktest::addTrack (doc);
    const auto id = (juce::int64) track.getProperty (SongIDs::trackId);
    TimelineViewState viewState;
    viewState.setPixelsPerTick (0.1);
    SectionViewState sectionView;
    TrackNotePreview preview (track, viewState);
    preview.setSectionView (&sectionView, id);
    preview.setBounds (0, 0, previewWidth, previewHeight);

    auto render = [&]
    {
        juce::Image image (juce::Image::ARGB, previewWidth, previewHeight, true, juce::SoftwareImageType());
        juce::Graphics g (image);
        preview.paint (g);
        return image;
    };

    const auto plain = render();

    MergeDragPreview m;
    m.targetTrackId = id + 1;   // some other row
    m.valid = true;
    m.ghosts = { { 1, 0, 480 } };
    sectionView.merge = m;
    CHECK (render().getPixelAt (0, previewHeight / 2) == plain.getPixelAt (0, previewHeight / 2));   // not the target: unchanged

    sectionView.merge->targetTrackId = id;
    const auto outlined = render();
    CHECK (outlined.getPixelAt (0, previewHeight / 2) != plain.getPixelAt (0, previewHeight / 2));   // outline at the edge
    CHECK (outlined.getPixelAt (viewState.xForTick (240), previewHeight / 2)
           != plain.getPixelAt (viewState.xForTick (240), previewHeight / 2));                         // ghost fill

    sectionView.merge->valid = false;
    CHECK (render().getPixelAt (0, previewHeight / 2) == plain.getPixelAt (0, previewHeight / 2));    // invalid target: no highlight
}

// --- Conductor row: tempo and meter markers ---

namespace
{
    struct TempMidi
    {
        juce::File file = juce::File::createTempFile (".mid");
        explicit TempMidi (const miditest::Bytes& bytes) { REQUIRE (file.replaceWithData (bytes.data(), bytes.size())); }
        ~TempMidi() { file.deleteFile(); }
    };

    // 120 BPM and 4/4 at tick 0, one note, PPQ 480.
    miditest::Bytes standardSong()
    {
        miditest::TrackBody c;
        c.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 }).ev (0, { 0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08 })
         .ev (0, { 0xFF, 0x59, 0x02, 0x00, 0x00 }).eot (3840);
        miditest::TrackBody n;
        n.ev (0, { 0x90, 60, 100 }).ev (480, { 0x80, 60, 0x40 }).eot();
        return miditest::smf (1, 480, { c, n });
    }

    struct Loaded
    {
        TempMidi midi { standardSong() };
        SongDocument doc;
        Loaded()
        {
            Diagnostics d;
            REQUIRE (importMidiFile (doc, midi.file, 1, d));
        }
    };

    juce::Image renderPreview (const juce::ValueTree& track, const TimelineViewState& view, int width = previewWidth)
    {
        TrackNotePreview preview (track, view);
        preview.setBounds (0, 0, width, previewHeight);
        juce::Image image (juce::Image::ARGB, width, previewHeight, true, juce::SoftwareImageType());
        juce::Graphics g (image);
        preview.paint (g);
        return image;
    }

    bool hasPixel (const juce::Image& image, juce::uint32 colour, int x0, int x1, int y0, int y1)
    {
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x)
                if (image.getPixelAt (x, y) == juce::Colour (colour))
                    return true;
        return false;
    }

    // Antialiased glyph cores are close to, not exactly, the text colour.
    bool hasPixelNear (const juce::Image& image, juce::uint32 colour, int x0, int x1, int y0, int y1, int tolerance = 40)
    {
        const juce::Colour want (colour);
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x)
            {
                const auto got = image.getPixelAt (x, y);
                if (std::abs (got.getRed() - want.getRed()) <= tolerance && std::abs (got.getGreen() - want.getGreen()) <= tolerance
                    && std::abs (got.getBlue() - want.getBlue()) <= tolerance)
                    return true;
            }
        return false;
    }

    TimelineViewState tenthPixelView (double scrollTicks = 0.0)
    {
        TimelineViewState v;
        v.setPixelsPerTick (0.1);
        v.setScrollOffsetTicks (scrollTicks);
        return v;
    }
}

TEST_CASE ("TrackNotePreview: the Conductor row draws a tempo line at the tempo's tick; other rows do not", "[track-note-preview][conductor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Loaded f;
    const auto view = tenthPixelView();

    const auto conductor = renderPreview (f.doc.getConductorTrack(), view);
    CHECK (conductor.getPixelAt (view.xForTick (0), 1) == juce::Colour (SongsmithColours::conductorTempo));

    const auto other = f.doc.getConductorTrack().getParent().getChild (1);
    REQUIRE (other.isValid());
    const auto plain = renderPreview (other, view);
    CHECK_FALSE (hasPixel (plain, SongsmithColours::conductorTempo, 0, 4, 0, 8));
    CHECK_FALSE (hasPixel (plain, SongsmithColours::conductorMeter, 0, previewWidth, 0, previewHeight));
}

TEST_CASE ("TrackNotePreview: a tempo change draws its line and flips the background shade after it", "[track-note-preview][conductor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Loaded f;
    REQUIRE_FALSE (setTempo (f.doc, 960, 90.0).has_value());
    const auto view = tenthPixelView();

    const auto image = renderPreview (f.doc.getConductorTrack(), view, 600);
    CHECK (image.getPixelAt (view.xForTick (960), previewHeight - 1) == juce::Colour (SongsmithColours::conductorTempo));
    CHECK (image.getPixelAt (view.xForTick (960) + 61, 3) != juce::Colour (SongsmithColours::background));
    CHECK (image.getPixelAt (view.xForTick (960) + 61, previewHeight - 3) == juce::Colour (SongsmithColours::background));   // tempo tint is top half only
    CHECK (image.getPixelAt (view.xForTick (480) - 4, 3) == juce::Colour (SongsmithColours::background));
}

TEST_CASE ("TrackNotePreview: a meter change draws a meter line", "[track-note-preview][conductor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Loaded f;
    REQUIRE_FALSE (setMeter (f.doc, 1920, 3, 4).has_value());
    const auto view = tenthPixelView();

    const auto image = renderPreview (f.doc.getConductorTrack(), view);
    CHECK (image.getPixelAt (view.xForTick (1920), previewHeight - 1) == juce::Colour (SongsmithColours::conductorMeter));
    CHECK (image.getPixelAt (view.xForTick (1920), 1) != juce::Colour (SongsmithColours::conductorTempo));
}

TEST_CASE ("TrackNotePreview: a tempo scrolled off the left still tints the visible row", "[track-note-preview][conductor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Loaded f;
    REQUIRE_FALSE (setTempo (f.doc, 960, 90.0).has_value());
    const auto view = tenthPixelView (2000.0);

    const auto image = renderPreview (f.doc.getConductorTrack(), view);
    CHECK (view.xForTick (960) < 0);
    CHECK (image.getPixelAt (150, 3) != juce::Colour (SongsmithColours::background));
    CHECK_FALSE (hasPixel (image, SongsmithColours::conductorTempo, 0, previewWidth, previewHeight - 4, previewHeight));
}

TEST_CASE ("TrackNotePreview: dense tempo changes all get lines even when labels are thinned", "[track-note-preview][conductor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Loaded f;
    for (int i = 1; i <= 40; ++i)
        REQUIRE_FALSE (setTempo (f.doc, i * 10, 100.0 + i).has_value());
    const auto view = tenthPixelView();

    const auto image = renderPreview (f.doc.getConductorTrack(), view);
    for (int i = 0; i <= 40; ++i)
        CHECK (image.getPixelAt (view.xForTick (i * 10), previewHeight - 1) == juce::Colour (SongsmithColours::conductorTempo));
}

TEST_CASE ("TrackNotePreview: a Conductor row with no document events paints only the background", "[track-note-preview][conductor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto conductor = doc.addTrack ("Conductor", (int) 0xFFAABBCC, 0, 0);
    conductor.setProperty (SongIDs::isConductor, true, nullptr);
    const auto view = tenthPixelView();

    const auto image = renderPreview (conductor, view);
    CHECK (image.getPixelAt (previewWidth / 2, previewHeight / 2) == juce::Colour (SongsmithColours::background));
    CHECK (image.getPixelAt (1, 1) == juce::Colour (SongsmithColours::background));
    CHECK (image.getPixelAt (10, 3) == juce::Colour (SongsmithColours::background));
}

TEST_CASE ("TrackNotePreview: three tempos tint only the odd segment, which ends at the next tempo", "[track-note-preview][conductor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Loaded f;
    REQUIRE_FALSE (setTempo (f.doc, 960, 90.0).has_value());
    REQUIRE_FALSE (setTempo (f.doc, 1920, 140.0).has_value());
    const auto view = tenthPixelView();

    const auto image = renderPreview (f.doc.getConductorTrack(), view, 600);
    const juce::Colour bg (SongsmithColours::background);
    CHECK (image.getPixelAt (view.xForTick (960) + 61, 3) != bg);      // segment 1: tinted
    CHECK (image.getPixelAt (view.xForTick (1920) - 6, 3) != bg);      // ... up to the next tempo
    // Segment 2 is plain again: same shade as the same grid phase in segment 0 (1920 ticks = 192 px
    // is a whole number of grid cells, so a grid line would fall on both or neither).
    CHECK (image.getPixelAt (view.xForTick (1920) + 61, 3) == image.getPixelAt (61, 3));
    CHECK (image.getPixelAt (61, 3) == bg);
}

TEST_CASE ("TrackNotePreview: a label of a marker just off the left edge still reaches into view", "[track-note-preview][conductor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Loaded f;
    REQUIRE_FALSE (setTempo (f.doc, 960, 888.0).has_value());
    const auto view = tenthPixelView (960.0 + 50.0);   // tempo line at x = -5

    const auto image = renderPreview (f.doc.getConductorTrack(), view);
    REQUIRE (view.xForTick (960) == -5);
    CHECK (hasPixelNear (image, SongsmithColours::conductorTempo, 0, 20, 0, previewHeight / 2));
}

TEST_CASE ("TrackNotePreview: a tempo and a meter on the same tick both show their labels", "[track-note-preview][conductor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Loaded f;
    const auto view = tenthPixelView();

    const auto image = renderPreview (f.doc.getConductorTrack(), view);
    const int half = previewHeight / 2;
    CHECK (hasPixelNear (image, SongsmithColours::conductorTempo, 2, 60, 0, half));
    CHECK (hasPixelNear (image, SongsmithColours::conductorMeter, 2, 60, half, previewHeight));
}

TEST_CASE ("TrackNotePreview: a meter change tints the bottom half after it, not the top", "[track-note-preview][conductor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Loaded f;
    REQUIRE_FALSE (setMeter (f.doc, 1920, 3, 4).has_value());
    const auto view = tenthPixelView();

    const auto image = renderPreview (f.doc.getConductorTrack(), view);
    const juce::Colour bg (SongsmithColours::background);
    const int x = view.xForTick (1920) + 61;
    CHECK (image.getPixelAt (x, previewHeight - 3) != bg);
    CHECK (image.getPixelAt (x, 3) == bg);
    CHECK (image.getPixelAt (61, previewHeight - 3) == bg);   // before the change: plain
}

TEST_CASE ("TrackNotePreview: three meters tint only the odd segment", "[track-note-preview][conductor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Loaded f;
    REQUIRE_FALSE (setMeter (f.doc, 1920, 3, 4).has_value());
    REQUIRE_FALSE (setMeter (f.doc, 3360, 6, 8).has_value());
    const auto view = tenthPixelView();

    const auto image = renderPreview (f.doc.getConductorTrack(), view, 600);
    const juce::Colour bg (SongsmithColours::background);
    CHECK (image.getPixelAt (view.xForTick (1920) + 61, previewHeight - 3) != bg);
    CHECK (image.getPixelAt (view.xForTick (3360) + 61, previewHeight - 3) == bg);
}

TEST_CASE ("TrackNotePreview: two tempos on one tick make no zero-width segment and do not invert later shades", "[track-note-preview][conductor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Loaded f;
    REQUIRE_FALSE (setTempo (f.doc, 960, 90.0).has_value());
    REQUIRE_FALSE (setTempo (f.doc, 1920, 140.0).has_value());
    auto map = f.doc.getTempoMapNode();
    map.appendChild (map.getChild (1).createCopy(), nullptr);   // a second tempo on tick 960
    REQUIRE (map.getNumChildren() == 4);
    const auto view = tenthPixelView();

    const auto image = renderPreview (f.doc.getConductorTrack(), view, 600);
    const juce::Colour bg (SongsmithColours::background);
    CHECK (image.getPixelAt (view.xForTick (960) + 61, 3) != bg);
    CHECK (image.getPixelAt (view.xForTick (1920) + 61, 3) == image.getPixelAt (61, 3));
}

TEST_CASE ("TrackNotePreview: the implicit tick-0 tempo and meter of a file without them are drawn", "[track-note-preview][conductor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    miditest::TrackBody c;
    c.eot (3840);
    miditest::TrackBody n;
    n.ev (0, { 0x90, 60, 100 }).ev (480, { 0x80, 60, 0x40 }).eot();
    const auto bytes = miditest::smf (1, 480, { c, n });
    TempMidi midi (bytes);
    SongDocument doc;
    Diagnostics d;
    REQUIRE (importMidiFile (doc, midi.file, 1, d));
    const auto view = tenthPixelView();

    const auto image = renderPreview (doc.getConductorTrack(), view);
    CHECK (image.getPixelAt (0, previewHeight - 1) == juce::Colour (SongsmithColours::conductorTempo));   // tempo wins the shared line
    CHECK (hasPixelNear (image, SongsmithColours::conductorTempo, 2, 60, 0, previewHeight / 2));
    CHECK (hasPixelNear (image, SongsmithColours::conductorMeter, 2, 60, previewHeight / 2, previewHeight));
}

TEST_CASE ("TrackNotePreview: with thousands of tempos a narrow clip still draws its own markers", "[track-note-preview][conductor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Loaded f;
    for (int i = 1; i <= 2000; ++i)
        REQUIRE_FALSE (setTempo (f.doc, i * 10, 60.0 + (i % 100)).has_value());
    const auto view = tenthPixelView();   // one tempo per pixel

    constexpr int width = 2200;
    TrackNotePreview preview (f.doc.getConductorTrack(), view);
    preview.setBounds (0, 0, width, previewHeight);
    juce::Image image (juce::Image::ARGB, width, previewHeight, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    g.reduceClipRegion (1500, 0, 60, previewHeight);
    preview.paint (g);

    for (int x = 1500; x < 1560; ++x)
        CHECK (image.getPixelAt (x, previewHeight - 1) == juce::Colour (SongsmithColours::conductorTempo));
    CHECK (image.getPixelAt (100, previewHeight - 1).isTransparent());
    CHECK (image.getPixelAt (1700, previewHeight - 1).isTransparent());
}

// --- Conductor row: clicking a marker ---

namespace
{
    struct MarkerClickProbe
    {
        std::vector<std::pair<TempoMeterKind, int>> markers;
        std::vector<int> timelineTicks;
        int nonToggleClicks = 0;
        int sectionPresses = 0;

        void attach (TrackNotePreview& p)
        {
            p.onMarkerClicked = [this] (TempoMeterKind k, int t) { markers.emplace_back (k, t); };
            p.onTimelineClicked = [this] (int t) { timelineTicks.push_back (t); };
            p.onNonToggleClick = [this] (const juce::ModifierKeys&) { ++nonToggleClicks; };
            p.onSectionPressed = [this] (const SectionHit&, int, const juce::ModifierKeys&) { ++sectionPresses; };
        }
    };
}

TEST_CASE ("TrackNotePreview: a plain click on a Conductor marker fires onMarkerClicked and nothing else; the half picks the kind", "[track-note-preview][conductor][marker-click]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Loaded f;
    REQUIRE_FALSE (setTempo (f.doc, 1920, 90.0).has_value());
    REQUIRE_FALSE (setMeter (f.doc, 1920, 3, 4).has_value());
    const auto view = tenthPixelView();
    SectionViewState sectionView;
    TrackNotePreview preview (f.doc.getConductorTrack(), view);
    preview.setSectionView (&sectionView, (juce::int64) f.doc.getConductorTrack().getProperty (SongIDs::trackId));
    preview.setBounds (0, 0, previewWidth, previewHeight);
    MarkerClickProbe probe;
    probe.attach (preview);

    preview.mouseDown (previewMouseAt (preview, view.xForTick (1920) + 1, 3));
    preview.mouseDown (previewMouseAt (preview, view.xForTick (1920), previewHeight - 3));
    REQUIRE (probe.markers.size() == 2);
    CHECK (probe.markers[0] == std::make_pair (TempoMeterKind::tempo, 1920));
    CHECK (probe.markers[1] == std::make_pair (TempoMeterKind::meter, 1920));
    CHECK (probe.timelineTicks.empty());
    CHECK (probe.nonToggleClicks == 0);
    CHECK (probe.sectionPresses == 0);
}

TEST_CASE ("TrackNotePreview: a Conductor click away from every marker still seeks and selects as before", "[track-note-preview][conductor][marker-click]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Loaded f;
    REQUIRE_FALSE (setTempo (f.doc, 1920, 90.0).has_value());
    const auto view = tenthPixelView();
    TrackNotePreview preview (f.doc.getConductorTrack(), view);
    preview.setBounds (0, 0, previewWidth, previewHeight);
    MarkerClickProbe probe;
    probe.attach (preview);

    preview.mouseDown (previewMouseAt (preview, view.xForTick (2400), 3));
    CHECK (probe.markers.empty());
    REQUIRE (probe.timelineTicks.size() == 1);
    CHECK (probe.timelineTicks[0] == 2400);
    CHECK (probe.nonToggleClicks == 1);
}

TEST_CASE ("TrackNotePreview: only a plain left press takes a marker; modified and right presses fall through", "[track-note-preview][conductor][marker-click]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Loaded f;
    REQUIRE_FALSE (setTempo (f.doc, 1920, 90.0).has_value());
    const auto view = tenthPixelView();
    TrackNotePreview preview (f.doc.getConductorTrack(), view);
    preview.setBounds (0, 0, previewWidth, previewHeight);
    MarkerClickProbe probe;
    probe.attach (preview);

    const int x = view.xForTick (1920);
    preview.mouseDown (previewMouseAt (preview, x, 3, juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier)));
    preview.mouseDown (previewMouseAt (preview, x, 3, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier)));
    preview.mouseDown (previewMouseAt (preview, x, 3, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::ctrlModifier)));
    CHECK (probe.markers.empty());
    CHECK (probe.timelineTicks.size() == 3);
}

TEST_CASE ("TrackNotePreview: a non-Conductor row never reports a marker click, even where the Conductor's marker would be", "[track-note-preview][conductor][marker-click]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Loaded f;
    REQUIRE_FALSE (setTempo (f.doc, 1920, 90.0).has_value());
    const auto view = tenthPixelView();
    const auto other = f.doc.getConductorTrack().getParent().getChild (1);
    TrackNotePreview preview (other, view);
    preview.setBounds (0, 0, previewWidth, previewHeight);
    MarkerClickProbe probe;
    probe.attach (preview);

    preview.mouseDown (previewMouseAt (preview, view.xForTick (1920), 3));
    preview.mouseDown (previewMouseAt (preview, view.xForTick (0), 3));
    CHECK (probe.markers.empty());
    CHECK (probe.timelineTicks.size() == 2);
}

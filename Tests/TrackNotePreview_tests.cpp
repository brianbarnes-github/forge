// Tests/TrackNotePreview_tests.cpp
#include "UI/TrackNotePreview.h"
#include "UI/SongDocument.h"
#include "UI/SongsmithColours.h"
#include "UI/SectionEdit.h"
#include "UI/SectionViewState.h"
#include "PlaybackTestSupport.h"

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

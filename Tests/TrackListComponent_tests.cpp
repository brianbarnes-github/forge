#include "UI/SongsmithColours.h"
// Verifies TrackListComponent::rebuild() clears a stale selection once the
// previously-selected track disappears from SOURCE_MIDI — e.g. after
// SongDocument::removeTrack — without over-pruning one that is still live.

#include "PlaybackTestSupport.h"
#include "UI/SongDocument.h"
#include "UI/SongModelBridge.h"
#include "UI/Playback/PlaybackController.h"
#include "UI/TrackListComponent.h"
#include "UI/Playback/PlayheadOverlay.h"
#include "UI/Playback/TimelineRuler.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <set>

using namespace lotro;

namespace lotro
{
    // Grants TrackListComponent_tests.cpp access to selectTrack()/rebuild(),
    // which stay private/internal on the class itself (see the friend
    // declaration in TrackListComponent.h for why).
    struct TrackListComponentTestAccess
    {
        static void selectTrack (TrackListComponent& c, juce::int64 trackId) { c.selectTrack (trackId, {}, false); }
        static void click (TrackListComponent& c, juce::int64 id, juce::ModifierKeys mods, bool fromStrip) { c.selectTrack (id, mods, fromStrip); }
        static const std::set<juce::int64>& selected (TrackListComponent& c) { return c.selectedTrackIds; }
        static void rebuild (TrackListComponent& c) { c.rebuild(); }
        static int numRows (TrackListComponent& c) { return c.content.rows.size(); }
        static juce::Array<TrackRowComponent*> rows (TrackListComponent& c)
        {
            juce::Array<TrackRowComponent*> result;
            for (auto* row : c.content.rows)
                result.add (row);
            return result;
        }
        static TrackRowComponent* rowFor (TrackListComponent& c, juce::int64 trackId)
        {
            for (auto* row : c.content.rows)
                if (row->getTrackId() == trackId)
                    return row;
            return nullptr;
        }
        static juce::int64 selectedTrackId (const TrackListComponent& c) { return c.selectedTrackId; }
        static const TimelineViewState& timelineView (const TrackListComponent& c) { return c.timelineView; }
        static TimelineViewState& timelineViewMut (TrackListComponent& c) { return c.timelineView; }
        static int contentWidth (const TrackListComponent& c) { return c.contentWidth(); }
        static int notePreviewOriginX (const TrackListComponent& c) { return c.notePreviewOriginX(); }
        static juce::ScrollBar& horizontalBar (TrackListComponent& c) { return c.horizontalBar; }
        static double scrollOffsetTicks (TrackListComponent& c) { return c.timelineView.getScrollOffsetTicks(); }
        static void zoomIn (TrackListComponent& c)
        {
            c.timelineView.zoomBy (4.0, 0);
            c.timelineFitted = false;
            c.syncHorizontalBar();
        }
        static juce::Viewport& viewport (TrackListComponent& c) { return c.viewport; }
        static PlayheadOverlay* overlay (TrackListComponent& c) { return c.overlay.get(); }
        static int overlayRepaints (const TrackListComponent& c) { return c.overlayRepaintCount; }
        static MarkerOverlay* markerOverlay (TrackListComponent& c) { return c.markerOverlay.get(); }
    };
}

namespace
{
    using Access = TrackListComponentTestAccess;
}

namespace
{
    // One track, one note ending at `endTick` — the document's timeline end.
    juce::ValueTree addTrackEndingAt (SongDocument& doc, int endTick)
    {
        auto track = doc.addTrack ("Track A", (int) 0xFFAABBCCu, 0, 0);
        juce::ValueTree note (SongIDs::NOTE);
        note.setProperty (SongIDs::startTick, endTick - 1000, nullptr);
        note.setProperty (SongIDs::durationTicks, 1000, nullptr);
        SongDocument::getNotesNode (track).appendChild (note, nullptr);
        return track;
    }

    void wheelAt (TrackListComponent& list, int x, float deltaY, juce::ModifierKeys mods)
    {
        juce::MouseWheelDetails wheel {};
        wheel.deltaY = deltaY;
        const auto pos = juce::Point<float> ((float) x, 10.0f);
        list.mouseWheelMove (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                                pos, mods,
                                                0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &list, &list,
                                                juce::Time::getCurrentTime(), pos,
                                                juce::Time::getCurrentTime(), 1, false),
                              wheel);
    }

    int previewWidth (const TrackListComponent& list)
    {
        return Access::contentWidth (list) - Access::notePreviewOriginX (list);
    }
}


TEST_CASE ("TrackListComponent: rebuild() clears a selection whose track was removed", "[piano-roll]")
{
    SongDocument doc;
    auto track1 = doc.addTrack ("Track 1", (int) 0xFF7FA8D0, 0, 0);
    doc.addTrack ("Track 2", (int) 0xFF000000, 1, 0);
    const auto track1Id = (juce::int64) track1.getProperty (SongIDs::trackId);

    TrackListComponent list (doc);

    Access::selectTrack (list, track1Id);
    CHECK (Access::selectedTrackId (list) == track1Id);

    doc.removeTrack (track1Id);
    Access::rebuild (list);

    CHECK (Access::selectedTrackId (list) == -1);
}

TEST_CASE ("TrackListComponent: a live selection survives a rebuild after a second import raises ticksPerQuarter", "[piano-roll]")
{
    // A second MIDI import at a higher PPQ than the first forces an LCM raise
    // of the document's ticksPerQuarter (and rescales every existing note's
    // ticks) without removing or adding the selected track. rebuild()'s
    // stale-selection pruning must not over-prune here: the track is still
    // there, so the selection has to stay.
    Song first;
    first.ticksPerQuarter = 120;
    Track t0;
    t0.name = "Existing Track";
    t0.sourceMidiChannel = 0;
    Note n0;
    n0.pitch = 60;
    n0.startTick = 10;
    n0.durationTicks = 20;
    t0.notes.push_back (n0);
    first.tracks.push_back (t0);

    SongDocument doc;
    Diagnostics diag1;
    appendImportedSong (doc, first, 1, diag1);

    const auto track1Id = (juce::int64) doc.getTrack (0).getProperty (SongIDs::trackId);

    TrackListComponent list (doc);

    Access::selectTrack (list, track1Id);
    REQUIRE (Access::selectedTrackId (list) == track1Id);
    REQUIRE ((int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter) == 120);

    Song second;
    second.ticksPerQuarter = 480; // LCM(120, 480) == 480: raises the document.
    Track t1;
    t1.name = "Incoming Track";
    t1.sourceMidiChannel = 1;
    Note n1;
    n1.pitch = 64;
    n1.startTick = 5;
    n1.durationTicks = 3;
    t1.notes.push_back (n1);
    second.tracks.push_back (t1);

    Diagnostics diag2;
    appendImportedSong (doc, second, 2, diag2);
    REQUIRE ((int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter) == 480);

    Access::rebuild (list);

    CHECK (Access::selectedTrackId (list) == track1Id);
    CHECK ((int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter) == 480);
}

TEST_CASE ("TrackListComponent: a real import populates the row list through the real async listener path", "[piano-roll]")
{
    // Every other test in this file drives rebuild()/selectTrack() directly
    // via TrackListComponentTestAccess, bypassing the real
    // ValueTree::Listener -> triggerAsyncUpdate() -> handleAsyncUpdate()
    // chain that a real import actually goes through in forge_ui. This test
    // exercises that real chain end to end, with a real pumped message loop,
    // to catch a bug that direct rebuild() calls structurally cannot: import
    // reported empty track lists in the real app despite `ctest` passing.
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    TrackListComponent list (doc);
    REQUIRE (Access::numRows (list) == 1); // the conductor row

    Diagnostics diags;
    // __FILE__-relative, not getCurrentWorkingDirectory()-relative: ctest
    // runs test binaries with CWD = build/Tests, not the repo root (see
    // Tests/SongsmithRoundTrip_tests.cpp's projectRoot() for the same
    // pattern already established there).
    const auto file = juce::File (__FILE__).getParentDirectory().getParentDirectory()
                           .getChildFile ("midi").getChildFile ("blue.mid");
    REQUIRE (file.existsAsFile());
    REQUIRE (importMidiFile (doc, file, 1, diags));

    // Import happened synchronously; TrackListComponent's ValueTree::Listener
    // callbacks fired synchronously too (JUCE notifies listeners at the point
    // of mutation), each just calling triggerAsyncUpdate() — so rebuild()
    // itself has NOT run yet. Pump the message loop for a bounded real
    // wall-clock window to let it run. runDispatchLoopUntil (rather than
    // runDispatchLoop()/stopDispatchLoop()) is used deliberately: stop is a
    // one-shot latch on MessageManager's process-wide singleton, easy to
    // leave stale across unrelated tests sharing this test binary.
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);

    CHECK (doc.getNumTracks() > 0);
    CHECK (Access::numRows (list) == doc.getNumTracks());
}

TEST_CASE ("TrackListComponent: double-clicking a row forwards its trackId via onTrackDoubleClicked", "[track-list]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCCu, 0, 0);
    const auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);

    TrackListComponent list (doc);
    list.setBounds (0, 0, 300, 400);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

    juce::int64 firedId = -1;
    list.onTrackDoubleClicked = [&] (juce::int64 id) { firedId = id; };

    auto& row = *TrackListComponentTestAccess::rows (list)[1]; // [0] is the conductor
    row.onTrackDoubleClicked (trackId);

    CHECK (firedId == trackId);
}

TEST_CASE ("TrackListComponent: ghost toggle from a row forwards (trackId, visible) via onGhostToggled", "[track-list]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCCu, 0, 0);
    const auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);

    TrackListComponent list (doc);
    list.setBounds (0, 0, 300, 400);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

    juce::int64 firedId = -1;
    bool firedVisible = false;
    list.onGhostToggled = [&] (juce::int64 id, bool visible) { firedId = id; firedVisible = visible; };

    auto& row = *TrackListComponentTestAccess::rows (list)[1]; // [0] is the conductor
    row.onGhostToggled (trackId, true);

    CHECK (firedId == trackId);
    CHECK (firedVisible);
}

TEST_CASE ("TrackListComponent: a rebuild restores each row's ghost-visible state via isTrackGhosted", "[track-list]")
{
    // rebuild() recreates every row (and so every TrackNotePreview, which
    // defaults to ghost-hidden) on ANY SOURCE_MIDI change -- including a note
    // edit made in the floating editor, which bubbles up through the same
    // tree. Without restoring the ghost state the way selection is already
    // restored, every eye icon visually resets to "off" while the owner's
    // ghostedTrackIds -- the actual source of truth driving the overlays --
    // is untouched, so the icons contradict what the editor is rendering.
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto trackA = doc.addTrack ("Track A", (int) 0xFFAABBCCu, 0, 0);
    const auto idA = (juce::int64) trackA.getProperty (SongIDs::trackId);

    TrackListComponent list (doc);
    list.setBounds (0, 0, 300, 400);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

    // Mirrors SongsmithMainComponent's real wiring: it owns the ghosted-id
    // set, and the list asks it what to restore.
    std::set<juce::int64> ghosted;
    list.onGhostToggled = [&] (juce::int64 id, bool visible)
    {
        if (visible) ghosted.insert (id);
        else         ghosted.erase (id);
    };
    list.isTrackGhosted = [&] (juce::int64 id) { return ghosted.count (id) > 0; };

    auto& preview = Access::rows (list)[1]->notePreviewForTesting();
    REQUIRE (preview.toggleGhostIfHit (preview.ghostToggleBounds().getCentre()));
    REQUIRE (ghosted.count (idA) == 1);

    doc.addTrack ("Track B", (int) 0xFFDDEEFFu, 1, 0);
    Access::rebuild (list);

    auto rows = Access::rows (list);
    REQUIRE (rows.size() == 3);
    CHECK (rows[1]->notePreviewForTesting().isGhostVisible());
    CHECK_FALSE (rows[2]->notePreviewForTesting().isGhostVisible());
}

TEST_CASE ("TrackListComponent: plain wheel zooms the shared TimelineViewState about the view centre when there is no marker", "[track-list]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCCu, 0, 0);
    juce::ValueTree note (SongIDs::NOTE);
    note.setProperty (SongIDs::startTick, 0, nullptr);
    note.setProperty (SongIDs::durationTicks, 52000, nullptr);
    SongDocument::getNotesNode (track).appendChild (note, nullptr);

    TrackListComponent list (doc);
    list.setBounds (0, 0, 300, 400);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

    auto& view = Access::timelineViewMut (list);
    const int centreX = (Access::contentWidth (list) - Access::notePreviewOriginX (list)) / 2;
    view.setScrollOffsetTicks (20000.0);
    const double pixelsPerTickBefore = view.getPixelsPerTick();
    const int centreTickBefore = view.tickForX (centreX);

    // The pointer position is irrelevant: zoom is anchored on the view, not the cursor.
    wheelAt (list, Access::notePreviewOriginX (list) + 10, 1.0f, {});

    CHECK (view.getPixelsPerTick() > pixelsPerTickBefore);
    CHECK (std::abs (view.tickForX (centreX) - centreTickBefore) <= 1);

    const double zoomedIn = view.getPixelsPerTick();
    wheelAt (list, Access::notePreviewOriginX (list) + 10, -1.0f, {});
    CHECK (view.getPixelsPerTick() < zoomedIn);
}

TEST_CASE ("TrackListComponent: plain wheel centres the start marker and zooms about it", "[track-list][marker]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCC, 0, 0);
    lotro::playbacktest::addNote (track, 60, 0, 96000);
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    controller.flushRebuild();
    TrackListComponent list (doc);
    list.setPlayback (&controller);
    list.setSize (800, 300);
    list.fitTimelineToDocument();

    const auto& view = Access::timelineView (list);
    const int centreX = (Access::contentWidth (list) - Access::notePreviewOriginX (list)) / 2;
    const double markerTick = 60000.0;
    controller.setMarkerTick (markerTick);
    // Zoomed in enough that the marker can reach the centre (scroll is clamped to the song).
    Access::timelineViewMut (list).setPixelsPerTick (0.05);
    const double pixelsPerTickBefore = view.getPixelsPerTick();

    wheelAt (list, Access::notePreviewOriginX (list) + 5, 1.0f, {});

    CHECK (view.getPixelsPerTick() > pixelsPerTickBefore);
    CHECK (std::abs (view.xForTick ((int) markerTick) - centreX) <= 1);

    // Scroll away: the next notch jumps back to the marker.
    Access::timelineViewMut (list).setScrollOffsetTicks (0.0);
    wheelAt (list, Access::notePreviewOriginX (list) + 5, 1.0f, {});
    CHECK (std::abs (view.xForTick ((int) markerTick) - centreX) <= 1);
}

TEST_CASE ("TrackListComponent: ctrl+wheel scrolls the track list vertically and leaves the zoom alone", "[track-list]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    for (int i = 0; i < 12; ++i)
        addTrackEndingAt (doc, 52000);

    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 200);   // far shorter than 12+ rows

    auto& viewport = Access::viewport (list);
    REQUIRE (viewport.getViewPositionY() == 0);
    const double zoomBefore = Access::timelineView (list).getPixelsPerTick();

    wheelAt (list, 300, -1.0f, juce::ModifierKeys::ctrlModifier);   // wheel down
    const int scrolledDown = viewport.getViewPositionY();
    CHECK (scrolledDown > 0);

    wheelAt (list, 300, 1.0f, juce::ModifierKeys::ctrlModifier);    // wheel up
    CHECK (viewport.getViewPositionY() < scrolledDown);
    CHECK (Access::timelineView (list).getPixelsPerTick() == Catch::Approx (zoomBefore));
}

TEST_CASE ("TrackListComponent: fitTimelineToDocument scales the shared zoom so the longest track fits the preview width",
           "[piano-roll]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto trackA = doc.addTrack ("Track A", (int) 0xFFAABBCCu, 0, 0);
    auto trackB = doc.addTrack ("Track B", (int) 0xFF334455u, 1, 0);

    juce::ValueTree shortNote (SongIDs::NOTE);
    shortNote.setProperty (SongIDs::startTick, 0, nullptr);
    shortNote.setProperty (SongIDs::durationTicks, 480, nullptr);
    SongDocument::getNotesNode (trackA).appendChild (shortNote, nullptr);

    // Track B has the later-ending note, so it -- not Track A -- should
    // determine the fit.
    juce::ValueTree longNote (SongIDs::NOTE);
    longNote.setProperty (SongIDs::startTick, 50000, nullptr);
    longNote.setProperty (SongIDs::durationTicks, 2000, nullptr);
    SongDocument::getNotesNode (trackB).appendChild (longNote, nullptr);

    TrackListComponent list (doc);
    list.setBounds (0, 0, 900, 700);
    list.fitTimelineToDocument();

    const auto& view = Access::timelineView (list);
    const int expectedPreviewWidth = Access::contentWidth (list) - Access::notePreviewOriginX (list);
    CHECK (view.getScrollOffsetTicks() == Catch::Approx (0.0));
    CHECK (view.xForTick (52000) == expectedPreviewWidth);
}

TEST_CASE ("TrackListComponent: fitTimelineToDocument is a no-op when no track has any notes",
           "[piano-roll]")
{
    SongDocument doc;
    doc.addTrack ("Track A", (int) 0xFFAABBCCu, 0, 0);

    TrackListComponent list (doc);
    const double before = Access::timelineView (list).getPixelsPerTick();

    list.fitTimelineToDocument();

    CHECK (Access::timelineView (list).getPixelsPerTick() == Catch::Approx (before));
}

TEST_CASE ("TrackListComponent: a fitted timeline refits when the list is resized, and needs no scroll bar", "[track-list]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    addTrackEndingAt (doc, 52000);

    TrackListComponent list (doc);
    list.setBounds (0, 0, 1400, 400);
    list.fitTimelineToDocument();

    list.setBounds (0, 0, 600, 400);

    const auto& view = Access::timelineView (list);
    CHECK (view.xForTick (52000) == previewWidth (list));
    CHECK_FALSE (Access::horizontalBar (list).isVisible());
}

TEST_CASE ("TrackListComponent: after a manual wheel zoom, resizing keeps the zoom and shows a scroll bar over the whole song", "[track-list]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    addTrackEndingAt (doc, 52000);

    TrackListComponent list (doc);
    list.setBounds (0, 0, 1400, 400);
    list.fitTimelineToDocument();

    wheelAt (list, Access::notePreviewOriginX (list), 1.0f, {}); // zoom in
    const double zoomed = Access::timelineView (list).getPixelsPerTick();

    list.setBounds (0, 0, 600, 400);

    CHECK (Access::timelineView (list).getPixelsPerTick() == Catch::Approx (zoomed));

    auto& bar = Access::horizontalBar (list);
    CHECK (bar.isVisible());
    CHECK (bar.getMinimumRangeLimit() == Catch::Approx (0.0));
    CHECK (bar.getMaximumRangeLimit() == Catch::Approx (52000.0));
    CHECK (bar.getCurrentRangeSize() == Catch::Approx ((double) previewWidth (list) / zoomed));

    // The bar spans the note-preview column only, along the bottom edge.
    CHECK (bar.getX() == Access::notePreviewOriginX (list));
    CHECK (bar.getBottom() == list.getHeight());
}

TEST_CASE ("TrackListComponent: dragging the scroll bar scrolls the shared timeline", "[track-list]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    addTrackEndingAt (doc, 52000);

    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 400);   // default zoom (0.1 px/tick) — song is far wider than the pane

    auto& bar = Access::horizontalBar (list);
    REQUIRE (bar.isVisible());

    bar.setCurrentRangeStart (10000.0, juce::sendNotificationSync);

    CHECK (Access::timelineView (list).getScrollOffsetTicks() == Catch::Approx (10000.0));
}

TEST_CASE ("TrackListComponent: shift+wheel panning moves the scroll bar and stops at the end of the song", "[track-list]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    addTrackEndingAt (doc, 52000);

    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 400);

    wheelAt (list, 300, -1.0f, juce::ModifierKeys::shiftModifier);
    const auto& view = Access::timelineView (list);
    REQUIRE (view.getScrollOffsetTicks() > 0.0);
    CHECK (Access::horizontalBar (list).getCurrentRangeStart() == Catch::Approx (view.getScrollOffsetTicks()));

    for (int i = 0; i < 500; ++i)
        wheelAt (list, 300, -1.0f, juce::ModifierKeys::shiftModifier);

    const double visibleTicks = (double) previewWidth (list) / view.getPixelsPerTick();
    CHECK (view.getScrollOffsetTicks() == Catch::Approx (52000.0 - visibleTicks));
}

TEST_CASE ("TrackListComponent: an empty document shows no horizontal scroll bar", "[track-list]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 400);

    CHECK_FALSE (Access::horizontalBar (list).isVisible());
}

TEST_CASE ("TrackListComponent: clearSelection forgets the selected track", "[session-reset]")
{
    SongDocument doc;
    auto track = doc.addTrack ("Track 1", (int) 0xFF7FA8D0, 0, 0);
    const auto id = (juce::int64) track.getProperty (SongIDs::trackId);

    TrackListComponent list (doc);
    Access::selectTrack (list, id);
    REQUIRE (list.getSelectedTrackId() == id);

    list.clearSelection();
    CHECK (list.getSelectedTrackId() == -1);
}

TEST_CASE ("TrackListComponent: M/S clicks drive the PlaybackController and rows reflect it after a rebuild", "[track-list][mutesolo]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCC, 0, 0);
    SongDocument::getNotesNode (track).addChild (juce::ValueTree (SongIDs::NOTE), -1, nullptr);
    const auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);

    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    TrackListComponent list (doc);
    list.setPlayback (&controller);
    list.setSize (600, 200);
    Access::rebuild (list);

    controller.setMuted (trackId, true);
    Access::rebuild (list);
    CHECK (Access::rowFor (list, trackId)->muteButtonForTesting().getToggleState());

    auto& solo = Access::rowFor (list, trackId)->soloButtonForTesting();
    solo.triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    CHECK (controller.isSoloed (trackId));
}

TEST_CASE ("TrackListComponent: clicking the ruler moves the shared playhead", "[track-list][playhead]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCC, 0, 0);
    lotro::playbacktest::addNote (track, 60, 0, 9600);
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    controller.flushRebuild();
    TrackListComponent list (doc);
    list.setPlayback (&controller);
    list.setSize (800, 300);
    list.fitTimelineToDocument();

    list.rulerForTesting()->onSeek (960.0);
    CHECK (controller.getPositionTicks() == Catch::Approx (960.0));
}

TEST_CASE ("TrackListComponent: the ruler row sits above the viewport and the overlay spans the note-preview strip", "[track-list][playhead]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCC, 0, 0);
    lotro::playbacktest::addNote (track, 60, 0, 9600);
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    TrackListComponent list (doc);
    list.setPlayback (&controller);
    list.setSize (800, 300);

    auto* ruler = list.rulerForTesting();
    auto* overlay = Access::overlay (list);
    REQUIRE (ruler != nullptr);
    REQUIRE (overlay != nullptr);
    CHECK (ruler->getY() == 0);
    CHECK (ruler->getHeight() == TimelineRuler::height);
    CHECK (overlay->getY() == TimelineRuler::height);
    CHECK (overlay->getX() == Access::notePreviewOriginX (list));
    CHECK (overlay->getWidth() == previewWidth (list));
    bool onSelf = true, onChildren = true;
    overlay->getInterceptsMouseClicks (onSelf, onChildren);
    CHECK_FALSE (onSelf);
    CHECK_FALSE (onChildren);

    // A click at ruler-local x (the ruler spans the full list width) maps
    // through the preview-local frame: tick under (x - preview origin).
    double seeked = -1.0;
    ruler->onSeek = [&seeked] (double t) { seeked = t; };
    const auto pos = juce::Point<float> ((float) Access::notePreviewOriginX (list) + 100.0f, 3.0f);
    ruler->mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                         pos, juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier),
                                         0.0f, 0.0f, 0.0f, 0.0f, 0.0f, ruler, ruler,
                                         juce::Time::getCurrentTime(), pos,
                                         juce::Time::getCurrentTime(), 1, false));
    CHECK (seeked == Catch::Approx ((double) Access::timelineView (list).tickForX (100)));
}

TEST_CASE ("TrackListComponent: a left click in the timing bar sets the start marker; its triangle clears it", "[track-list][ruler][marker]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCC, 0, 0);
    lotro::playbacktest::addNote (track, 60, 0, 9600);
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    controller.flushRebuild();
    TrackListComponent list (doc);
    list.setPlayback (&controller);
    list.setSize (800, 300);
    list.fitTimelineToDocument();
    auto* ruler = list.rulerForTesting();
    REQUIRE (ruler != nullptr);

    auto press = [&] (int x, int y)
    {
        const auto pos = juce::Point<float> ((float) x, (float) y);
        ruler->mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos,
                                             juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, ruler, ruler,
                                             juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false));
    };

    const double playheadBefore = controller.getPositionTicks();
    press (Access::notePreviewOriginX (list) + 100, TimelineRuler::height - 4);
    REQUIRE (controller.getMarkerTick().has_value());
    CHECK (*controller.getMarkerTick() == Catch::Approx ((double) Access::timelineView (list).tickForX (100)));
    CHECK (controller.getPositionTicks() == Catch::Approx (playheadBefore));   // the playhead stays put

    const auto handle = ruler->markerHandleBounds();
    REQUIRE (handle.has_value());
    press (handle->getCentreX(), 3);
    CHECK (! controller.getMarkerTick().has_value());
}

TEST_CASE ("TrackListComponent: while playing, the view page-flips to keep the playhead visible", "[track-list][playhead]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCC, 0, 0);
    lotro::playbacktest::addNote (track, 60, 0, 96000);
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    controller.flushRebuild();
    TrackListComponent list (doc);
    list.setPlayback (&controller);
    list.setSize (800, 300);
    list.fitTimelineToDocument();
    Access::zoomIn (list);        // so the song is wider than the view

    controller.seekToTick (50000.0);                    // far beyond the visible span
    list.followPlayheadForTesting (/*playing*/ true);
    CHECK (Access::scrollOffsetTicks (list) == Catch::Approx (50000.0));
}

TEST_CASE ("TrackListComponent: when not playing, seeking never scrolls the view", "[track-list][playhead]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCC, 0, 0);
    lotro::playbacktest::addNote (track, 60, 0, 96000);
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    controller.flushRebuild();
    TrackListComponent list (doc);
    list.setPlayback (&controller);
    list.setSize (800, 300);
    list.fitTimelineToDocument();
    Access::zoomIn (list);
    REQUIRE (Access::scrollOffsetTicks (list) == Catch::Approx (0.0));

    list.rulerForTesting()->onSeek (50000.0);           // user click, off-screen, not playing
    list.followPlayheadForTesting (/*playing*/ false);
    CHECK (Access::scrollOffsetTicks (list) == Catch::Approx (0.0));
    CHECK (controller.getPositionTicks() == Catch::Approx (50000.0));
}

TEST_CASE ("TrackListComponent: zoom, scroll and resize repaint the playhead overlay and move its x", "[track-list][playhead]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCC, 0, 0);
    lotro::playbacktest::addNote (track, 60, 0, 96000);
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    controller.flushRebuild();
    TrackListComponent list (doc);
    list.setPlayback (&controller);
    list.setSize (800, 300);
    list.fitTimelineToDocument();
    controller.seekToTick (20000.0);
    auto* overlay = Access::overlay (list);
    REQUIRE (overlay != nullptr);

    // Zoom (plain wheel): same position, different mapping.
    int before = Access::overlayRepaints (list);
    const int xFitted = overlay->currentX();
    wheelAt (list, Access::notePreviewOriginX (list), 1.0f, {});
    CHECK (Access::overlayRepaints (list) > before);
    CHECK (overlay->currentX() != xFitted);

    // Horizontal wheel pan.
    before = Access::overlayRepaints (list);
    const int xZoomed = overlay->currentX();
    wheelAt (list, 300, -1.0f, juce::ModifierKeys::shiftModifier);
    CHECK (Access::overlayRepaints (list) > before);
    CHECK (overlay->currentX() != xZoomed);

    // Scroll-bar drag.
    before = Access::overlayRepaints (list);
    Access::horizontalBar (list).setCurrentRangeStart (0.0, juce::sendNotificationSync); // the pan above may have reached the far end
    CHECK (Access::overlayRepaints (list) > before);

    // Resize while fitted (refit changes pixels-per-tick).
    list.fitTimelineToDocument();
    before = Access::overlayRepaints (list);
    list.setSize (500, 300);
    CHECK (Access::overlayRepaints (list) > before);

    // Fit.
    before = Access::overlayRepaints (list);
    list.fitTimelineToDocument();
    CHECK (Access::overlayRepaints (list) > before);
}

TEST_CASE ("TrackListComponent: solo on one track dims the other row in place, without a rebuild", "[track-list][mutesolo]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto a = doc.addTrack ("A", (int) 0xFFAABBCC, 0, 0);
    auto b = doc.addTrack ("B", (int) 0xFF112233, 1, 0);
    lotro::playbacktest::addNote (a, 60, 0, 480);
    lotro::playbacktest::addNote (b, 62, 0, 480);
    const auto idA = (juce::int64) a.getProperty (SongIDs::trackId);
    const auto idB = (juce::int64) b.getProperty (SongIDs::trackId);

    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    TrackListComponent list (doc);
    list.setPlayback (&controller);
    list.setSize (600, 200);
    auto* rowA = Access::rowFor (list, idA);
    auto* rowB = Access::rowFor (list, idB);
    REQUIRE (rowA != nullptr);
    REQUIRE (rowB != nullptr);
    CHECK (rowB->getAlpha() == Catch::Approx (1.0f));

    controller.setSoloed (idA, true);                   // no rebuild
    REQUIRE (Access::rowFor (list, idB) == rowB);       // same row object: the update was in place
    CHECK (rowA->soloButtonForTesting().getToggleState());
    CHECK (rowA->getAlpha() == Catch::Approx (1.0f));
    CHECK (rowB->getAlpha() < 1.0f);

    controller.setSoloed (idA, false);
    controller.setMuted (idB, true);
    CHECK (rowB->muteButtonForTesting().getToggleState());
    CHECK (rowB->getAlpha() < 1.0f);
    CHECK (rowA->getAlpha() == Catch::Approx (1.0f));
}

TEST_CASE ("TrackListComponent: following a playhead at the end of a fitted song keeps fitted mode", "[track-list][playhead]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCC, 0, 0);
    lotro::playbacktest::addNote (track, 60, 0, 96000);
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    controller.flushRebuild();
    TrackListComponent list (doc);
    list.setPlayback (&controller);
    list.setSize (800, 300);
    list.fitTimelineToDocument();

    // Within half a pixel of the end: rounds to x == previewWidth(), but the
    // clamped scroll offset is still 0, so there is nothing to flip.
    controller.seekToTick (95999.0);
    list.followPlayheadForTesting (/*playing*/ true);
    CHECK (Access::scrollOffsetTicks (list) == Catch::Approx (0.0));

    list.setSize (500, 300);
    CHECK (Access::timelineView (list).getPixelsPerTick()
           == Catch::Approx ((double) previewWidth (list) / 96000.0));
}

TEST_CASE ("TrackListComponent: clicking a row's note preview sets the shared start marker and shows it", "[track-list][marker]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCC, 0, 0);
    lotro::playbacktest::addNote (track, 60, 0, 9600);
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    controller.flushRebuild();
    TrackListComponent list (doc);
    list.setPlayback (&controller);
    list.setSize (800, 300);
    list.fitTimelineToDocument();

    auto* markerOverlay = Access::markerOverlay (list);
    REQUIRE (markerOverlay != nullptr);
    CHECK (! markerOverlay->currentX().has_value());

    auto* row = Access::rowFor (list, (juce::int64) track.getProperty (SongIDs::trackId));
    REQUIRE (row != nullptr);
    auto& preview = row->notePreviewForTesting();
    const juce::Point<float> pos { 60.0f, 20.0f };
    preview.mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos,
                                         juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &preview, &preview,
                                         juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false));

    REQUIRE (controller.getMarkerTick().has_value());
    CHECK (*controller.getMarkerTick() == Catch::Approx ((double) Access::timelineView (list).tickForX (60)));
    CHECK (markerOverlay->currentX().has_value());
}

TEST_CASE ("TrackListComponent: the timing bar draws bar lines over the note previews, not over the info column", "[track-list][ruler]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCC, 0, 0);
    lotro::playbacktest::addNote (track, 60, 0, 9600);
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    controller.flushRebuild();
    TrackListComponent list (doc);
    list.setPlayback (&controller);
    list.setSize (800, 300);
    list.fitTimelineToDocument();

    auto* ruler = list.rulerForTesting();
    REQUIRE (ruler != nullptr);
    REQUIRE (ruler->getHeight() == TimelineRuler::height);

    juce::Image image (juce::Image::ARGB, ruler->getWidth(), ruler->getHeight(), true, juce::SoftwareImageType());
    {
        juce::Graphics g (image);
        ruler->paintEntireComponent (g, false);
    }

    const auto background = juce::Colour (SongsmithColours::background).brighter (0.1f);
    const int origin = Access::notePreviewOriginX (list);
    const int lowerRow = TimelineRuler::height - 4;

    CHECK (image.getPixelAt (origin, lowerRow) != background);        // bar 1's line, at the preview's left edge
    for (int x = 0; x < origin; ++x)
        CHECK (image.getPixelAt (x, lowerRow) == background);         // the info column stays clear
}

TEST_CASE ("TrackListComponent: click selects one track, ctrl-click toggles, shift-click selects a range", "[track-list][selection]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    std::vector<juce::int64> ids;
    for (int i = 0; i < 4; ++i)
    {
        auto t = playbacktest::addTrack (doc, "T");
        playbacktest::addNote (t, 60, 0, 480);
        ids.push_back ((juce::int64) t.getProperty (SongIDs::trackId));
    }
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);

    const juce::ModifierKeys none, ctrl (juce::ModifierKeys::ctrlModifier), shift (juce::ModifierKeys::shiftModifier);

    TrackListComponentTestAccess::click (list, ids[0], none, false);
    CHECK (list.getSelectedTrackIds() == std::set<juce::int64> { ids[0] });

    TrackListComponentTestAccess::click (list, ids[2], ctrl, false);
    CHECK (list.getSelectedTrackIds() == std::set<juce::int64> { ids[0], ids[2] });

    TrackListComponentTestAccess::click (list, ids[2], ctrl, false);   // toggles off
    CHECK (list.getSelectedTrackIds() == std::set<juce::int64> { ids[0] });

    // The Ctrl-click on ids[2] moved the anchor there, so Shift ranges from ids[2], not ids[0].
    TrackListComponentTestAccess::click (list, ids[3], shift, false);
    CHECK (list.getSelectedTrackIds() == std::set<juce::int64> { ids[2], ids[3] });

    TrackListComponentTestAccess::click (list, ids[0], none, false);
    TrackListComponentTestAccess::click (list, ids[3], shift, false);   // anchor ids[0] .. ids[3]
    CHECK (list.getSelectedTrackIds() == std::set<juce::int64> { ids[0], ids[1], ids[2], ids[3] });
}

TEST_CASE ("TrackListComponent: a plain strip click keeps a multi-track selection it is part of", "[track-list][selection]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    std::vector<juce::int64> ids;
    for (int i = 0; i < 3; ++i)
    {
        auto t = playbacktest::addTrack (doc, "T");
        playbacktest::addNote (t, 60, 0, 480);
        ids.push_back ((juce::int64) t.getProperty (SongIDs::trackId));
    }
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);

    list.selectAllTracks();
    CHECK (list.getSelectedTrackIds().size() == 3);   // the conductor is never selected

    TrackListComponentTestAccess::click (list, ids[1], {}, true);    // strip click, inside the selection
    CHECK (list.getSelectedTrackIds().size() == 3);

    TrackListComponentTestAccess::click (list, ids[1], {}, false);   // info-column click: select only it
    CHECK (list.getSelectedTrackIds() == std::set<juce::int64> { ids[1] });

    list.clearSelection();
    CHECK (list.getSelectedTrackIds().empty());
}

TEST_CASE ("TrackListComponent: rebuild drops selected tracks that no longer exist", "[track-list][selection]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto a = playbacktest::addTrack (doc, "A");
    auto b = playbacktest::addTrack (doc, "B");
    playbacktest::addNote (a, 60, 0, 480);
    playbacktest::addNote (b, 60, 0, 480);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);
    list.selectAllTracks();

    doc.removeTrack ((juce::int64) b.getProperty (SongIDs::trackId));
    TrackListComponentTestAccess::rebuild (list);

    CHECK (list.getSelectedTrackIds() == std::set<juce::int64> { (juce::int64) a.getProperty (SongIDs::trackId) });
}

namespace
{
    struct SelectionFixture
    {
        SongDocument doc;
        std::vector<juce::int64> ids;   // display order; index 1 is the conductor when withConductor
        std::unique_ptr<TrackListComponent> list;

        explicit SelectionFixture (int count, int conductorIndex = -1)
        {
            for (int i = 0; i < count; ++i)
            {
                auto t = playbacktest::addTrack (doc, "T");
                playbacktest::addNote (t, 60, 0, 480);
                if (i == conductorIndex)
                    t.setProperty (SongIDs::isConductor, true, nullptr);
                ids.push_back ((juce::int64) t.getProperty (SongIDs::trackId));
            }
            list = std::make_unique<TrackListComponent> (doc);
            list->setBounds (0, 0, 600, 300);
            TrackListComponentTestAccess::rebuild (*list);
        }

        bool highlighted (juce::int64 id) { return TrackListComponentTestAccess::rowFor (*list, id)->isSelected(); }
    };

    using Sel = std::set<juce::int64>;
    const juce::ModifierKeys kNone, kCtrl (juce::ModifierKeys::ctrlModifier), kShift (juce::ModifierKeys::shiftModifier),
                             kCtrlShift (juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier);
}

TEST_CASE ("TrackListComponent: shift-click with no anchor selects just that track", "[track-list][selection]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SelectionFixture f (4);
    TrackListComponentTestAccess::click (*f.list, f.ids[2], kShift, false);
    CHECK (f.list->getSelectedTrackIds() == Sel { f.ids[2] });
}

TEST_CASE ("TrackListComponent: shift-click replaces the selection with the range, ctrl+shift extends it", "[track-list][selection]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SelectionFixture f (5);
    auto& l = *f.list;
    TrackListComponentTestAccess::click (l, f.ids[3], kNone, false);
    TrackListComponentTestAccess::click (l, f.ids[0], kCtrl, false);       // {3, 0}, anchor 0
    TrackListComponentTestAccess::click (l, f.ids[1], kShift, false);      // range 0..1 replaces
    CHECK (l.getSelectedTrackIds() == Sel { f.ids[0], f.ids[1] });
    CHECK (l.getSelectedTrackId() == f.ids[0]);                            // anchor stays on Shift

    TrackListComponentTestAccess::click (l, f.ids[4], kCtrl, false);       // {0,1,4}, anchor 4
    TrackListComponentTestAccess::click (l, f.ids[3], kCtrlShift, false);  // extends with 3..4
    CHECK (l.getSelectedTrackIds() == Sel { f.ids[0], f.ids[1], f.ids[3], f.ids[4] });

    TrackListComponentTestAccess::click (l, f.ids[1], kShift, false);      // upward range from anchor 4
    CHECK (l.getSelectedTrackIds() == Sel { f.ids[1], f.ids[2], f.ids[3], f.ids[4] });
}

TEST_CASE ("TrackListComponent: shift range across a conductor row skips it", "[track-list][selection]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SelectionFixture f (4, 1);
    TrackListComponentTestAccess::click (*f.list, f.ids[0], kNone, false);
    TrackListComponentTestAccess::click (*f.list, f.ids[3], kShift, false);
    CHECK (f.list->getSelectedTrackIds() == Sel { f.ids[0], f.ids[2], f.ids[3] });
}

TEST_CASE ("TrackListComponent: ctrl-toggling the last selected track off leaves nothing highlighted", "[track-list][selection]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SelectionFixture f (3);
    TrackListComponentTestAccess::click (*f.list, f.ids[1], kNone, false);
    CHECK (f.highlighted (f.ids[1]));
    TrackListComponentTestAccess::click (*f.list, f.ids[1], kCtrl, false);
    CHECK (f.list->getSelectedTrackIds().empty());
    CHECK_FALSE (f.highlighted (f.ids[1]));
}

TEST_CASE ("TrackListComponent: highlight follows the whole selection and survives a rebuild", "[track-list][selection]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SelectionFixture f (4);
    TrackListComponentTestAccess::click (*f.list, f.ids[0], kNone, false);
    TrackListComponentTestAccess::click (*f.list, f.ids[2], kCtrl, false);
    TrackListComponentTestAccess::rebuild (*f.list);
    CHECK (f.highlighted (f.ids[0]));
    CHECK_FALSE (f.highlighted (f.ids[1]));
    CHECK (f.highlighted (f.ids[2]));
    CHECK_FALSE (f.highlighted (f.ids[3]));
    CHECK (f.list->getSelectedTrackIds() == Sel { f.ids[0], f.ids[2] });
}

TEST_CASE ("TrackListComponent: removing the anchor on rebuild drops it, and shift then starts fresh", "[track-list][selection]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SelectionFixture f (4);
    TrackListComponentTestAccess::click (*f.list, f.ids[0], kNone, false);
    f.doc.removeTrack (f.ids[0]);
    TrackListComponentTestAccess::rebuild (*f.list);
    CHECK (f.list->getSelectedTrackId() == -1);
    CHECK (f.list->getSelectedTrackIds().empty());
    TrackListComponentTestAccess::click (*f.list, f.ids[2], kShift, false);   // no anchor left
    CHECK (f.list->getSelectedTrackIds() == Sel { f.ids[2] });
}

TEST_CASE ("TrackListComponent: select-all with only a conductor track selects nothing", "[track-list][selection]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SelectionFixture f (1, 0);
    f.list->selectAllTracks();
    CHECK (f.list->getSelectedTrackIds().empty());
}

TEST_CASE ("TrackListComponent: plain click on a conductor highlights it without selecting it; selection changes leave the document untouched", "[track-list][selection]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SelectionFixture f (3, 1);
    const auto before = f.doc.getTree().createCopy();
    TrackListComponentTestAccess::click (*f.list, f.ids[0], kNone, false);
    TrackListComponentTestAccess::click (*f.list, f.ids[1], kNone, false);
    CHECK (f.list->getSelectedTrackIds().empty());
    CHECK (f.list->getSelectedTrackId() == f.ids[1]);
    CHECK (f.highlighted (f.ids[1]));
    CHECK_FALSE (f.highlighted (f.ids[0]));
    f.list->selectAllTracks();
    TrackListComponentTestAccess::click (*f.list, f.ids[2], kShift, false);   // anchor is the conductor
    CHECK (f.list->getSelectedTrackIds() == Sel { f.ids[2] });
    CHECK (f.doc.getTree().isEquivalentTo (before));
}

TEST_CASE ("TrackListComponent: a plain strip click outside a multi-selection selects only that track", "[track-list][selection]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SelectionFixture f (4);
    TrackListComponentTestAccess::click (*f.list, f.ids[0], kNone, false);
    TrackListComponentTestAccess::click (*f.list, f.ids[1], kCtrl, false);
    TrackListComponentTestAccess::click (*f.list, f.ids[3], kNone, true);
    CHECK (f.list->getSelectedTrackIds() == Sel { f.ids[3] });
}

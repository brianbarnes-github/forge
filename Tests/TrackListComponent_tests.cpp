#include "UI/SongsmithColours.h"
// Verifies TrackListComponent::rebuild() clears a stale selection once the
// previously-selected track disappears from SOURCE_MIDI — e.g. after
// SongDocument::removeTrack — without over-pruning one that is still live.

#include "PlaybackTestSupport.h"
#include "UI/SongDocument.h"
#include "UI/SongModelBridge.h"
#include "UI/Playback/PlaybackController.h"
#include "UI/TrackListComponent.h"
#include "UI/SectionEdit.h"
#include "UI/SectionViewState.h"
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
        static SectionViewState& sectionView (TrackListComponent& c) { return c.sectionView; }
        static void press (TrackListComponent& c, juce::int64 trackId, SectionHit hit, int tick) { c.sectionPressed (trackId, hit, tick); }
        static void drag (TrackListComponent& c, int tick) { c.sectionDragged (tick); }
        static void release (TrackListComponent& c, int tick) { c.sectionReleased (tick); }
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

TEST_CASE ("TrackListComponent: selecting a section also selects its companions on the other selected tracks", "[track-list][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    std::vector<juce::int64> ids;
    for (int i = 0; i < 2; ++i)
    {
        auto t = playbacktest::addTrack (doc, "T");
        playbacktest::addNote (t, 60, 0, 960);
        ids.push_back ((juce::int64) t.getProperty (SongIDs::trackId));
    }
    splitAt (doc, ids, 480);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);
    list.selectAllTracks();

    const auto first = sectionsOf (doc.findTrackById (ids[0]))[1];
    TrackListComponentTestAccess::press (list, ids[0], { first.id, SectionZone::Body }, 700);

    CHECK (TrackListComponentTestAccess::sectionView (list).selected.size() == 2);
}

TEST_CASE ("TrackListComponent: a section drag changes nothing in the document until release", "[track-list][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto t = playbacktest::addTrack (doc);
    playbacktest::addNote (t, 60, 0, 960);
    const auto id = (juce::int64) t.getProperty (SongIDs::trackId);
    splitAt (doc, { id }, 480);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);

    const auto second = sectionsOf (t)[1];
    TrackListComponentTestAccess::press (list, id, { second.id, SectionZone::Body }, 700);
    TrackListComponentTestAccess::drag (list, 900);

    CHECK (sectionsOf (t)[1].startTick == 480);   // untouched mid-drag
    REQUIRE (TrackListComponentTestAccess::sectionView (list).drag.has_value());
    CHECK (TrackListComponentTestAccess::sectionView (list).drag->deltaTicks == 200);

    TrackListComponentTestAccess::release (list, 900);
    CHECK (sectionsOf (t)[1].startTick == 680);
    CHECK_FALSE (TrackListComponentTestAccess::sectionView (list).drag.has_value());

    doc.undo();
    CHECK (sectionsOf (t)[1].startTick == 480);   // one undo step for the whole drag
}

TEST_CASE ("TrackListComponent: dragging an edge resizes, a press on empty strip selects no section", "[track-list][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto t = playbacktest::addTrack (doc);
    playbacktest::addNote (t, 60, 0, 960);
    const auto id = (juce::int64) t.getProperty (SongIDs::trackId);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);

    const auto sec = sectionsOf (t)[0];
    TrackListComponentTestAccess::press (list, id, { sec.id, SectionZone::RightEdge }, 960);
    TrackListComponentTestAccess::drag (list, 600);
    TrackListComponentTestAccess::release (list, 600);
    CHECK (sectionsOf (t)[0].endTick == 600);

    TrackListComponentTestAccess::press (list, id, {}, 5000);   // SectionHit{} has zone None
    CHECK (TrackListComponentTestAccess::sectionView (list).selected.empty());
}

TEST_CASE ("TrackListComponent: rebuild forgets selected sections that no longer exist", "[track-list][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto t = playbacktest::addTrack (doc);
    playbacktest::addNote (t, 60, 0, 960);
    const auto id = (juce::int64) t.getProperty (SongIDs::trackId);
    splitAt (doc, { id }, 480);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);

    const auto second = sectionsOf (t)[1];
    TrackListComponentTestAccess::press (list, id, { second.id, SectionZone::Body }, 700);
    deleteSections (doc, { { id, second.id } });
    TrackListComponentTestAccess::rebuild (list);

    CHECK (TrackListComponentTestAccess::sectionView (list).selected.empty());
}

namespace
{
    juce::MouseEvent stripMouseAt (juce::Component& c, int x, juce::ModifierKeys mods = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier))
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                 juce::Point<float> ((float) x, 20.0f), mods,
                                 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, juce::Time::getCurrentTime(),
                                 juce::Point<float> ((float) x, 20.0f), juce::Time::getCurrentTime(), 1, false);
    }

    juce::int64 idOf (const juce::ValueTree& t) { return (juce::int64) t.getProperty (SongIDs::trackId); }
}

TEST_CASE ("TrackListComponent: a click on a section edge with no movement changes nothing and opens no undo step", "[track-list][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto t = playbacktest::addTrack (doc);
    playbacktest::addNote (t, 60, 0, 960);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);
    REQUIRE_FALSE (doc.canUndo());

    // 30 ticks inside the right edge: within the hit slop, but not on the edge itself.
    TrackListComponentTestAccess::press (list, idOf (t), { 0, SectionZone::RightEdge }, 930);
    TrackListComponentTestAccess::release (list, 930);
    TrackListComponentTestAccess::press (list, idOf (t), { 0, SectionZone::LeftEdge }, 25);
    TrackListComponentTestAccess::release (list, 25);
    TrackListComponentTestAccess::press (list, idOf (t), { 0, SectionZone::Body }, 400);
    TrackListComponentTestAccess::release (list, 400);

    CHECK (sectionsOf (t)[0].startTick == 0);
    CHECK (sectionsOf (t)[0].endTick == 960);
    CHECK (SongDocument::getNotesNode (t).getChild (0).getProperty (SongIDs::durationTicks) == juce::var (960));
    CHECK (t.getChildWithName (SongIDs::SECTIONS).getNumChildren() == 0);   // not materialised
    CHECK_FALSE (doc.canUndo());
    CHECK (TrackListComponentTestAccess::sectionView (list).selected.size() == 1);   // still selected
}

TEST_CASE ("TrackListComponent: an edge drag moves the edge by the pointer's movement", "[track-list][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto t = playbacktest::addTrack (doc);
    playbacktest::addNote (t, 60, 0, 960);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);

    TrackListComponentTestAccess::press (list, idOf (t), { 0, SectionZone::RightEdge }, 930);
    TrackListComponentTestAccess::drag (list, 630);
    REQUIRE (TrackListComponentTestAccess::sectionView (list).drag.has_value());
    CHECK (TrackListComponentTestAccess::sectionView (list).drag->kind == SectionDragPreview::Kind::ResizeRight);
    TrackListComponentTestAccess::release (list, 630);
    CHECK (sectionsOf (t)[0].endTick == 660);
}

TEST_CASE ("TrackListComponent: a resize past the opposite edge leaves the section one tick wide", "[track-list][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto t = playbacktest::addTrack (doc);
    playbacktest::addNote (t, 60, 0, 960);
    splitAt (doc, { idOf (t) }, 480);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);

    const auto second = sectionsOf (t)[1];
    TrackListComponentTestAccess::press (list, idOf (t), { second.id, SectionZone::RightEdge }, 960);
    TrackListComponentTestAccess::drag (list, 100);
    TrackListComponentTestAccess::release (list, 100);
    CHECK (sectionsOf (t)[1].startTick == 480);
    CHECK (sectionsOf (t)[1].endTick == 481);

    TrackListComponentTestAccess::press (list, idOf (t), { second.id, SectionZone::LeftEdge }, 480);
    TrackListComponentTestAccess::drag (list, 5000);
    TrackListComponentTestAccess::release (list, 5000);
    CHECK (sectionsOf (t)[1].startTick == 480);   // already as narrow as it can be: left edge stays below the end
    CHECK (sectionsOf (t)[1].endTick == 481);
}

TEST_CASE ("TrackListComponent: companion sections clamp together at tick 0, in the preview and the commit", "[track-list][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    std::vector<juce::int64> ids;
    for (int i = 0; i < 2; ++i)
    {
        auto t = playbacktest::addTrack (doc, "T");
        playbacktest::addNote (t, 60, 480, 480);
        ids.push_back (idOf (t));
    }
    splitAt (doc, ids, 700);   // [0, 700) and [700, 960) on both
    resizeSections (doc, { { ids[0], sectionsOf (doc.findTrackById (ids[0]))[0].id },
                           { ids[1], sectionsOf (doc.findTrackById (ids[1]))[0].id } }, SectionEdge::Left, 300);
    auto a = doc.findTrackById (ids[0]);
    auto b = doc.findTrackById (ids[1]);
    playbacktest::addNote (a, 64, 100, 50);   // before every section: a member of the first
    REQUIRE (sectionsOf (a)[0].startTick == 300);
    REQUIRE (sectionsOf (b)[0].startTick == 300);

    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);
    list.selectAllTracks();

    // Press on B's section: A's stray note at 100 is what limits the shared move.
    TrackListComponentTestAccess::press (list, ids[1], { sectionsOf (b)[0].id, SectionZone::Body }, 500);
    REQUIRE (TrackListComponentTestAccess::sectionView (list).selected.size() == 2);
    TrackListComponentTestAccess::drag (list, -200);
    REQUIRE (TrackListComponentTestAccess::sectionView (list).drag.has_value());
    CHECK (TrackListComponentTestAccess::sectionView (list).drag->deltaTicks == -100);

    TrackListComponentTestAccess::release (list, -200);
    CHECK (sectionsOf (a)[0].startTick == 200);
    CHECK (sectionsOf (b)[0].startTick == 200);
    int lowest = std::numeric_limits<int>::max();
    for (int i = 0; i < SongDocument::getNotesNode (a).getNumChildren(); ++i)
        lowest = std::min (lowest, (int) SongDocument::getNotesNode (a).getChild (i).getProperty (SongIDs::startTick));
    CHECK (lowest == 0);
}

TEST_CASE ("TrackListComponent: a rebuild mid-drag cancels the gesture without committing", "[track-list][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto t = playbacktest::addTrack (doc);
    playbacktest::addNote (t, 60, 0, 960);
    splitAt (doc, { idOf (t) }, 480);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);

    const auto second = sectionsOf (t)[1];
    TrackListComponentTestAccess::press (list, idOf (t), { second.id, SectionZone::Body }, 700);
    TrackListComponentTestAccess::drag (list, 900);
    TrackListComponentTestAccess::rebuild (list);   // the strip holding the mouse is gone: no mouse-up will come

    CHECK_FALSE (TrackListComponentTestAccess::sectionView (list).drag.has_value());
    TrackListComponentTestAccess::release (list, 900);
    CHECK (sectionsOf (t)[1].startTick == 480);
}

TEST_CASE ("TrackListComponent: a virtual section stays selected after its first edit mints it an id", "[track-list][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto t = playbacktest::addTrack (doc);
    playbacktest::addNote (t, 60, 0, 960);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);

    TrackListComponentTestAccess::press (list, idOf (t), { 0, SectionZone::Body }, 400);
    TrackListComponentTestAccess::drag (list, 600);
    TrackListComponentTestAccess::release (list, 600);
    TrackListComponentTestAccess::rebuild (list);

    const auto sections = sectionsOf (t);
    REQUIRE (sections.size() == 1);
    REQUIRE (sections[0].id != 0);
    CHECK (sections[0].startTick == 200);
    CHECK (TrackListComponentTestAccess::sectionView (list).selected == std::set<SectionRef> { { idOf (t), sections[0].id } });
}

TEST_CASE ("TrackListComponent: a press on the conductor or an empty track starts no gesture", "[track-list][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto empty = playbacktest::addTrack (doc, "E");
    auto t = playbacktest::addTrack (doc, "T");
    playbacktest::addNote (t, 60, 0, 960);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);
    TrackListComponentTestAccess::timelineViewMut (list).setPixelsPerTick (0.1);
    TrackListComponentTestAccess::timelineViewMut (list).setScrollOffsetTicks (0.0);

    TrackListComponentTestAccess::press (list, idOf (t), { 0, SectionZone::Body }, 400);
    REQUIRE (TrackListComponentTestAccess::sectionView (list).selected.size() == 1);

    // Through the real strips: the conductor (row 0) and the note-less track.
    for (auto* row : TrackListComponentTestAccess::rows (list))
    {
        if (row->getTrackId() == idOf (t))
            continue;
        auto& preview = row->notePreviewForTesting();
        const int x = TrackListComponentTestAccess::timelineView (list).xForTick (400);
        preview.mouseDown (stripMouseAt (preview, x));
        CHECK (TrackListComponentTestAccess::sectionView (list).selected.empty());
        preview.mouseDrag (stripMouseAt (preview, x + 30));
        CHECK_FALSE (TrackListComponentTestAccess::sectionView (list).drag.has_value());
        preview.mouseUp (stripMouseAt (preview, x + 30));
    }
    CHECK (sectionsOf (t)[0].startTick == 0);
    CHECK (sectionsOf (empty).empty());
}

TEST_CASE ("TrackListComponent: a real strip drag moves the companions of every selected track as one undo step", "[track-list][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    std::vector<juce::int64> ids;
    for (int i = 0; i < 3; ++i)
    {
        auto t = playbacktest::addTrack (doc, "T");
        playbacktest::addNote (t, 60, 0, 960);
        ids.push_back (idOf (t));
    }
    splitAt (doc, ids, 480);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);
    TrackListComponentTestAccess::timelineViewMut (list).setPixelsPerTick (0.1);
    TrackListComponentTestAccess::timelineViewMut (list).setScrollOffsetTicks (0.0);

    // Select tracks 0 and 2 only, then press on track 2's strip inside the second section.
    TrackListComponentTestAccess::click (list, ids[0], {}, false);
    TrackListComponentTestAccess::click (list, ids[2], juce::ModifierKeys (juce::ModifierKeys::ctrlModifier), false);
    auto& preview = TrackListComponentTestAccess::rowFor (list, ids[2])->notePreviewForTesting();
    const auto& view = TrackListComponentTestAccess::timelineView (list);
    preview.mouseDown (stripMouseAt (preview, view.xForTick (700)));
    CHECK (list.getSelectedTrackIds() == std::set<juce::int64> { ids[0], ids[2] });   // plain strip click kept it
    CHECK (TrackListComponentTestAccess::sectionView (list).selected.size() == 2);

    preview.mouseDrag (stripMouseAt (preview, view.xForTick (900)));
    CHECK (sectionsOf (doc.findTrackById (ids[2]))[1].startTick == 480);   // nothing yet
    preview.mouseUp (stripMouseAt (preview, view.xForTick (900)));

    CHECK (sectionsOf (doc.findTrackById (ids[0]))[1].startTick == 680);
    CHECK (sectionsOf (doc.findTrackById (ids[1]))[1].startTick == 480);   // not selected
    CHECK (sectionsOf (doc.findTrackById (ids[2]))[1].startTick == 680);
    doc.undo();
    CHECK (sectionsOf (doc.findTrackById (ids[0]))[1].startTick == 480);
    CHECK (sectionsOf (doc.findTrackById (ids[2]))[1].startTick == 480);
}

namespace
{
    // A [0, 960) and B [0, 5000) (notes at 0 and 3000), both selected, viewed at 0.1 px/tick.
    struct CompanionResizeFixture
    {
        SongDocument doc;
        juce::ValueTree a, b;
        TrackListComponent list { doc };

        CompanionResizeFixture()
        {
            a = playbacktest::addTrack (doc, "A");
            b = playbacktest::addTrack (doc, "B");
            playbacktest::addNote (a, 60, 0, 960);
            playbacktest::addNote (b, 60, 0, 1000);
            playbacktest::addNote (b, 62, 3000, 2000);
            list.setBounds (0, 0, 600, 300);
            TrackListComponentTestAccess::rebuild (list);
            TrackListComponentTestAccess::timelineViewMut (list).setPixelsPerTick (0.1);
            TrackListComponentTestAccess::timelineViewMut (list).setScrollOffsetTicks (0.0);
            list.selectAllTracks();
        }

        TrackNotePreview& strip (const juce::ValueTree& t) { return TrackListComponentTestAccess::rowFor (list, idOf (t))->notePreviewForTesting(); }
        int x (int tick) { return TrackListComponentTestAccess::timelineView (list).xForTick (tick); }
    };
}

TEST_CASE ("TrackListComponent: growing an edge grows every companion by the same delta and deletes nothing", "[track-list][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CompanionResizeFixture f;
    auto& strip = f.strip (f.a);

    strip.mouseDown (stripMouseAt (strip, f.x (960)));   // A's right edge
    strip.mouseDrag (stripMouseAt (strip, f.x (1000)));
    strip.mouseUp (stripMouseAt (strip, f.x (1000)));

    CHECK (sectionsOf (f.a)[0].endTick == 1000);
    CHECK (sectionsOf (f.b)[0].endTick == 5040);
    REQUIRE (SongDocument::getNotesNode (f.b).getNumChildren() == 2);
    CHECK ((int) SongDocument::getNotesNode (f.b).getChild (1).getProperty (SongIDs::durationTicks) == 2000);

    f.doc.undo();   // one step for the whole gesture
    CHECK (sectionsOf (f.a)[0].endTick == 960);
    CHECK (sectionsOf (f.b)[0].endTick == 5000);
}

TEST_CASE ("TrackListComponent: a left-edge drag moves every companion's start by the same delta", "[track-list][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CompanionResizeFixture f;
    auto& strip = f.strip (f.b);

    strip.mouseDown (stripMouseAt (strip, f.x (0)));   // B's left edge
    strip.mouseDrag (stripMouseAt (strip, f.x (1500)));
    strip.mouseUp (stripMouseAt (strip, f.x (1500)));

    CHECK (sectionsOf (f.a)[0].startTick == 959);    // clamped: one tick left
    CHECK (sectionsOf (f.a)[0].endTick == 960);
    CHECK (sectionsOf (f.b)[0].startTick == 1500);
    CHECK (sectionsOf (f.b)[0].endTick == 5000);

    f.doc.undo();
    CHECK (sectionsOf (f.a)[0].startTick == 0);
    CHECK (sectionsOf (f.b)[0].startTick == 0);
}

TEST_CASE ("TrackListComponent: shrinking past a companion's start clamps only that companion, in preview and commit", "[track-list][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CompanionResizeFixture f;

    TrackListComponentTestAccess::press (f.list, idOf (f.a), { 0, SectionZone::RightEdge }, 960);
    TrackListComponentTestAccess::drag (f.list, -1040);
    REQUIRE (TrackListComponentTestAccess::sectionView (f.list).drag.has_value());
    CHECK (TrackListComponentTestAccess::sectionView (f.list).drag->deltaTicks == -2000);
    TrackListComponentTestAccess::release (f.list, -1040);

    CHECK (sectionsOf (f.a)[0].endTick == 1);
    CHECK (sectionsOf (f.b)[0].endTick == 3000);
    f.doc.undo();
    CHECK (sectionsOf (f.a)[0].endTick == 960);
    CHECK (sectionsOf (f.b)[0].endTick == 5000);
    CHECK (SongDocument::getNotesNode (f.a).getNumChildren() == 1);
    CHECK (SongDocument::getNotesNode (f.b).getNumChildren() == 2);
}

TEST_CASE ("TrackListComponent: a pixel or two of jitter on an edge is still a click", "[track-list][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CompanionResizeFixture f;
    auto& strip = f.strip (f.a);
    REQUIRE_FALSE (f.doc.canUndo());

    strip.mouseDown (stripMouseAt (strip, f.x (960)));
    strip.mouseDrag (stripMouseAt (strip, f.x (960) - 1));
    strip.mouseDrag (stripMouseAt (strip, f.x (960) - 2));
    CHECK_FALSE (TrackListComponentTestAccess::sectionView (f.list).drag.has_value());
    strip.mouseUp (stripMouseAt (strip, f.x (960) - 2));

    CHECK (sectionsOf (f.a)[0].endTick == 960);
    CHECK_FALSE (f.doc.canUndo());
    CHECK (f.a.getChildWithName (SongIDs::SECTIONS).getNumChildren() == 0);

    strip.mouseDown (stripMouseAt (strip, f.x (960)));
    strip.mouseDrag (stripMouseAt (strip, f.x (960) - 10));
    strip.mouseUp (stripMouseAt (strip, f.x (960) - 10));
    CHECK (sectionsOf (f.a)[0].endTick == 860);
}

TEST_CASE ("TrackListComponent: S splits at the pointer's tick on the selected tracks", "[track-list][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto a = playbacktest::addTrack (doc, "A");
    auto b = playbacktest::addTrack (doc, "B");
    playbacktest::addNote (a, 60, 0, 960);
    playbacktest::addNote (b, 60, 0, 960);
    const auto idA = (juce::int64) a.getProperty (SongIDs::trackId);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);
    list.selectAllTracks();

    CHECK (list.splitSections (480, idA));

    CHECK (sectionsOf (a).size() == 2);
    CHECK (sectionsOf (b).size() == 2);
    CHECK (sectionsOf (a)[1].startTick == 480);
    CHECK (sectionsOf (b)[1].startTick == 480);
    doc.undo();   // both tracks in one transaction
    CHECK (sectionsOf (a).size() == 1);
    CHECK (sectionsOf (b).size() == 1);
}

TEST_CASE ("TrackListComponent: S with no selection splits only the track under the pointer", "[track-list][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto a = playbacktest::addTrack (doc, "A");
    auto b = playbacktest::addTrack (doc, "B");
    playbacktest::addNote (a, 60, 0, 960);
    playbacktest::addNote (b, 60, 0, 960);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);

    CHECK (list.splitSections (480, (juce::int64) b.getProperty (SongIDs::trackId)));

    CHECK (sectionsOf (a).size() == 1);
    CHECK (sectionsOf (b).size() == 2);
}

TEST_CASE ("TrackListComponent: S falls back to the marker when the pointer is not over a strip, and does nothing without one", "[track-list][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto a = playbacktest::addTrack (doc, "A");
    playbacktest::addNote (a, 60, 0, 960);
    playbacktest::RecordingSink sink;
    PlaybackController playback (doc, sink);
    TrackListComponent list (doc);
    list.setPlayback (&playback);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);
    list.selectAllTracks();

    CHECK_FALSE (list.splitSections (std::nullopt, -1));
    CHECK (sectionsOf (a).size() == 1);   // no pointer, no marker: nothing at all
    CHECK_FALSE (doc.canUndo());
    CHECK_FALSE (list.splitAtPointer());  // headless: the pointer is nowhere, there is no marker
    CHECK_FALSE (doc.canUndo());

    playback.setMarkerTick (600.0);
    CHECK (list.splitSections (std::nullopt, -1));
    CHECK (sectionsOf (a).size() == 2);
    CHECK (sectionsOf (a)[1].startTick == 600);
}

TEST_CASE ("TrackListComponent: S at a tick that splits nothing is a no-op and opens no undo step", "[track-list][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto a = playbacktest::addTrack (doc, "A");
    auto cond = playbacktest::addTrack (doc, "Cond");
    cond.setProperty (SongIDs::isConductor, true, nullptr);
    playbacktest::addNote (a, 60, 100, 860);   // the implicit section is [0, 960)
    const auto idA = (juce::int64) a.getProperty (SongIDs::trackId);
    const auto idC = (juce::int64) cond.getProperty (SongIDs::trackId);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);

    CHECK_FALSE (list.splitSections (0, idA));     // exactly on the start edge
    CHECK_FALSE (list.splitSections (960, idA));   // exactly on the end edge
    CHECK_FALSE (list.splitSections (2000, idA));  // beyond the last note
    CHECK_FALSE (list.splitSections (480, idC));   // conductor has no sections
    CHECK_FALSE (list.splitSections (480, 9999));  // unknown track
    CHECK_FALSE (doc.canUndo());
    CHECK (a.getChildWithName (SongIDs::SECTIONS).getNumChildren() == 0);   // nothing materialised

    CHECK (list.splitSections (500, idA));
    CHECK (sectionsOf (a).size() == 2);
    CHECK_FALSE (list.splitSections (500, idA));   // repeat press: already a boundary
    doc.undo();
    CHECK (sectionsOf (a).size() == 1);            // the repeat opened no second step
    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("TrackListComponent: Ctrl+A with only a conductor selects nothing, and a split then uses the pointer's track", "[track-list][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto cond = playbacktest::addTrack (doc, "Cond");
    cond.setProperty (SongIDs::isConductor, true, nullptr);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);
    list.selectAllTracks();
    CHECK (list.getSelectedTrackIds().empty());
    CHECK_FALSE (list.splitSections (480, (juce::int64) cond.getProperty (SongIDs::trackId)));
    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("TrackListComponent: Delete removes the selected sections in one undo step", "[track-list][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto a = playbacktest::addTrack (doc, "A");
    playbacktest::addNote (a, 60, 0, 960);
    const auto id = (juce::int64) a.getProperty (SongIDs::trackId);
    splitAt (doc, { id }, 480);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);

    TrackListComponentTestAccess::press (list, id, { sectionsOf (a)[1].id, SectionZone::Body }, 700);
    CHECK (list.deleteSelectedSections());
    CHECK (sectionsOf (a).size() == 1);
    CHECK (TrackListComponentTestAccess::sectionView (list).selected.empty());

    CHECK_FALSE (list.deleteSelectedSections());   // nothing selected any more: no-op
    CHECK (sectionsOf (a).size() == 1);
    doc.undo();
    CHECK (sectionsOf (a).size() == 2);
    CHECK (SongDocument::getNotesNode (a).getNumChildren() == 2);
}

TEST_CASE ("TrackListComponent: Delete over several tracks is one undo step, and does nothing once a rebuild pruned the selection", "[track-list][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto a = playbacktest::addTrack (doc, "A");
    auto b = playbacktest::addTrack (doc, "B");
    playbacktest::addNote (a, 60, 0, 960);
    playbacktest::addNote (b, 62, 0, 960);
    const auto idA = (juce::int64) a.getProperty (SongIDs::trackId);
    const auto idB = (juce::int64) b.getProperty (SongIDs::trackId);
    splitAt (doc, { idA, idB }, 480);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);
    list.selectAllTracks();

    TrackListComponentTestAccess::press (list, idA, { sectionsOf (a)[1].id, SectionZone::Body }, 700);
    list.deleteSelectedSections();
    CHECK (sectionsOf (a).size() == 1);
    CHECK (sectionsOf (b).size() == 1);   // the companion went too
    doc.undo();
    CHECK (sectionsOf (a).size() == 2);
    CHECK (sectionsOf (b).size() == 2);

    // Select, then remove the track: the rebuild prunes the stored selection.
    const auto sectionB = sectionsOf (b)[1].id;
    TrackListComponentTestAccess::press (list, idB, { sectionB, SectionZone::Body }, 700);
    REQUIRE (TrackListComponentTestAccess::sectionView (list).selected.size() == 2);   // B's and its companion on A
    doc.removeTrack (idA);
    TrackListComponentTestAccess::rebuild (list);
    CHECK (TrackListComponentTestAccess::sectionView (list).selected
           == std::set<SectionRef> { { idB, sectionB } });

    // The surviving selection still deletes, and only B's section.
    CHECK (list.deleteSelectedSections());
    CHECK (sectionsOf (b).size() == 1);
}

TEST_CASE ("TrackListComponent: S over a strip but outside every section does nothing even with a marker set", "[track-list][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto a = playbacktest::addTrack (doc, "A");
    playbacktest::addNote (a, 60, 0, 960);
    const auto idA = (juce::int64) a.getProperty (SongIDs::trackId);
    playbacktest::RecordingSink sink;
    PlaybackController playback (doc, sink);
    TrackListComponent list (doc);
    list.setPlayback (&playback);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);
    playback.setMarkerTick (480.0);   // would split, if it were used

    CHECK_FALSE (list.splitSections (2000, idA));   // pointer on the strip, past the last note
    CHECK_FALSE (doc.canUndo());
    CHECK (a.getChildWithName (SongIDs::SECTIONS).getNumChildren() == 0);
    CHECK (sectionsOf (a).size() == 1);
}

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
        static void selectTrack (TrackListComponent& c, juce::int64 trackId) { c.selectTrack (trackId); }
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
        static PlayheadOverlay* overlay (TrackListComponent& c) { return c.overlay.get(); }
        static int overlayRepaints (const TrackListComponent& c) { return c.overlayRepaintCount; }
        static MarkerOverlay* markerOverlay (TrackListComponent& c) { return c.markerOverlay.get(); }
    };
}

namespace
{
    using Access = TrackListComponentTestAccess;
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

TEST_CASE ("TrackListComponent: ctrl+wheel zooms the shared TimelineViewState and keeps the tick under the cursor", "[track-list]")
{
    // TimelineViewState's coordinate frame is TrackNotePreview-LOCAL: that is
    // the frame TrackNotePreview::paint() calls xForTick/tickForX in. The
    // preview sits at the right edge of each row, so a wheel position in this
    // component's own local space has to be shifted by that origin before it
    // can serve as a zoom anchor. Checking only that pixelsPerTick grew would
    // pass with the anchor in the wrong frame entirely.
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCCu, 0, 0);

    // A song long enough that the zoomed view stays inside it — scrolling is
    // clamped to the song's extent, which would otherwise pin the offset to 0
    // and move the anchor for a reason unrelated to the frame under test.
    juce::ValueTree note (SongIDs::NOTE);
    note.setProperty (SongIDs::startTick, 0, nullptr);
    note.setProperty (SongIDs::durationTicks, 52000, nullptr);
    SongDocument::getNotesNode (track).appendChild (note, nullptr);

    TrackListComponent list (doc);
    list.setBounds (0, 0, 300, 400);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

    const auto& view = Access::timelineView (list);
    const int originX = Access::notePreviewOriginX (list);
    REQUIRE (originX > 0); // otherwise the frames coincide and prove nothing

    // 40px into the preview strip, expressed in both frames.
    const int anchorInPreview = 40;
    const int wheelX = originX + anchorInPreview;

    const double pixelsPerTickBefore = view.getPixelsPerTick();
    const int tickUnderCursorBefore = view.tickForX (anchorInPreview);

    juce::MouseWheelDetails wheel {};
    wheel.deltaY = 1.0f;
    const auto pos = juce::Point<float> ((float) wheelX, 10.0f);
    list.mouseWheelMove (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                            pos, juce::ModifierKeys::ctrlModifier,
                                            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &list, &list,
                                            juce::Time::getCurrentTime(), pos,
                                            juce::Time::getCurrentTime(), 1, false),
                          wheel);

    CHECK (view.getPixelsPerTick() > pixelsPerTickBefore);
    CHECK (view.tickForX (anchorInPreview) == tickUnderCursorBefore);
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

TEST_CASE ("TrackListComponent: after a manual ctrl+wheel zoom, resizing keeps the zoom and shows a scroll bar over the whole song", "[track-list]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    addTrackEndingAt (doc, 52000);

    TrackListComponent list (doc);
    list.setBounds (0, 0, 1400, 400);
    list.fitTimelineToDocument();

    wheelAt (list, Access::notePreviewOriginX (list), 1.0f, juce::ModifierKeys::ctrlModifier); // zoom in
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

TEST_CASE ("TrackListComponent: wheel panning moves the scroll bar and stops at the end of the song", "[track-list]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    addTrackEndingAt (doc, 52000);

    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 400);

    wheelAt (list, 300, -1.0f, {});
    const auto& view = Access::timelineView (list);
    REQUIRE (view.getScrollOffsetTicks() > 0.0);
    CHECK (Access::horizontalBar (list).getCurrentRangeStart() == Catch::Approx (view.getScrollOffsetTicks()));

    for (int i = 0; i < 500; ++i)
        wheelAt (list, 300, -1.0f, {});

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
                                         pos, juce::ModifierKeys(),
                                         0.0f, 0.0f, 0.0f, 0.0f, 0.0f, ruler, ruler,
                                         juce::Time::getCurrentTime(), pos,
                                         juce::Time::getCurrentTime(), 1, false));
    CHECK (seeked == Catch::Approx ((double) Access::timelineView (list).tickForX (100)));
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

    // Zoom (ctrl+wheel): same position, different mapping.
    int before = Access::overlayRepaints (list);
    const int xFitted = overlay->currentX();
    wheelAt (list, Access::notePreviewOriginX (list), 1.0f, juce::ModifierKeys::ctrlModifier);
    CHECK (Access::overlayRepaints (list) > before);
    CHECK (overlay->currentX() != xFitted);

    // Horizontal wheel pan.
    before = Access::overlayRepaints (list);
    const int xZoomed = overlay->currentX();
    wheelAt (list, 300, -1.0f, {});
    CHECK (Access::overlayRepaints (list) > before);
    CHECK (overlay->currentX() != xZoomed);

    // Scroll-bar drag.
    before = Access::overlayRepaints (list);
    Access::horizontalBar (list).setCurrentRangeStart (10000.0, juce::sendNotificationSync);
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

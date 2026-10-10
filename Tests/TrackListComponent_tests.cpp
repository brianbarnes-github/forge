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
        static void selectTrack (TrackListComponent& c, juce::int64 trackId) { c.selectTrack (trackId, {}); }
        static void click (TrackListComponent& c, juce::int64 id, juce::ModifierKeys mods) { c.selectTrack (id, mods); }
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
        static void wheel (TrackListComponent& c, TrackListComponent::WheelRegion region, juce::ModifierKeys mods,
                           float deltaY, int pointerY = 10, float deltaX = 0.0f)
        {
            juce::MouseWheelDetails details {};
            details.deltaX = deltaX;
            details.deltaY = deltaY;
            c.handleWheel (region, mods, details, pointerY);
        }
        static PlayheadOverlay* overlay (TrackListComponent& c) { return c.overlay.get(); }
        static int overlayRepaints (const TrackListComponent& c) { return c.overlayRepaintCount; }
        static MarkerOverlay* markerOverlay (TrackListComponent& c) { return c.markerOverlay.get(); }
        static SectionViewState& sectionView (TrackListComponent& c) { return c.sectionView; }
        static void mergePress (TrackListComponent& c, juce::int64 trackId, SectionHit hit, juce::ModifierKeys mods = {}) { c.mergePressed (trackId, hit, mods); }
        static bool mergeDrag (TrackListComponent& c, juce::Point<int> screen, juce::ModifierKeys mods = {}) { return c.mergeDragged (screen, mods); }
        static void mergeRelease (TrackListComponent& c, juce::Point<int> screen, juce::ModifierKeys mods = {}) { c.mergeReleased (screen, mods); }
        static void press (TrackListComponent& c, juce::int64 trackId, SectionHit hit, int tick, juce::ModifierKeys mods = {}) { c.sectionPressed (trackId, hit, tick, mods); }
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

namespace
{
    using Region = TrackListComponent::WheelRegion;
    constexpr auto ctrl = juce::ModifierKeys::ctrlModifier;
    constexpr auto shift = juce::ModifierKeys::shiftModifier;

    void addTracks (SongDocument& doc, int n)
    {
        for (int i = 0; i < n; ++i)
            addTrackEndingAt (doc, 52000);
    }
}

TEST_CASE ("TrackListComponent: a plain wheel over the note canvas zooms whether or not the rows need a vertical scrollbar", "[track-list][wheel]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    for (const int tracks : { 12, 1 })   // 12 rows overflow a 200 px viewport; 1 fits
    {
        SongDocument doc;
        addTracks (doc, tracks);
        TrackListComponent list (doc);
        list.setBounds (0, 0, 600, 200);
        REQUIRE ((Access::viewport (list).getMaximumVisibleHeight() < Access::viewport (list).getViewedComponent()->getHeight())
                 == (tracks == 12));

        const double before = Access::timelineView (list).getPixelsPerTick();
        Access::wheel (list, Region::Canvas, {}, 1.0f);
        CHECK (Access::timelineView (list).getPixelsPerTick() > before);
        CHECK (Access::viewport (list).getViewPositionY() == 0);
    }
}

TEST_CASE ("TrackListComponent: a plain wheel over the heads scrolls the rows vertically and never zooms", "[track-list][wheel]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    addTracks (doc, 12);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 200);
    auto& viewport = Access::viewport (list);
    const double zoomBefore = Access::timelineView (list).getPixelsPerTick();

    Access::wheel (list, Region::Heads, {}, -1.0f);   // wheel down
    const int down = viewport.getViewPositionY();
    CHECK (down == 50);
    Access::wheel (list, Region::Heads, {}, 1.0f);    // wheel up: back towards the first row
    CHECK (viewport.getViewPositionY() == 0);
    Access::wheel (list, Region::Heads, {}, 1.0f);    // already at the top
    CHECK (viewport.getViewPositionY() == 0);

    for (int i = 0; i < 100; ++i)
        Access::wheel (list, Region::Heads, {}, -1.0f);
    CHECK (viewport.getViewPositionY() == viewport.getViewedComponent()->getHeight() - viewport.getMaximumVisibleHeight());
    CHECK (Access::timelineView (list).getPixelsPerTick() == Catch::Approx (zoomBefore));
}

TEST_CASE ("TrackListComponent: a plain wheel over the heads does nothing when the rows fit", "[track-list][wheel]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    addTracks (doc, 1);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 400);
    const double zoomBefore = Access::timelineView (list).getPixelsPerTick();
    const double scrollBefore = Access::scrollOffsetTicks (list);

    Access::wheel (list, Region::Heads, {}, 1.0f);
    Access::wheel (list, Region::Heads, {}, -1.0f);

    CHECK (Access::viewport (list).getViewPositionY() == 0);
    CHECK (Access::timelineView (list).getPixelsPerTick() == Catch::Approx (zoomBefore));
    CHECK (Access::scrollOffsetTicks (list) == Catch::Approx (scrollBefore));
}

TEST_CASE ("TrackListComponent: ctrl+wheel resizes every row 4 px per notch and leaves zoom and horizontal scroll alone", "[track-list][wheel]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    addTracks (doc, 12);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 200);
    const int n = doc.getNumTracks();
    Access::timelineViewMut (list).setPixelsPerTick (0.05);
    Access::timelineViewMut (list).setScrollOffsetTicks (1000.0);
    const double zoomBefore = Access::timelineView (list).getPixelsPerTick();
    const double scrollBefore = Access::scrollOffsetTicks (list);
    const int band = TrackRowComponent::instrumentBandHeight;
    REQUIRE (list.getRowHeight() == 34);

    for (const auto region : { Region::Heads, Region::Canvas })
    {
        const int before = list.getRowHeight();
        Access::wheel (list, region, ctrl, 1.0f);   // wheel up grows
        CHECK (list.getRowHeight() == before + 4);
        for (auto* row : Access::rows (list))
            CHECK (row->getHeight() == before + 4 + band);
        CHECK (Access::viewport (list).getViewedComponent()->getHeight() == n * (before + 4 + band));
    }
    Access::wheel (list, Region::Canvas, ctrl, -2.0f);   // two notches down
    CHECK (list.getRowHeight() == 34);
    CHECK (Access::viewport (list).getViewedComponent()->getHeight() == n * (34 + band));
    CHECK (Access::rows (list)[3]->getBottom() == 4 * (34 + band));
    CHECK (Access::timelineView (list).getPixelsPerTick() == Catch::Approx (zoomBefore));
    CHECK (Access::scrollOffsetTicks (list) == Catch::Approx (scrollBefore));
}

TEST_CASE ("TrackListComponent: ctrl+wheel clamps the row height to the minimum and maximum", "[track-list][wheel]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    addTracks (doc, 3);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 400);

    Access::wheel (list, Region::Heads, ctrl, -100.0f);
    CHECK (list.getRowHeight() == TrackRowComponent::minRowHeight);
    for (auto* row : Access::rows (list))
        CHECK (row->getHeight() == TrackRowComponent::minRowHeight + TrackRowComponent::instrumentBandHeight);
    // Having been pushed far past the minimum must not delay growing again.
    Access::wheel (list, Region::Heads, ctrl, 1.0f);
    CHECK (list.getRowHeight() == TrackRowComponent::minRowHeight + 4);

    Access::wheel (list, Region::Canvas, ctrl, 100.0f);
    CHECK (list.getRowHeight() == TrackRowComponent::maxRowHeight);
    for (auto* row : Access::rows (list))
        CHECK (row->getHeight() == TrackRowComponent::maxRowHeight + TrackRowComponent::instrumentBandHeight);
    Access::wheel (list, Region::Canvas, ctrl, -1.0f);
    CHECK (list.getRowHeight() == TrackRowComponent::maxRowHeight - 4);
}

TEST_CASE ("TrackListComponent: ctrl+wheel with a small touchpad delta accumulates until it moves a whole pixel", "[track-list][wheel]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    addTracks (doc, 3);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 400);

    Access::wheel (list, Region::Heads, ctrl, 0.05f);   // 0.2 px
    CHECK (list.getRowHeight() == 34);
    for (int i = 0; i < 2; ++i)
        Access::wheel (list, Region::Heads, ctrl, 0.05f);   // 0.6 px in all
    CHECK (list.getRowHeight() == 35);
    for (int i = 0; i < 10; ++i)
        Access::wheel (list, Region::Heads, ctrl, -0.05f);  // back down 2 px
    CHECK (list.getRowHeight() == 33);
}

TEST_CASE ("TrackListComponent: ctrl+wheel keeps the content under the pointer where it was", "[track-list][wheel]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    addTracks (doc, 30);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 200);
    auto& viewport = Access::viewport (list);
    const int n = doc.getNumTracks();
    viewport.setViewPosition (0, 300);
    REQUIRE (viewport.getViewPositionY() == 300);

    const double band = TrackRowComponent::instrumentBandHeight;
    const int pointerY = 57;
    const double fractionBefore = (300.0 + pointerY) / (n * (34.0 + band));
    Access::wheel (list, Region::Heads, ctrl, 1.0f, pointerY);

    REQUIRE (list.getRowHeight() == 38);
    const double fractionAfter = (viewport.getViewPositionY() + pointerY) / (n * (38.0 + band));
    CHECK (fractionAfter == Catch::Approx (fractionBefore).margin (1.0 / (n * (38.0 + band))));
    CHECK (viewport.getViewPositionY() == 329);   // (300 + 57) * 54 / 50 - 57, rounded

    // Shrinking to where the rows fit again pulls the view back to the top.
    Access::wheel (list, Region::Heads, ctrl, -100.0f, pointerY);
    CHECK (viewport.getViewPositionY() >= 0);
    CHECK (viewport.getViewPositionY() <= n * (30 + TrackRowComponent::instrumentBandHeight) - viewport.getMaximumVisibleHeight());
}

TEST_CASE ("TrackListComponent: the row height survives a rebuild that adds a track", "[track-list][wheel]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    addTracks (doc, 2);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 400);
    Access::wheel (list, Region::Heads, ctrl, 3.0f);
    REQUIRE (list.getRowHeight() == 46);

    addTrackEndingAt (doc, 52000);
    Access::rebuild (list);

    const int n = doc.getNumTracks();
    const int pitch = 46 + TrackRowComponent::instrumentBandHeight;   // notes height + band
    REQUIRE (Access::numRows (list) == n);
    int y = 0;
    for (auto* row : Access::rows (list))
    {
        CHECK (row->getHeight() == pitch);
        CHECK (row->getY() == y);
        y += pitch;
    }
    CHECK (Access::viewport (list).getViewedComponent()->getHeight() == n * pitch);
    // A later resize of the list keeps the height too.
    list.setBounds (0, 0, 500, 300);
    CHECK (Access::rows (list)[0]->getHeight() == pitch);
    CHECK (Access::viewport (list).getViewedComponent()->getHeight() == n * pitch);
}

TEST_CASE ("TrackListComponent: shift+wheel scrolls the timeline horizontally from either region and never touches the rows", "[track-list][wheel]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    addTracks (doc, 12);
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 200);
    Access::timelineViewMut (list).setPixelsPerTick (0.05);
    const double zoom = Access::timelineView (list).getPixelsPerTick();

    for (const auto region : { Region::Heads, Region::Canvas })
    {
        const double before = Access::scrollOffsetTicks (list);
        Access::wheel (list, region, shift, -1.0f);
        CHECK (Access::scrollOffsetTicks (list) > before);
    }
    CHECK (Access::timelineView (list).getPixelsPerTick() == Catch::Approx (zoom));
    CHECK (Access::viewport (list).getViewPositionY() == 0);
    CHECK (list.getRowHeight() == 34);
}

TEST_CASE ("TrackListComponent: wheel events reaching the viewport are routed by the pointer's region", "[track-list][wheel]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    addTracks (doc, 12);   // a vertical scrollbar is showing: the base Viewport would scroll everything
    TrackListComponent list (doc);
    list.setBounds (0, 0, 600, 200);
    auto& viewport = Access::viewport (list);
    auto* row = Access::rows (list)[1];
    const double zoom0 = Access::timelineView (list).getPixelsPerTick();

    auto wheelOnRow = [&] (int xInRow, float deltaY, juce::ModifierKeys mods)
    {
        juce::MouseWheelDetails details {};
        details.deltaY = deltaY;
        const auto pos = juce::Point<float> ((float) xInRow, 5.0f);
        viewport.mouseWheelMove (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                                   pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, row, row,
                                                   juce::Time::getCurrentTime(), pos,
                                                   juce::Time::getCurrentTime(), 1, false),
                                 details);
    };

    wheelOnRow (Access::notePreviewOriginX (list) + 20, 1.0f, {});   // canvas
    CHECK (Access::timelineView (list).getPixelsPerTick() > zoom0);
    CHECK (viewport.getViewPositionY() == 0);

    const double zoom1 = Access::timelineView (list).getPixelsPerTick();
    wheelOnRow (20, -1.0f, {});                                      // heads
    CHECK (viewport.getViewPositionY() > 0);
    CHECK (Access::timelineView (list).getPixelsPerTick() == Catch::Approx (zoom1));

    wheelOnRow (20, 1.0f, ctrl);                                     // ctrl resizes from the heads
    CHECK (list.getRowHeight() == 38);
    wheelOnRow (Access::notePreviewOriginX (list) + 20, 1.0f, ctrl); // and from the canvas
    CHECK (list.getRowHeight() == 42);
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

TEST_CASE ("TrackListComponent: with follow switched off a playing seek never scrolls the view; switching it on restores the page-flip", "[track-list][playhead]")
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
    CHECK (list.getFollowPlayhead());   // default: on

    controller.seekToTick (50000.0);
    list.setFollowPlayhead (false);
    list.followPlayheadForTesting (/*playing*/ true);
    CHECK (Access::scrollOffsetTicks (list) == Catch::Approx (0.0));

    list.setFollowPlayhead (true);
    list.followPlayheadForTesting (/*playing*/ true);
    CHECK (Access::scrollOffsetTicks (list) == Catch::Approx (50000.0));
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

    TrackListComponentTestAccess::click (list, ids[0], none);
    CHECK (list.getSelectedTrackIds() == std::set<juce::int64> { ids[0] });

    TrackListComponentTestAccess::click (list, ids[2], ctrl);
    CHECK (list.getSelectedTrackIds() == std::set<juce::int64> { ids[0], ids[2] });

    TrackListComponentTestAccess::click (list, ids[2], ctrl);   // toggles off
    CHECK (list.getSelectedTrackIds() == std::set<juce::int64> { ids[0] });

    // The Ctrl-click on ids[2] moved the anchor there, so Shift ranges from ids[2], not ids[0].
    TrackListComponentTestAccess::click (list, ids[3], shift);
    CHECK (list.getSelectedTrackIds() == std::set<juce::int64> { ids[2], ids[3] });

    TrackListComponentTestAccess::click (list, ids[0], none);
    TrackListComponentTestAccess::click (list, ids[3], shift);   // anchor ids[0] .. ids[3]
    CHECK (list.getSelectedTrackIds() == std::set<juce::int64> { ids[0], ids[1], ids[2], ids[3] });
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
    list.selectAllHeads();

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
    TrackListComponentTestAccess::click (*f.list, f.ids[2], kShift);
    CHECK (f.list->getSelectedTrackIds() == Sel { f.ids[2] });
}

TEST_CASE ("TrackListComponent: shift-click replaces the selection with the range, ctrl+shift extends it", "[track-list][selection]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SelectionFixture f (5);
    auto& l = *f.list;
    TrackListComponentTestAccess::click (l, f.ids[3], kNone);
    TrackListComponentTestAccess::click (l, f.ids[0], kCtrl);       // {3, 0}, anchor 0
    TrackListComponentTestAccess::click (l, f.ids[1], kShift);      // range 0..1 replaces
    CHECK (l.getSelectedTrackIds() == Sel { f.ids[0], f.ids[1] });
    CHECK (l.getSelectedTrackId() == f.ids[0]);                            // anchor stays on Shift

    TrackListComponentTestAccess::click (l, f.ids[4], kCtrl);       // {0,1,4}, anchor 4
    TrackListComponentTestAccess::click (l, f.ids[3], kCtrlShift);  // extends with 3..4
    CHECK (l.getSelectedTrackIds() == Sel { f.ids[0], f.ids[1], f.ids[3], f.ids[4] });

    TrackListComponentTestAccess::click (l, f.ids[1], kShift);      // upward range from anchor 4
    CHECK (l.getSelectedTrackIds() == Sel { f.ids[1], f.ids[2], f.ids[3], f.ids[4] });
}

TEST_CASE ("TrackListComponent: shift range across a conductor row skips it", "[track-list][selection]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SelectionFixture f (4, 1);
    TrackListComponentTestAccess::click (*f.list, f.ids[0], kNone);
    TrackListComponentTestAccess::click (*f.list, f.ids[3], kShift);
    CHECK (f.list->getSelectedTrackIds() == Sel { f.ids[0], f.ids[2], f.ids[3] });
}

TEST_CASE ("TrackListComponent: ctrl-toggling the last selected track off leaves nothing highlighted", "[track-list][selection]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SelectionFixture f (3);
    TrackListComponentTestAccess::click (*f.list, f.ids[1], kNone);
    CHECK (f.highlighted (f.ids[1]));
    TrackListComponentTestAccess::click (*f.list, f.ids[1], kCtrl);
    CHECK (f.list->getSelectedTrackIds().empty());
    CHECK_FALSE (f.highlighted (f.ids[1]));
}

TEST_CASE ("TrackListComponent: highlight follows the whole selection and survives a rebuild", "[track-list][selection]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SelectionFixture f (4);
    TrackListComponentTestAccess::click (*f.list, f.ids[0], kNone);
    TrackListComponentTestAccess::click (*f.list, f.ids[2], kCtrl);
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
    TrackListComponentTestAccess::click (*f.list, f.ids[0], kNone);
    f.doc.removeTrack (f.ids[0]);
    TrackListComponentTestAccess::rebuild (*f.list);
    CHECK (f.list->getSelectedTrackId() == -1);
    CHECK (f.list->getSelectedTrackIds().empty());
    TrackListComponentTestAccess::click (*f.list, f.ids[2], kShift);   // no anchor left
    CHECK (f.list->getSelectedTrackIds() == Sel { f.ids[2] });
}

TEST_CASE ("TrackListComponent: select-all with only a conductor track selects nothing", "[track-list][selection]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SelectionFixture f (1, 0);
    f.list->selectAllHeads();
    CHECK (f.list->getSelectedTrackIds().empty());
}

TEST_CASE ("TrackListComponent: plain click on a conductor highlights it without selecting it; selection changes leave the document untouched", "[track-list][selection]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SelectionFixture f (3, 1);
    const auto before = f.doc.getTree().createCopy();
    TrackListComponentTestAccess::click (*f.list, f.ids[0], kNone);
    TrackListComponentTestAccess::click (*f.list, f.ids[1], kNone);
    CHECK (f.list->getSelectedTrackIds().empty());
    CHECK (f.list->getSelectedTrackId() == f.ids[1]);
    CHECK (f.highlighted (f.ids[1]));
    CHECK_FALSE (f.highlighted (f.ids[0]));
    f.list->selectAllHeads();
    TrackListComponentTestAccess::click (*f.list, f.ids[2], kShift);   // anchor is the conductor
    CHECK (f.list->getSelectedTrackIds() == Sel { f.ids[2] });
    CHECK (f.doc.getTree().isEquivalentTo (before));
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

TEST_CASE ("TrackListComponent: cancelSectionDrag drops the preview, the release commits nothing, and the selection stays", "[track-list][sections]")
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

    CHECK_FALSE (list.cancelSectionDrag());   // nothing in flight: the key is not consumed

    const auto second = sectionsOf (t)[1];
    TrackListComponentTestAccess::press (list, id, { second.id, SectionZone::Body }, 700);
    TrackListComponentTestAccess::drag (list, 900);
    REQUIRE (TrackListComponentTestAccess::sectionView (list).drag.has_value());

    const auto before = doc.getTree().createCopy();
    CHECK (list.cancelSectionDrag());
    CHECK_FALSE (TrackListComponentTestAccess::sectionView (list).drag.has_value());
    CHECK (TrackListComponentTestAccess::sectionView (list).selected.count ({ id, second.id }) == 1);

    TrackListComponentTestAccess::drag (list, 1000);   // pointer still down after Escape
    CHECK_FALSE (TrackListComponentTestAccess::sectionView (list).drag.has_value());
    TrackListComponentTestAccess::release (list, 1000);
    CHECK (doc.getTree().isEquivalentTo (before));
    CHECK (sectionsOf (t)[1].startTick == 480);
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

namespace
{
    // Tracks of one note each, 0..960, every non-conductor track split at 480 into
    // [0,480) and [480,960) -- except `unsplit` (index), which keeps its single
    // virtual section. `conductor` (index) is the conductor. Viewed at 0.1 px/tick.
    struct CanvasFixture
    {
        SongDocument doc;
        std::vector<juce::int64> ids;
        std::unique_ptr<TrackListComponent> list;

        explicit CanvasFixture (int count, int conductor = -1, int unsplit = -1)
        {
            for (int i = 0; i < count; ++i)
            {
                auto t = playbacktest::addTrack (doc, "T");
                playbacktest::addNote (t, 60, 0, 960);
                if (i == conductor)
                    t.setProperty (SongIDs::isConductor, true, nullptr);
                ids.push_back (idOf (t));
            }
            std::vector<juce::int64> toSplit;
            for (int i = 0; i < count; ++i)
                if (i != conductor && i != unsplit)
                    toSplit.push_back (ids[(size_t) i]);
            splitAt (doc, toSplit, 480);
            doc.getUndoManager().clearUndoHistory();   // the setup split is not part of any test's undo count
            list = std::make_unique<TrackListComponent> (doc);
            list->setBounds (0, 0, 600, 300);
            TrackListComponentTestAccess::rebuild (*list);
            TrackListComponentTestAccess::timelineViewMut (*list).setPixelsPerTick (0.1);
            TrackListComponentTestAccess::timelineViewMut (*list).setScrollOffsetTicks (0.0);
        }

        SectionRef sec (int track, int index) const
        {
            return { ids[(size_t) track], sectionsOf (doc.findTrackById (ids[(size_t) track]))[(size_t) index].id };
        }
        // A press (and, unless `hold`, the release) on a section's body.
        void press (int track, int index, juce::ModifierKeys mods = {}, bool release = true)
        {
            const auto r = sec (track, index);
            const int tick = index == 0 ? 200 : 700;
            TrackListComponentTestAccess::press (*list, r.trackId, { r.sectionId, SectionZone::Body }, tick, mods);
            if (release)
                TrackListComponentTestAccess::release (*list, tick);
        }
        std::set<SectionRef> canvas() const { return TrackListComponentTestAccess::sectionView (*list).selected; }
        std::set<juce::int64> heads() const { return list->getSelectedTrackIds(); }
        bool highlighted (int track) const { return TrackListComponentTestAccess::rowFor (*list, ids[(size_t) track])->isSelected(); }
        int sections (int track) const { return (int) sectionsOf (doc.findTrackById (ids[(size_t) track])).size(); }
    };

    using Canvas = std::set<SectionRef>;
    using Heads = std::set<juce::int64>;
}

TEST_CASE ("TrackListComponent: a head click changes only the head selection and leaves the canvas selection alone", "[track-list][selection][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CanvasFixture f (4);
    f.press (1, 0);
    f.press (2, 1, kCtrl);
    const auto canvas = f.canvas();
    REQUIRE (canvas == Canvas { f.sec (1, 0), f.sec (2, 1) });

    TrackListComponentTestAccess::click (*f.list, f.ids[3], kNone);
    CHECK (f.heads() == Heads { f.ids[3] });
    CHECK (f.canvas() == canvas);

    TrackListComponentTestAccess::click (*f.list, f.ids[0], kCtrl);
    CHECK (f.heads() == Heads { f.ids[3], f.ids[0] });
    CHECK (f.canvas() == canvas);

    TrackListComponentTestAccess::click (*f.list, f.ids[1], kShift);   // range 0..1 from the Ctrl anchor
    CHECK (f.heads() == Heads { f.ids[0], f.ids[1] });
    CHECK (f.canvas() == canvas);

    TrackListComponentTestAccess::click (*f.list, f.ids[3], kCtrlShift);
    CHECK (f.canvas() == canvas);
}

TEST_CASE ("TrackListComponent: a plain canvas click selects one section and mirrors only its track onto the heads", "[track-list][selection][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CanvasFixture f (4);
    f.list->selectAllHeads();
    REQUIRE (f.heads().size() == 4);

    f.press (2, 1, kNone, false);   // press, still held
    CHECK (f.canvas() == Canvas { f.sec (2, 1) });
    CHECK (f.heads() == Heads { f.ids[2] });   // the other heads were dropped, not kept as companions
    CHECK (f.list->getSelectedTrackId() == f.ids[2]);
    CHECK (f.highlighted (2));
    CHECK_FALSE (f.highlighted (0));
    CHECK_FALSE (f.highlighted (1));
    CHECK_FALSE (f.highlighted (3));
    TrackListComponentTestAccess::release (*f.list, 700);

    // Heads no longer give a click companions: head-select three, click one section.
    f.list->selectAllHeads();
    f.press (0, 0);
    CHECK (f.canvas() == Canvas { f.sec (0, 0) });
}

TEST_CASE ("TrackListComponent: two heads and three canvas sections are highlighted independently", "[track-list][selection][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CanvasFixture f (5);
    f.press (0, 1);
    f.press (1, 1, kCtrl);
    f.press (2, 1, kCtrl);
    REQUIRE (f.canvas().size() == 3);

    TrackListComponentTestAccess::click (*f.list, f.ids[3], kNone);
    TrackListComponentTestAccess::click (*f.list, f.ids[4], kCtrl);
    CHECK (f.heads() == Heads { f.ids[3], f.ids[4] });
    CHECK (f.canvas() == Canvas { f.sec (0, 1), f.sec (1, 1), f.sec (2, 1) });
    for (int i = 0; i < 5; ++i)
        CHECK (f.highlighted (i) == (i >= 3));

    // The next canvas click mirrors again, replacing the head selection.
    f.press (1, 1, kCtrl);   // toggles that section off
    CHECK (f.canvas() == Canvas { f.sec (0, 1), f.sec (2, 1) });
    CHECK (f.heads() == Heads { f.ids[0], f.ids[2] });
    for (int i = 0; i < 5; ++i)
        CHECK (f.highlighted (i) == (i == 0 || i == 2));
}

TEST_CASE ("TrackListComponent: Ctrl-click toggles a section, starts no gesture and mirrors to the heads", "[track-list][selection][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CanvasFixture f (3);
    f.press (0, 1);
    f.press (2, 1, kCtrl);
    CHECK (f.canvas() == Canvas { f.sec (0, 1), f.sec (2, 1) });
    CHECK (f.heads() == Heads { f.ids[0], f.ids[2] });

    f.press (0, 1, kCtrl);
    CHECK (f.canvas() == Canvas { f.sec (2, 1) });
    CHECK (f.heads() == Heads { f.ids[2] });
    CHECK_FALSE (f.highlighted (0));

    // A Ctrl press never drags anything.
    TrackListComponentTestAccess::press (*f.list, f.ids[2], { f.sec (2, 1).sectionId, SectionZone::Body }, 700, kCtrl);
    TrackListComponentTestAccess::drag (*f.list, 900);
    CHECK_FALSE (TrackListComponentTestAccess::sectionView (*f.list).drag.has_value());
    TrackListComponentTestAccess::release (*f.list, 900);
    CHECK_FALSE (f.doc.canUndo());
    CHECK (sectionsOf (f.doc.findTrackById (f.ids[2]))[1].startTick == 480);
}

TEST_CASE ("TrackListComponent: Shift-click selects the sections starting where the anchor does across the track range", "[track-list][selection][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CanvasFixture f (4, -1, 2);   // track 2 is unsplit: no section starts at 480
    f.press (0, 1);               // anchor: track 0, start 480
    f.press (3, 1, kShift);
    CHECK (f.canvas() == Canvas { f.sec (0, 1), f.sec (1, 1), f.sec (3, 1) });   // track 2 skipped
    CHECK (f.heads() == Heads { f.ids[0], f.ids[1], f.ids[3] });
    CHECK (f.list->getSelectedTrackId() == f.ids[3]);

    f.press (1, 1, kShift);   // plain Shift replaces, from the same anchor
    CHECK (f.canvas() == Canvas { f.sec (0, 1), f.sec (1, 1) });
    CHECK (f.heads() == Heads { f.ids[0], f.ids[1] });

    // The range takes the anchor's start tick, not the clicked section's.
    f.press (1, 0);
    f.press (0, 0, kCtrl);                      // anchor: track 0, start 0
    f.press (3, 1, kShift);                     // clicked [480,..) on track 3, but the anchor starts at 0
    CHECK (f.canvas() == Canvas { f.sec (0, 0), f.sec (1, 0), f.sec (2, 0), f.sec (3, 0) });

    // Ctrl+Shift extends; the range runs upward too.
    f.press (3, 1);                             // anchor: track 3, start 480
    f.press (0, 0, kCtrl);                      // selection {3/1, 0/0}, anchor 0/0
    f.press (1, 0, kCtrlShift);                 // extends with track 1's start-0 section
    CHECK (f.canvas() == Canvas { f.sec (3, 1), f.sec (0, 0), f.sec (1, 0) });
    f.press (3, 1);
    f.press (0, 1, kShift);                     // upward from track 3
    CHECK (f.canvas() == Canvas { f.sec (0, 1), f.sec (1, 1), f.sec (3, 1) });
}

TEST_CASE ("TrackListComponent: Shift-click with no anchor acts as a plain click, and a conductor row in the range is skipped", "[track-list][selection][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CanvasFixture f (4, 1);
    f.press (2, 1, kShift);
    CHECK (f.canvas() == Canvas { f.sec (2, 1) });

    f.press (0, 1);
    f.press (3, 1, kShift);
    CHECK (f.canvas() == Canvas { f.sec (0, 1), f.sec (2, 1), f.sec (3, 1) });
    CHECK (f.heads() == Heads { f.ids[0], f.ids[2], f.ids[3] });
}

TEST_CASE ("TrackListComponent: a press on a multi-selected section keeps the selection and a drag moves all of it", "[track-list][selection][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CanvasFixture f (3);
    f.press (0, 1);
    f.press (2, 1, kCtrl);

    f.press (2, 1, kNone, false);
    CHECK (f.canvas() == Canvas { f.sec (0, 1), f.sec (2, 1) });   // kept on press
    TrackListComponentTestAccess::drag (*f.list, 900);
    TrackListComponentTestAccess::release (*f.list, 900);

    CHECK (sectionsOf (f.doc.findTrackById (f.ids[0]))[1].startTick == 680);
    CHECK (sectionsOf (f.doc.findTrackById (f.ids[1]))[1].startTick == 480);   // not selected
    CHECK (sectionsOf (f.doc.findTrackById (f.ids[2]))[1].startTick == 680);
    CHECK (f.canvas() == Canvas { f.sec (0, 1), f.sec (2, 1) });
    f.doc.undo();   // one step
    CHECK (sectionsOf (f.doc.findTrackById (f.ids[0]))[1].startTick == 480);
    CHECK (sectionsOf (f.doc.findTrackById (f.ids[2]))[1].startTick == 480);
}

TEST_CASE ("TrackListComponent: releasing a press on a multi-selected section without a drag collapses to it and opens no undo step", "[track-list][selection][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CanvasFixture f (3);
    f.press (0, 1);
    f.press (2, 1, kCtrl);
    f.press (1, 1, kCtrl);
    REQUIRE (f.canvas().size() == 3);
    REQUIRE_FALSE (f.doc.canUndo());

    f.press (1, 1);   // press + release at the press tick
    CHECK (f.canvas() == Canvas { f.sec (1, 1) });
    CHECK (f.heads() == Heads { f.ids[1] });
    CHECK (f.highlighted (1));
    CHECK_FALSE (f.highlighted (0));
    CHECK_FALSE (f.doc.canUndo());

    // A press on a section outside the selection replaces it at once.
    f.press (0, 0, kNone, false);
    CHECK (f.canvas() == Canvas { f.sec (0, 0) });
    TrackListComponentTestAccess::release (*f.list, 200);
}

TEST_CASE ("TrackListComponent: a plain click on empty strip clears the canvas selection and selects that head", "[track-list][selection][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CanvasFixture f (4, 3);
    f.press (0, 0);
    f.press (1, 0, kCtrl);
    REQUIRE (f.canvas().size() == 2);

    TrackListComponentTestAccess::press (*f.list, f.ids[2], {}, 5000);   // zone None
    CHECK (f.canvas().empty());
    CHECK (f.heads() == Heads { f.ids[2] });
    CHECK (f.highlighted (2));
    CHECK_FALSE (f.highlighted (0));

    // Ctrl or Shift on empty strip is not a "click on nothing": the selection stays.
    f.press (0, 0);
    TrackListComponentTestAccess::press (*f.list, f.ids[2], {}, 5000, kCtrl);
    TrackListComponentTestAccess::press (*f.list, f.ids[2], {}, 5000, kShift);
    CHECK (f.canvas() == Canvas { f.sec (0, 0) });

    // On the conductor: nothing to select, but it highlights as the clicked row.
    TrackListComponentTestAccess::press (*f.list, f.ids[3], {}, 400);
    CHECK (f.canvas().empty());
    CHECK (f.heads().empty());
    CHECK (f.highlighted (3));
}

TEST_CASE ("TrackListComponent: selectAllCanvases selects every section of every track, virtual ones included, and mirrors the heads", "[track-list][selection][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CanvasFixture f (4, 0, 2);   // conductor first; track 2 has only the virtual section 0
    f.list->selectAllCanvases();

    CHECK (f.canvas() == Canvas { f.sec (1, 0), f.sec (1, 1), SectionRef { f.ids[2], 0 }, f.sec (3, 0), f.sec (3, 1) });
    CHECK (f.heads() == Heads { f.ids[1], f.ids[2], f.ids[3] });   // never the conductor
    CHECK_FALSE (f.highlighted (0));
    CHECK (f.highlighted (1));
    CHECK (f.highlighted (2));
    CHECK (f.highlighted (3));
    CHECK_FALSE (f.doc.canUndo());

    // The virtual id selects like any other: a move of everything is one step.
    TrackListComponentTestAccess::press (*f.list, f.ids[3], { f.sec (3, 1).sectionId, SectionZone::Body }, 700);
    TrackListComponentTestAccess::drag (*f.list, 800);
    TrackListComponentTestAccess::release (*f.list, 800);
    CHECK (sectionsOf (f.doc.findTrackById (f.ids[2]))[0].startTick == 100);
    f.doc.undo();
    CHECK (sectionsOf (f.doc.findTrackById (f.ids[2]))[0].startTick == 0);
}

TEST_CASE ("TrackListComponent: selectAllHeads selects every non-conductor head and leaves the canvas selection alone", "[track-list][selection][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CanvasFixture f (4, 0);
    f.press (2, 1);
    f.list->selectAllHeads();
    CHECK (f.heads() == Heads { f.ids[1], f.ids[2], f.ids[3] });
    CHECK (f.canvas() == Canvas { f.sec (2, 1) });
    CHECK (f.highlighted (1));

    // selectAll() with the pointer nowhere near the strips is the heads variant.
    f.press (3, 0);
    f.list->selectAll();
    CHECK (f.heads() == Heads { f.ids[1], f.ids[2], f.ids[3] });
    CHECK (f.canvas() == Canvas { f.sec (3, 0) });
}

TEST_CASE ("TrackListComponent: S splits the tracks owning a selected section, not the selected heads", "[track-list][selection][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CanvasFixture f (4);
    f.press (0, 1);
    f.press (2, 1, kCtrl);
    TrackListComponentTestAccess::click (*f.list, f.ids[1], kNone);   // heads: track 1 only
    REQUIRE (f.heads() == Heads { f.ids[1] });

    CHECK (f.list->splitSections (700, f.ids[3]));
    CHECK (f.sections (0) == 3);
    CHECK (f.sections (1) == 2);   // selected head, but no selected section
    CHECK (f.sections (2) == 3);
    CHECK (f.sections (3) == 2);   // the pointer's track is not consulted either
    f.doc.undo();
    CHECK (f.sections (0) == 2);
    CHECK (f.sections (2) == 2);
}

TEST_CASE ("TrackListComponent: S with an empty canvas selection splits the pointer's track and ignores the selected heads", "[track-list][selection][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CanvasFixture f (3);
    f.list->selectAllHeads();
    REQUIRE (f.canvas().empty());

    CHECK (f.list->splitSections (700, f.ids[1]));
    CHECK (f.sections (0) == 2);
    CHECK (f.sections (1) == 3);
    CHECK (f.sections (2) == 2);

    CHECK_FALSE (f.list->splitSections (700, -1));   // no pointer track, no canvas: nothing, though heads are selected
    CHECK (f.sections (0) == 2);
}

TEST_CASE ("TrackListComponent: rebuild prunes the head and canvas selections independently and never re-mirrors", "[track-list][selection][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CanvasFixture f (4);
    f.press (0, 1);
    TrackListComponentTestAccess::click (*f.list, f.ids[2], kNone);   // heads {2}, canvas {0/1}

    // A dead head track does not touch the canvas selection, and the heads are not refilled from it.
    f.doc.removeTrack (f.ids[2]);
    TrackListComponentTestAccess::rebuild (*f.list);
    CHECK (f.heads().empty());
    CHECK (f.list->getSelectedTrackId() == -1);
    CHECK (f.canvas() == Canvas { f.sec (0, 1) });

    // A dead section does not touch the head selection.
    TrackListComponentTestAccess::click (*f.list, f.ids[3], kNone);
    const auto doomed = f.sec (0, 1);
    deleteSections (f.doc, { doomed });
    TrackListComponentTestAccess::rebuild (*f.list);
    CHECK (f.canvas().empty());
    CHECK (f.heads() == Heads { f.ids[3] });
    CHECK (f.highlighted (3));
}

TEST_CASE ("TrackListComponent: clearSelection forgets both the head and the canvas selection", "[track-list][selection][session-reset]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CanvasFixture f (2);
    f.press (0, 1);
    f.list->clearSelection();
    CHECK (f.heads().empty());
    CHECK (f.canvas().empty());
    CHECK_FALSE (f.highlighted (0));
    f.press (1, 1, kShift);   // and the Shift anchor is gone
    CHECK (f.canvas() == Canvas { f.sec (1, 1) });
}

TEST_CASE ("TrackListComponent: real strip clicks drive the canvas selection and the heads follow", "[track-list][selection][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CanvasFixture f (3);
    const auto left = juce::ModifierKeys::leftButtonModifier;
    const auto& view = TrackListComponentTestAccess::timelineView (*f.list);
    auto strip = [&] (int track) -> TrackNotePreview&
    { return TrackListComponentTestAccess::rowFor (*f.list, f.ids[(size_t) track])->notePreviewForTesting(); };

    auto& s0 = strip (0);
    s0.mouseDown (stripMouseAt (s0, view.xForTick (700)));
    s0.mouseUp (stripMouseAt (s0, view.xForTick (700)));
    CHECK (f.canvas() == Canvas { f.sec (0, 1) });
    CHECK (f.heads() == Heads { f.ids[0] });

    auto& s2 = strip (2);
    s2.mouseDown (stripMouseAt (s2, view.xForTick (700), juce::ModifierKeys (left | juce::ModifierKeys::ctrlModifier)));
    s2.mouseUp (stripMouseAt (s2, view.xForTick (700), juce::ModifierKeys (left | juce::ModifierKeys::ctrlModifier)));
    CHECK (f.canvas() == Canvas { f.sec (0, 1), f.sec (2, 1) });
    CHECK (f.heads() == Heads { f.ids[0], f.ids[2] });

    // Press on the selected section of track 2 and drag: both move; heads untouched by the gesture.
    s2.mouseDown (stripMouseAt (s2, view.xForTick (700)));
    s2.mouseDrag (stripMouseAt (s2, view.xForTick (900)));
    s2.mouseUp (stripMouseAt (s2, view.xForTick (900)));
    CHECK (sectionsOf (f.doc.findTrackById (f.ids[0]))[1].startTick == 680);
    CHECK (sectionsOf (f.doc.findTrackById (f.ids[2]))[1].startTick == 680);
    CHECK (f.heads() == Heads { f.ids[0], f.ids[2] });
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
    const juce::ModifierKeys ctrl (juce::ModifierKeys::ctrlModifier);
    TrackListComponentTestAccess::press (list, ids[0], { sectionsOf (a)[0].id, SectionZone::Body }, 500, ctrl);
    TrackListComponentTestAccess::press (list, ids[1], { sectionsOf (b)[0].id, SectionZone::Body }, 500, ctrl);

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

TEST_CASE ("TrackListComponent: a real strip drag moves every selected section as one undo step", "[track-list][sections]")
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

    // Select tracks 0 and 2's second sections (Ctrl-click on the canvas), then drag track 2's.
    const auto left = juce::ModifierKeys::leftButtonModifier;
    const juce::ModifierKeys leftCtrl (left | juce::ModifierKeys::ctrlModifier);
    const auto& view = TrackListComponentTestAccess::timelineView (list);
    auto& p0 = TrackListComponentTestAccess::rowFor (list, ids[0])->notePreviewForTesting();
    auto& preview = TrackListComponentTestAccess::rowFor (list, ids[2])->notePreviewForTesting();
    p0.mouseDown (stripMouseAt (p0, view.xForTick (700), leftCtrl));
    p0.mouseUp (stripMouseAt (p0, view.xForTick (700), leftCtrl));
    preview.mouseDown (stripMouseAt (preview, view.xForTick (700), leftCtrl));
    preview.mouseUp (stripMouseAt (preview, view.xForTick (700), leftCtrl));
    REQUIRE (TrackListComponentTestAccess::sectionView (list).selected.size() == 2);

    preview.mouseDown (stripMouseAt (preview, view.xForTick (700)));
    CHECK (TrackListComponentTestAccess::sectionView (list).selected.size() == 2);   // kept: it is part of the selection
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
            list.selectAllCanvases();
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
    list.selectAllCanvases();

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
    list.selectAllCanvases();

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
    list.selectAllHeads();
    CHECK (list.getSelectedTrackIds().empty());
    list.selectAllCanvases();
    CHECK (list.getSelectedTrackIds().empty());
    CHECK (TrackListComponentTestAccess::sectionView (list).selected.empty());
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

    TrackListComponentTestAccess::press (list, idA, { sectionsOf (a)[1].id, SectionZone::Body }, 700);
    TrackListComponentTestAccess::press (list, idB, { sectionsOf (b)[1].id, SectionZone::Body }, 700, juce::ModifierKeys::ctrlModifier);
    list.deleteSelectedSections();
    CHECK (sectionsOf (a).size() == 1);
    CHECK (sectionsOf (b).size() == 1);   // both selected sections went, in one step
    doc.undo();
    CHECK (sectionsOf (a).size() == 2);
    CHECK (sectionsOf (b).size() == 2);

    // Select, then remove the track: the rebuild prunes the stored selection.
    const auto sectionB = sectionsOf (b)[1].id;
    TrackListComponentTestAccess::press (list, idB, { sectionB, SectionZone::Body }, 700);
    TrackListComponentTestAccess::press (list, idA, { sectionsOf (a)[1].id, SectionZone::Body }, 700, juce::ModifierKeys::ctrlModifier);
    REQUIRE (TrackListComponentTestAccess::sectionView (list).selected.size() == 2);   // B's and A's
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

TEST_CASE ("TrackListComponent: canSplitAtMarker needs a marker inside a selected section; hasSelectedSections tracks the canvas selection", "[track-list][sections][menu]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto a = playbacktest::addTrack (doc, "A");
    playbacktest::addNote (a, 60, 0, 960);   // implicit section [0, 960)
    playbacktest::RecordingSink sink;
    PlaybackController playback (doc, sink);
    TrackListComponent list (doc);
    list.setPlayback (&playback);
    list.setBounds (0, 0, 600, 300);
    TrackListComponentTestAccess::rebuild (list);

    CHECK_FALSE (list.hasSelectedSections());
    CHECK_FALSE (list.canSplitAtMarker());           // nothing selected, no marker

    list.selectAllCanvases();
    CHECK (list.hasSelectedSections());
    CHECK_FALSE (list.canSplitAtMarker());           // selected, but no marker

    playback.setMarkerTick (960.0);
    CHECK_FALSE (list.canSplitAtMarker());           // marker on the section's end edge splits nothing
    playback.setMarkerTick (5000.0);
    CHECK_FALSE (list.canSplitAtMarker());           // marker beyond the section

    playback.setMarkerTick (600.0);
    CHECK (list.canSplitAtMarker());
    // It agrees with what the menu's split would really do.
    CHECK (list.splitSections (std::nullopt, -1));
    CHECK_FALSE (list.canSplitAtMarker());           // 600 is now a boundary

    list.clearSelection();
    playback.setMarkerTick (300.0);
    CHECK_FALSE (list.hasSelectedSections());
    CHECK_FALSE (list.canSplitAtMarker());           // marker inside a section, but none selected
}

namespace
{
    juce::Point<int> screenOf (TrackListComponent& list, juce::int64 trackId)
    {
        auto* row = TrackListComponentTestAccess::rowFor (list, trackId);
        REQUIRE (row != nullptr);
        return row->localPointToGlobal (row->getLocalBounds().getCentre());
    }

    struct MergeFixture
    {
        SongDocument doc;
        juce::ValueTree source, target;
        juce::int64 sourceId = 0, targetId = 0;
        std::unique_ptr<TrackListComponent> list;

        MergeFixture()
        {
            source = playbacktest::addTrack (doc, "S");
            target = playbacktest::addTrack (doc, "T");
            playbacktest::addNote (source, 60, 0, 480);
            sourceId = (juce::int64) source.getProperty (SongIDs::trackId);
            targetId = (juce::int64) target.getProperty (SongIDs::trackId);
            list = std::make_unique<TrackListComponent> (doc);
            list->setBounds (0, 0, 600, 300);
            TrackListComponentTestAccess::rebuild (*list);
        }

        int notesIn (const juce::ValueTree& t) const { return SongDocument::getNotesNode (t).getNumChildren(); }
    };

    const juce::ModifierKeys altMods { juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier };
    const juce::ModifierKeys altCtrlMods { juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier | juce::ModifierKeys::ctrlModifier };
}

TEST_CASE ("TrackListComponent: an Alt-drag onto another row moves the notes on release, in one undo step", "[track-list][merge]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    MergeFixture f;
    using A = TrackListComponentTestAccess;

    A::mergePress (*f.list, f.sourceId, { 0, SectionZone::Body }, altMods);
    CHECK (A::mergeDrag (*f.list, screenOf (*f.list, f.targetId), altMods));
    REQUIRE (A::sectionView (*f.list).merge.has_value());
    CHECK (A::sectionView (*f.list).merge->valid);
    CHECK_FALSE (A::sectionView (*f.list).merge->copy);
    CHECK (f.notesIn (f.target) == 0);   // nothing touched mid-drag

    A::mergeRelease (*f.list, screenOf (*f.list, f.targetId), altMods);
    CHECK (f.notesIn (f.target) == 1);
    CHECK (f.notesIn (f.source) == 0);
    CHECK_FALSE (A::sectionView (*f.list).merge.has_value());

    f.doc.undo();
    CHECK (f.notesIn (f.source) == 1);
    CHECK (f.notesIn (f.target) == 0);
}

TEST_CASE ("TrackListComponent: Ctrl at release makes the merge a copy", "[track-list][merge]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    MergeFixture f;
    using A = TrackListComponentTestAccess;

    A::mergePress (*f.list, f.sourceId, { 0, SectionZone::Body }, altMods);
    A::mergeDrag (*f.list, screenOf (*f.list, f.targetId), altMods);
    CHECK_FALSE (A::sectionView (*f.list).merge->copy);
    A::mergeDrag (*f.list, screenOf (*f.list, f.targetId), altCtrlMods);   // Ctrl pressed mid-drag
    CHECK (A::sectionView (*f.list).merge->copy);
    A::mergeRelease (*f.list, screenOf (*f.list, f.targetId), altCtrlMods);

    CHECK (f.notesIn (f.target) == 1);
    CHECK (f.notesIn (f.source) == 1);
}

TEST_CASE ("TrackListComponent: releasing over the source, the conductor or nothing merges nothing", "[track-list][merge]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    MergeFixture f;
    using A = TrackListComponentTestAccess;
    const auto conductorId = (juce::int64) f.doc.getConductorTrack().getProperty (SongIDs::trackId);

    for (const auto where : { screenOf (*f.list, f.sourceId), screenOf (*f.list, conductorId),
                              f.list->localPointToGlobal (juce::Point<int> (5, 299)) })
    {
        A::mergePress (*f.list, f.sourceId, { 0, SectionZone::Body }, altMods);
        CHECK_FALSE (A::mergeDrag (*f.list, where, altMods));
        A::mergeRelease (*f.list, where, altMods);
    }
    CHECK (f.notesIn (f.target) == 0);
    CHECK (f.notesIn (f.source) == 1);
    CHECK_FALSE (f.doc.canUndo());
}

TEST_CASE ("TrackListComponent: a merge press that never drags, or is cancelled, merges nothing", "[track-list][merge]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    MergeFixture f;
    using A = TrackListComponentTestAccess;

    A::mergePress (*f.list, f.sourceId, { 0, SectionZone::Body }, altMods);
    A::mergeRelease (*f.list, screenOf (*f.list, f.targetId), altMods);   // no drag: a click
    CHECK (f.notesIn (f.target) == 0);

    CHECK_FALSE (f.list->cancelSectionDrag());   // nothing in flight
    A::mergePress (*f.list, f.sourceId, { 0, SectionZone::Body }, altMods);
    A::mergeDrag (*f.list, screenOf (*f.list, f.targetId), altMods);
    CHECK (f.list->cancelSectionDrag());
    CHECK_FALSE (A::sectionView (*f.list).merge.has_value());
    A::mergeDrag (*f.list, screenOf (*f.list, f.targetId), altMods);   // pointer still down after Esc
    A::mergeRelease (*f.list, screenOf (*f.list, f.targetId), altMods);
    CHECK (f.notesIn (f.target) == 0);
    CHECK_FALSE (f.doc.canUndo());
}

TEST_CASE ("TrackListComponent: a merge carries only the pressed section when it is not in the selection", "[track-list][merge]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    MergeFixture f;
    using A = TrackListComponentTestAccess;
    playbacktest::addNote (f.source, 62, 480, 480);
    splitAt (f.doc, { f.sourceId }, 480);
    TrackListComponentTestAccess::rebuild (*f.list);
    const auto sections = sectionsOf (f.source);
    REQUIRE (sections.size() == 2);

    A::mergePress (*f.list, f.sourceId, { sections[0].id, SectionZone::Body }, altMods);
    A::mergeDrag (*f.list, screenOf (*f.list, f.targetId), altMods);
    A::mergeRelease (*f.list, screenOf (*f.list, f.targetId), altMods);
    CHECK (f.notesIn (f.target) == 1);
    CHECK (f.notesIn (f.source) == 1);
}

TEST_CASE ("TrackListComponent: a press on empty strip or the conductor starts no merge", "[track-list][merge]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    MergeFixture f;
    using A = TrackListComponentTestAccess;
    const auto conductorId = (juce::int64) f.doc.getConductorTrack().getProperty (SongIDs::trackId);

    A::mergePress (*f.list, f.sourceId, { 0, SectionZone::None }, altMods);
    CHECK_FALSE (A::mergeDrag (*f.list, screenOf (*f.list, f.targetId), altMods));
    A::mergePress (*f.list, conductorId, { 0, SectionZone::Body }, altMods);
    CHECK_FALSE (A::mergeDrag (*f.list, screenOf (*f.list, f.targetId), altMods));
    CHECK_FALSE (A::sectionView (*f.list).merge.has_value());
}

TEST_CASE ("TrackListComponent: with a multi-track selection, releasing over the pressed row merges nothing", "[track-list][merge]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    MergeFixture f;
    using A = TrackListComponentTestAccess;
    auto other = playbacktest::addTrack (f.doc, "O");
    playbacktest::addNote (other, 64, 0, 480);
    const auto otherId = (juce::int64) other.getProperty (SongIDs::trackId);
    A::rebuild (*f.list);
    A::sectionView (*f.list).selected = { { f.sourceId, 0 }, { otherId, 0 } };

    A::mergePress (*f.list, f.sourceId, { 0, SectionZone::Body }, altMods);
    CHECK_FALSE (A::mergeDrag (*f.list, screenOf (*f.list, f.sourceId), altMods));
    A::mergeRelease (*f.list, screenOf (*f.list, f.sourceId), altMods);

    CHECK (f.notesIn (f.source) == 1);
    CHECK (f.notesIn (other) == 1);
    CHECK_FALSE (f.doc.canUndo());
}

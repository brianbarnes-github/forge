// Verifies TrackListComponent::rebuild() clears a stale selection once the
// previously-selected track disappears from SOURCE_MIDI — e.g. after
// SongDocument::removeTrack — without over-pruning one that is still live.

#include "UI/SongDocument.h"
#include "UI/SongModelBridge.h"
#include "UI/TrackListComponent.h"

#include <catch2/catch_test_macros.hpp>

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
        static juce::int64 selectedTrackId (const TrackListComponent& c) { return c.selectedTrackId; }
        static const TimelineViewState& timelineView (const TrackListComponent& c) { return c.timelineView; }
        static int notePreviewOriginX (const TrackListComponent& c) { return c.notePreviewOriginX(); }
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
    REQUIRE (Access::numRows (list) == 0);

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

    auto& row = *TrackListComponentTestAccess::rows (list).getFirst();
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

    auto& row = *TrackListComponentTestAccess::rows (list).getFirst();
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

    auto& preview = Access::rows (list).getFirst()->notePreviewForTesting();
    REQUIRE (preview.toggleGhostIfHit (preview.ghostToggleBounds().getCentre()));
    REQUIRE (ghosted.count (idA) == 1);

    doc.addTrack ("Track B", (int) 0xFFDDEEFFu, 1, 0);
    Access::rebuild (list);

    auto rows = Access::rows (list);
    REQUIRE (rows.size() == 2);
    CHECK (rows.getFirst()->notePreviewForTesting().isGhostVisible());
    CHECK_FALSE (rows[1]->notePreviewForTesting().isGhostVisible());
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
    doc.addTrack ("Track A", (int) 0xFFAABBCCu, 0, 0);

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

    juce::MouseWheelDetails wheel;
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

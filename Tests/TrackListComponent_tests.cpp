// Verifies TrackListComponent::rebuild() clears a stale selection (and fires
// onTrackSelected with the cleared sentinel) once the previously-selected
// track disappears from SOURCE_MIDI — e.g. after SongDocument::removeTrack.

#include "UI/SongDocument.h"
#include "UI/SongModelBridge.h"
#include "UI/TrackListComponent.h"

#include <catch2/catch_test_macros.hpp>

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

    juce::int64 lastCallback = -2; // distinct from both real ids and the -1 sentinel
    int callbackCount = 0;
    list.onTrackSelected = [&] (juce::int64 id)
    {
        lastCallback = id;
        ++callbackCount;
    };

    Access::selectTrack (list, track1Id);
    CHECK (callbackCount == 1);
    CHECK (lastCallback == track1Id);

    doc.removeTrack (track1Id);
    Access::rebuild (list);

    CHECK (callbackCount == 2);
    CHECK (lastCallback == -1);
}

TEST_CASE ("TrackListComponent: rebuild() re-fires onTrackSelected for a still-live selection too", "[piano-roll]")
{
    // I1: a rebuild means something under SOURCE_MIDI changed, which can
    // affect a live selection's document state (e.g. ticksPerQuarter after a
    // second import) without the selected track itself disappearing —
    // callers need onTrackSelected to re-fire so they re-read fresh state,
    // not just on the disappearance case M5 originally covered.
    SongDocument doc;
    auto track1 = doc.addTrack ("Track 1", (int) 0xFF7FA8D0, 0, 0);
    const auto track1Id = (juce::int64) track1.getProperty (SongIDs::trackId);

    TrackListComponent list (doc);

    juce::int64 lastCallback = -2;
    int callbackCount = 0;
    list.onTrackSelected = [&] (juce::int64 id)
    {
        lastCallback = id;
        ++callbackCount;
    };

    Access::selectTrack (list, track1Id);
    CHECK (callbackCount == 1);

    Access::rebuild (list);

    // The selected track is still present, but rebuild() must still re-fire
    // onTrackSelected with the same (live) trackId, not clear the selection.
    CHECK (callbackCount == 2);
    CHECK (lastCallback == track1Id);
}

TEST_CASE ("TrackListComponent: rebuild() re-fires a live selection after a second import raises ticksPerQuarter", "[piano-roll]")
{
    // Reproduces I1: a second MIDI import at a higher PPQ than the first
    // forces an LCM raise of the document's ticksPerQuarter (and rescales
    // every existing note's ticks) without removing or adding a selected
    // track. A caller (SongsmithMainComponent) that only refreshes on
    // onTrackSelected firing must still see this, or it keeps rendering the
    // piano roll against the stale, pre-raise PPQ.
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

    juce::int64 lastCallback = -2;
    int callbackCount = 0;
    list.onTrackSelected = [&] (juce::int64 id)
    {
        lastCallback = id;
        ++callbackCount;
    };

    Access::selectTrack (list, track1Id);
    REQUIRE (callbackCount == 1);
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

    // rebuild() must re-fire onTrackSelected for the still-live track1Id, so
    // a caller re-reading ticksPerQuarter from doc at that point gets 480,
    // not the stale 120 it read at selection time.
    CHECK (callbackCount == 2);
    CHECK (lastCallback == track1Id);
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

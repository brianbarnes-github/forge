// Reproduces the stale-preview bug: removing the currently-previewed PART
// node does not fire valueTreeChildRemoved on that node (ValueTree only
// notifies the removed child's PARENT), only valueTreeParentChanged on the
// removed node itself. See juce-valuetree-conventions and the fix in
// SongsmithMainComponent::valueTreeParentChanged.

#include "UI/SongDocument.h"
#include "UI/SongsmithMainComponent.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace lotro
{
    // Grants SongsmithMainComponent_tests.cpp access to selectPartForPreview()
    // and the private preview-state members, which stay private on the class
    // itself (see the friend declaration in SongsmithMainComponent.h).
    struct SongsmithMainComponentTestAccess
    {
        static void selectPartForPreview (SongsmithMainComponent& c, juce::int64 partId)
        {
            c.selectPartForPreview (partId);
        }
        static bool hasPreviewNoteSource (const SongsmithMainComponent& c)
        {
            return c.currentPreviewNoteSource != nullptr;
        }
        static bool hasWatchedPartNode (const SongsmithMainComponent& c)
        {
            return c.watchedPartNode.isValid();
        }
        static bool hasTrackEditorWindow (const SongsmithMainComponent& c)
        {
            return c.trackEditorWindow != nullptr;
        }
        static juce::int64 trackEditorWindowTrackId (const SongsmithMainComponent& c)
        {
            return c.trackEditorWindow != nullptr ? c.trackEditorWindow->getTrackId() : -1;
        }
        static void trackDoubleClicked (SongsmithMainComponent& c, juce::int64 trackId)
        {
            c.trackDoubleClicked (trackId);
        }
        static TrackListComponent& trackList (SongsmithMainComponent& c)
        {
            return c.trackList;
        }
        static int activeEditorGridTicks (const SongsmithMainComponent& c)
        {
            return c.trackEditorWindow != nullptr ? c.trackEditorWindow->getGridTicks() : -1;
        }
    };
}

namespace
{
    using Access = SongsmithMainComponentTestAccess;
}

TEST_CASE ("SongsmithMainComponent: removing the previewed part clears the stale preview", "[piano-roll]")
{
    // Exercises the real ValueTree::Listener -> AsyncUpdater -> recompute
    // chain end to end (real doc.removePart mutation, real pumped message
    // loop), not a direct call into the clearing branch — that is exactly
    // the path the bug lived on (valueTreeParentChanged was a no-op, so
    // nothing downstream ever ran).
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto part = doc.addPart ("Lute", "Part 1");
    const auto partId = (juce::int64) part.getProperty (SongIDs::partId);

    SongsmithMainComponent main (doc);
    Access::selectPartForPreview (main, partId);

    REQUIRE (Access::hasWatchedPartNode (main));
    REQUIRE (Access::hasPreviewNoteSource (main));

    doc.removePart (partId);

    // The removal notified listeners synchronously; the recompute itself is
    // coalesced through AsyncUpdater, same as TrackListComponent's real-
    // import test. Pump a bounded real message loop to let it run.
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);

    CHECK_FALSE (Access::hasWatchedPartNode (main));
    CHECK_FALSE (Access::hasPreviewNoteSource (main));
}

TEST_CASE ("SongsmithMainComponent: double-clicking a track opens the editor window", "[track-editor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCCu, 0, 0);
    const auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);

    SongsmithMainComponent main (doc);

    CHECK_FALSE (main.isTrackEditorOpen());

    Access::trackDoubleClicked (main, trackId);

    CHECK (main.isTrackEditorOpen());
    CHECK (Access::trackEditorWindowTrackId (main) == trackId);
}

TEST_CASE ("SongsmithMainComponent: double-clicking a second track re-points the existing window rather than opening a new one", "[track-editor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto trackA = doc.addTrack ("Track A", (int) 0xFFAABBCCu, 0, 0);
    auto trackB = doc.addTrack ("Track B", (int) 0xFFDDEEFFu, 1, 0);
    const auto idA = (juce::int64) trackA.getProperty (SongIDs::trackId);
    const auto idB = (juce::int64) trackB.getProperty (SongIDs::trackId);

    SongsmithMainComponent main (doc);

    Access::trackDoubleClicked (main, idA);
    CHECK (Access::trackEditorWindowTrackId (main) == idA);

    Access::trackDoubleClicked (main, idB);
    CHECK (Access::trackEditorWindowTrackId (main) == idB);
    CHECK (main.isTrackEditorOpen());
}

TEST_CASE ("SongsmithMainComponent: setActiveEditorGridSize resolves ticks against the document's own ticksPerQuarter", "[track-editor]")
{
    // The whole point of taking a GridSize rather than a raw tick count: the
    // same menu selection has to mean a different number of ticks in a
    // 480-PPQ document than in a 120-PPQ one. A hardcoded constant would
    // satisfy the 480 case alone, so both are checked.
    juce::ScopedJuceInitialiser_GUI juceInit;

    for (const int ppq : { 480, 120 })
    {
        SongDocument doc;
        auto track = doc.addTrack ("Track A", (int) 0xFFAABBCCu, 0, 0);
        const auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);
        doc.getSourceMidiNode().setProperty (SongIDs::ticksPerQuarter, ppq, nullptr);

        SongsmithMainComponent main (doc);
        Access::trackDoubleClicked (main, trackId);
        REQUIRE (main.isTrackEditorOpen());

        main.setActiveEditorGridSize (GridSize::Quarter);
        CHECK (Access::activeEditorGridTicks (main) == ppq);

        main.setActiveEditorGridSize (GridSize::Eighth);
        CHECK (Access::activeEditorGridTicks (main) == ppq / 2);

        main.setActiveEditorGridSize (GridSize::Sixteenth);
        CHECK (Access::activeEditorGridTicks (main) == ppq / 4);

        // "Off" means no grid at all, not a tick count.
        main.setActiveEditorGridSize (GridSize::Off);
        CHECK (Access::activeEditorGridTicks (main) == 0);
    }
}

TEST_CASE ("SongsmithMainComponent: a ghosted row's eye icon survives a SOURCE_MIDI rebuild", "[track-editor]")
{
    // The end-to-end version of TrackListComponent's own isTrackGhosted test:
    // proves the callback is actually wired to ghostedTrackIds in production,
    // which is the half that was missing -- ghostedTrackIds kept driving the
    // overlays while every eye icon reset to "off" on the next rebuild.
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto trackA = doc.addTrack ("Track A", (int) 0xFFAABBCCu, 0, 0);
    const auto idA = (juce::int64) trackA.getProperty (SongIDs::trackId);

    SongsmithMainComponent main (doc);
    main.setSize (900, 700);

    main.trackGhostToggled (idA, true);

    auto trackB = doc.addTrack ("Track B", (int) 0xFFDDEEFFu, 1, 0);
    const auto idB = (juce::int64) trackB.getProperty (SongIDs::trackId);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);

    auto& list = Access::trackList (main);
    REQUIRE (list.isTrackGhosted != nullptr);
    CHECK (list.isTrackGhosted (idA));
    CHECK_FALSE (list.isTrackGhosted (idB));

    main.trackGhostToggled (idA, false);
    CHECK_FALSE (list.isTrackGhosted (idA));
}

TEST_CASE ("SongsmithMainComponent: quantizeActiveEditor/setActiveEditorGridTicks are no-ops when no editor is open", "[track-editor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    SongsmithMainComponent main (doc);

    CHECK_FALSE (main.isTrackEditorOpen());
    CHECK_NOTHROW (main.quantizeActiveEditor());
    CHECK_NOTHROW (main.setActiveEditorGridTicks (240));
}

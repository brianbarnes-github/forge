// Reproduces the stale-preview bug: removing the currently-previewed PART
// node does not fire valueTreeChildRemoved on that node (ValueTree only
// notifies the removed child's PARENT), only valueTreeParentChanged on the
// removed node itself. See juce-valuetree-conventions and the fix in
// SongsmithMainComponent::valueTreeParentChanged.

#include "PartStripTestAccess.h"
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
        static juce::int64 trackEditorWindowTrackId (const SongsmithMainComponent& c)
        {
            return c.trackEditorWindow != nullptr ? c.trackEditorWindow->getTrackId() : -1;
        }
        static void trackDoubleClicked (SongsmithMainComponent& c, juce::int64 trackId)
        {
            c.trackDoubleClicked (trackId);
        }
        static PartStripComponent& partStrip (SongsmithMainComponent& c) { return c.partStrip; }
        static std::size_t ghostedCount (const SongsmithMainComponent& c) { return c.ghostedTrackIds.size(); }
        static juce::int64 selectedPreviewPartId (const SongsmithMainComponent& c) { return c.selectedPreviewPartId; }
        static void trackGhostToggled (SongsmithMainComponent& c, juce::int64 id, bool v) { c.trackGhostToggled (id, v); }
        static TrackListComponent& trackList (SongsmithMainComponent& c)
        {
            return c.trackList;
        }
        static int activeEditorGridTicks (const SongsmithMainComponent& c)
        {
            return c.trackEditorWindow != nullptr ? c.trackEditorWindow->getGridTicks() : -1;
        }
        // previewRegion is a child of lowerRegion, so its bounds are already
        // relative to lowerRegion's own local space -- compare against
        // lowerRegion's height (not lowerRegion.getBottom(), which is in
        // lowerRegion's PARENT's coordinate space, a different frame).
        static int lowerRegionHeight (const SongsmithMainComponent& c)
        {
            return c.lowerRegion.getHeight();
        }
        static int previewRegionBottom (const SongsmithMainComponent& c)
        {
            return c.previewRegion.getBottom();
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

TEST_CASE ("SongsmithMainComponent: quantizeActiveEditor/setActiveEditorGridSize are no-ops when no editor is open", "[track-editor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    SongsmithMainComponent main (doc);

    CHECK_FALSE (main.isTrackEditorOpen());
    CHECK_NOTHROW (main.quantizeActiveEditor());
    CHECK_NOTHROW (main.setActiveEditorGridSize (GridSize::Quarter));
}

TEST_CASE ("SongsmithMainComponent: diagnostics list is hidden by default and its space goes to the preview region",
           "[piano-roll]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    SongsmithMainComponent main (doc);
    main.setSize (900, 700);

    CHECK_FALSE (main.isDiagnosticsVisible());
    CHECK_FALSE (main.getDiagnostics().isVisible());
    // With diagnostics hidden, the preview region alone fills all of
    // lowerRegion's height below the part strip -- no dead gap left where
    // the diagnostics list used to share space via the inner splitter.
    CHECK (Access::previewRegionBottom (main) == Access::lowerRegionHeight (main));
}

TEST_CASE ("SongsmithMainComponent: View -> Diagnostics list toggle shows it again and shrinks the preview region",
           "[piano-roll]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    SongsmithMainComponent main (doc);
    main.setSize (900, 700);

    main.setDiagnosticsVisible (true);

    CHECK (main.isDiagnosticsVisible());
    CHECK (main.getDiagnostics().isVisible());
    CHECK (Access::previewRegionBottom (main) < Access::lowerRegionHeight (main));

    main.setDiagnosticsVisible (false);

    CHECK_FALSE (main.isDiagnosticsVisible());
    CHECK (Access::previewRegionBottom (main) == Access::lowerRegionHeight (main));
}

TEST_CASE ("SongsmithMainComponent: documentReplaced closes the editor and clears UI state outside the tree", "[session-reset]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCCu, 1, 1);
    const auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);
    auto part = doc.addPart ("Lute of Ages", "Part 1");
    const auto partId = (juce::int64) part.getProperty (SongIDs::partId);

    SongsmithMainComponent main (doc);
    Access::trackDoubleClicked (main, trackId);
    Access::trackGhostToggled (main, trackId, true);
    PartStripComponentTestAccess::selectPart (Access::partStrip (main), partId);   // also previews it, via onPartSelected
    REQUIRE (main.isTrackEditorOpen());
    REQUIRE (Access::ghostedCount (main) == 1);
    REQUIRE (Access::selectedPreviewPartId (main) == partId);
    REQUIRE (Access::partStrip (main).getSelectedPartId() == partId);

    main.documentReplaced();

    CHECK_FALSE (main.isTrackEditorOpen());
    CHECK (Access::ghostedCount (main) == 0);
    CHECK (Access::selectedPreviewPartId (main) == -1);
    CHECK_FALSE (Access::hasWatchedPartNode (main));
    CHECK_FALSE (Access::hasPreviewNoteSource (main));
    CHECK (Access::partStrip (main).getSelectedPartId() == -1);
    CHECK (Access::trackList (main).getSelectedTrackId() == -1);
}

TEST_CASE ("SongsmithMainComponent: after a document swap an old id never re-selects the new Song's part", "[session-reset]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto part = doc.addPart ("Lute of Ages", "Old");
    SongsmithMainComponent main (doc);
    PartStripComponentTestAccess::selectPart (Access::partStrip (main), (juce::int64) part.getProperty (SongIDs::partId));

    // A different Song whose first part has the SAME id (ids are per-Song counters).
    SongDocument other;
    other.addPart ("Harp", "New");

    doc.replaceContents (other.getTree());
    main.documentReplaced();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);   // let the async listener updates run

    CHECK (Access::selectedPreviewPartId (main) == -1);
    CHECK_FALSE (Access::hasPreviewNoteSource (main));
}

TEST_CASE ("SongsmithMainComponent: documentReplaced writes nothing to the tree", "[session-reset]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    doc.addTrack ("Track A", (int) 0xFFAABBCCu, 1, 1);
    SongsmithMainComponent main (doc);

    const auto before = doc.getTree().createCopy();
    main.documentReplaced();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);

    CHECK (doc.getTree().isEquivalentTo (before));
}

// Reproduces the stale-preview bug: removing the currently-previewed PART
// node does not fire valueTreeChildRemoved on that node (ValueTree only
// notifies the removed child's PARENT), only valueTreeParentChanged on the
// removed node itself. See juce-valuetree-conventions and the fix in
// SongsmithMainComponent::valueTreeParentChanged.

#include "PartStripTestAccess.h"
#include "UI/SongDocument.h"
#include "PlaybackTestSupport.h"
#include "UI/Playback/PlaybackController.h"
#include "UI/Playback/TransportStrip.h"
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
        static juce::String trackEditorWindowTitle (const SongsmithMainComponent& c)
        {
            return c.trackEditorWindow != nullptr ? c.trackEditorWindow->getName() : juce::String();
        }
        static int trackEditorAppearanceRepaints (const SongsmithMainComponent& c)
        {
            return c.trackEditorWindow != nullptr ? c.trackEditorWindow->appearanceRepaintsForTesting() : -1;
        }
        static bool previewUpdatePending (const SongsmithMainComponent& c) { return c.isUpdatePending(); }
        static void trackDoubleClicked (SongsmithMainComponent& c, juce::int64 trackId)
        {
            c.trackDoubleClicked (trackId);
        }
        static PartStripComponent& partStrip (SongsmithMainComponent& c) { return c.partStrip; }
        static TransportStrip* transportStrip (SongsmithMainComponent& c) { return c.transportStrip.get(); }
        static juce::Component& upperRegion (SongsmithMainComponent& c) { return c.upperRegion; }
        static juce::Component& lowerRegion (SongsmithMainComponent& c) { return c.lowerRegion; }
        static SplitterComponent& splitter (SongsmithMainComponent& c) { return c.splitter; }
        static std::size_t ghostedCount (const SongsmithMainComponent& c) { return c.ghostedTrackIds.size(); }
        static juce::int64 selectedPreviewPartId (const SongsmithMainComponent& c) { return c.selectedPreviewPartId; }
        static void trackGhostToggled (SongsmithMainComponent& c, juce::int64 id, bool v) { c.trackGhostToggled (id, v); }
        static TrackListComponent& trackList (SongsmithMainComponent& c)
        {
            return c.trackList;
        }
        static PianoRollComponent& previewRoll (SongsmithMainComponent& c) { return c.previewRoll; }
        static bool activeEditorFollows (const SongsmithMainComponent& c)
        {
            return c.trackEditorWindow != nullptr && c.trackEditorWindow->getFollowPlayhead();
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

TEST_CASE ("SongsmithMainComponent: a rename or recolour retitles the open editor and repaints the part strip's chips; volume does neither", "[track-editor][head]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCCu, 0, 0);
    const auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);
    lotro::playbacktest::addNote (track, 60, 0, 480);
    doc.assignTrackToPart ((juce::int64) doc.addPart ("Lute", "Part 1").getProperty (SongIDs::partId), trackId);

    SongsmithMainComponent main (doc);
    Access::trackDoubleClicked (main, trackId);
    REQUIRE (Access::trackEditorWindowTitle (main) == "Edit Track: Track A");
    const int stripBefore = PartStripComponentTestAccess::appearanceRepaints (Access::partStrip (main));
    const int editorBefore = Access::trackEditorAppearanceRepaints (main);

    doc.setProperty (track, SongIDs::playbackVolume, 40);
    CHECK (PartStripComponentTestAccess::appearanceRepaints (Access::partStrip (main)) == stripBefore);
    CHECK (Access::trackEditorAppearanceRepaints (main) == editorBefore);

    doc.setProperty (track, SongIDs::colorArgb, (int) 0xFF112233u);
    CHECK (PartStripComponentTestAccess::appearanceRepaints (Access::partStrip (main)) == stripBefore + 1);
    CHECK (Access::trackEditorAppearanceRepaints (main) == editorBefore + 1);

    doc.setProperty (track, SongIDs::name, "Harp");
    CHECK (Access::trackEditorWindowTitle (main) == "Edit Track: Harp");
    doc.getUndoManager().undo();
    CHECK (Access::trackEditorWindowTitle (main) == "Edit Track: Track A");
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
}

TEST_CASE ("SongsmithMainComponent: a volume change on a previewed part's track does not recompute the preview; a colour change does", "[track-editor][head][volume]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCCu, 0, 0);
    const auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);
    lotro::playbacktest::addNote (track, 60, 0, 480);
    const auto partId = (juce::int64) doc.addPart ("Lute", "Part 1").getProperty (SongIDs::partId);
    REQUIRE (doc.assignTrackToPart (partId, trackId));

    SongsmithMainComponent main (doc);
    Access::selectPartForPreview (main, partId);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    REQUIRE_FALSE (Access::previewUpdatePending (main));

    doc.setProperty (track, SongIDs::playbackVolume, 40);
    CHECK_FALSE (Access::previewUpdatePending (main));
    doc.setProperty (track, SongIDs::colorArgb, (int) 0xFF112233u);
    CHECK (Access::previewUpdatePending (main));
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
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

TEST_CASE ("SongsmithMainComponent: a newly opened editor starts on the default grid; an open one keeps its grid; the menu still overrides", "[track-editor][view-settings]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCCu, 0, 0);
    const auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);
    doc.getSourceMidiNode().setProperty (SongIDs::ticksPerQuarter, 480, nullptr);

    SongsmithMainComponent main (doc);
    main.setDefaultGridSize (GridSize::Eighth);
    Access::trackDoubleClicked (main, trackId);
    REQUIRE (main.isTrackEditorOpen());
    CHECK (Access::activeEditorGridTicks (main) == 240);

    main.setDefaultGridSize (GridSize::Quarter);          // changing the default...
    CHECK (Access::activeEditorGridTicks (main) == 240);  // ...leaves the open editor alone

    main.setActiveEditorGridSize (GridSize::Sixteenth);   // the menu overrides for this window
    CHECK (Access::activeEditorGridTicks (main) == 120);
}

TEST_CASE ("SongsmithMainComponent: the default grid is Off until set", "[track-editor][view-settings]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCCu, 0, 0);
    SongsmithMainComponent main (doc);
    Access::trackDoubleClicked (main, (juce::int64) track.getProperty (SongIDs::trackId));
    REQUIRE (main.isTrackEditorOpen());
    CHECK (Access::activeEditorGridTicks (main) == 0);
}

TEST_CASE ("SongsmithMainComponent: follow reaches the track list, the open editor and an editor opened later", "[track-editor][view-settings]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCCu, 0, 0);
    const auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);
    SongsmithMainComponent main (doc);

    main.setFollowPlayhead (false);                       // before any editor exists
    CHECK_FALSE (Access::trackList (main).getFollowPlayhead());
    Access::trackDoubleClicked (main, trackId);
    REQUIRE (main.isTrackEditorOpen());
    CHECK_FALSE (Access::activeEditorFollows (main));     // the later editor got it

    main.setFollowPlayhead (true);                        // live, to the open editor
    CHECK (Access::activeEditorFollows (main));
    CHECK (Access::trackList (main).getFollowPlayhead());
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

TEST_CASE ("SongsmithMainComponent: the transport strip heads the lower region, between the MIDI canvas and the part strip, and moves with the splitter", "[songsmith][transport]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    SongsmithMainComponent view (doc, &controller);
    view.setSize (1000, 800);

    using Access = SongsmithMainComponentTestAccess;
    auto* strip = Access::transportStrip (view);
    REQUIRE (strip != nullptr);

    auto stripTopInView = [&] { return view.getLocalArea (strip, strip->getLocalBounds()).getY(); };
    auto partTopInView  = [&] { return view.getLocalArea (&Access::partStrip (view), Access::partStrip (view).getLocalBounds()).getY(); };

    CHECK (stripTopInView() >= Access::upperRegion (view).getBottom());
    CHECK (strip->getHeight() == TransportStrip::height);
    CHECK (partTopInView() == stripTopInView() + TransportStrip::height);

    const int before = stripTopInView();
    Access::splitter (view).setFraction (0.3f);
    view.resized();
    CHECK (stripTopInView() < before);
    CHECK (partTopInView() == stripTopInView() + TransportStrip::height);
}

TEST_CASE ("SongsmithMainComponent: without a playback controller there is no transport strip", "[songsmith][transport]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    SongsmithMainComponent view (doc);
    view.setSize (1000, 800);
    CHECK (SongsmithMainComponentTestAccess::transportStrip (view) == nullptr);
}

TEST_CASE ("SongsmithMainComponent: the section keys forward to the track list and do nothing when there is nothing to act on", "[track-list][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto a = playbacktest::addTrack (doc, "A");
    auto b = playbacktest::addTrack (doc, "B");
    playbacktest::addNote (a, 60, 0, 960);
    playbacktest::addNote (b, 62, 0, 960);
    SongsmithMainComponent main (doc);
    main.setBounds (0, 0, 1000, 700);

    main.selectAllHeadsForTesting();
    CHECK (main.getSelectedTrackIdsForTesting().size() == 2);
    main.clearSelectionForTesting();
    main.selectAll();   // headless: the pointer is over nothing, so this is the heads variant
    CHECK (main.getSelectedTrackIdsForTesting().size() == 2);
    main.selectAllCanvasesForTesting();
    CHECK (main.getSelectedTrackIdsForTesting().size() == 2);
    main.clearSelectionForTesting();

    CHECK_FALSE (main.deleteSections());   // no section selected
    CHECK_FALSE (main.splitSections());    // pointer outside the window, no marker
    CHECK_FALSE (doc.canUndo());
    CHECK (a.getChildWithName (SongIDs::SECTIONS).getNumChildren() == 0);
    CHECK (b.getChildWithName (SongIDs::SECTIONS).getNumChildren() == 0);
}

TEST_CASE ("SongsmithMainComponent: the merge scope reaches the track list", "[track-editor][view-settings]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    SongsmithMainComponent main (doc);

    CHECK (Access::trackList (main).getMergeScope() == MergeScope::notesOnly);
    main.setMergeScope (MergeScope::allEvents);
    CHECK (Access::trackList (main).getMergeScope() == MergeScope::allEvents);
}

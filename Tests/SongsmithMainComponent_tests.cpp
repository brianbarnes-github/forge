// Reproduces the stale-preview bug: removing the currently-previewed PART
// node does not fire valueTreeChildRemoved on that node (ValueTree only
// notifies the removed child's PARENT), only valueTreeParentChanged on the
// removed node itself. See juce-valuetree-conventions and the fix in
// SongsmithMainComponent::valueTreeParentChanged.

#include "MidiTestBytes.h"
#include "PartStripTestAccess.h"
#include "UI/SongDocument.h"
#include "PlaybackTestSupport.h"
#include "UI/Playback/PlaybackController.h"
#include "UI/Playback/TransportStrip.h"
#include "UI/SectionEdit.h"
#include "UI/SongModelBridge.h"
#include "UI/Dock/DockFloatWindow.h"
#include "UI/Dock/DockPanel.h"
#include "UI/SongsmithMainComponent.h"
#include "UI/TempoEdit.h"
#include "UI/TempoMeterEditorWindow.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

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
        static const void* tempoMeterWindow (const SongsmithMainComponent& c) { return c.tempoMeterWindow.get(); }
        static std::optional<int> eventMarkerTick (SongsmithMainComponent& c) { return c.trackList.getEventMarkerTick(); }
        static void selectInTempoMeterWindow (SongsmithMainComponent& c, std::optional<int> tick) { c.tempoMeterWindow->onSelectedTickChanged (tick); }
        static std::optional<TempoMeterEvent> tempoMeterSelection (SongsmithMainComponent& c)
        {
            return c.tempoMeterWindow != nullptr ? c.tempoMeterWindow->selectedEvent() : std::nullopt;
        }
        static void closeTempoMeterWindow (SongsmithMainComponent& c) { c.tempoMeterWindow->closeButtonPressed(); }
        static int tempoGraphRepaints (SongsmithMainComponent& c) { return c.tempoMeterWindow->graphRepaintRequestsForTesting(); }
        static juce::int64 editorTrackId (const SongsmithMainComponent& c)
        {
            return c.trackEditor->getTrackId();
        }
        static juce::String editorTitle (const SongsmithMainComponent& c)
        {
            return c.trackEditor->title();
        }
        static int trackEditorAppearanceRepaints (const SongsmithMainComponent& c)
        {
            return c.trackEditor->appearanceRepaintsForTesting();
        }
        static bool previewUpdatePending (const SongsmithMainComponent& c) { return c.isUpdatePending(); }
        static void trackDoubleClicked (SongsmithMainComponent& c, juce::int64 trackId)
        {
            c.trackDoubleClicked (trackId);
        }
        static PartStripComponent& partStrip (SongsmithMainComponent& c) { return c.partStrip; }
        static lotro::dock::DockPanel* editorPanel (SongsmithMainComponent& c) { return c.dockHost.panel ("midiEditor"); }
        static TransportStrip* transportStrip (SongsmithMainComponent& c) { return c.transportStrip.get(); }
        static juce::Component& upperRegion (SongsmithMainComponent& c) { return c.upperRegion; }
        static juce::Component& lowerRegion (SongsmithMainComponent& c) { return c.lowerRegion; }
        // Stands in for dragging the old splitter: gives the upper region `fraction` of the height.
        static void setUpperFraction (SongsmithMainComponent& c, double fraction)
        {
            auto layout = c.dockHost.state();
            layout.upperWeight = fraction;
            layout.lowerWeight = 1.0 - fraction;
            c.dockHost.apply (layout);
        }
        static std::size_t ghostedCount (const SongsmithMainComponent& c) { return c.ghostedTrackIds.size(); }
        static juce::int64 selectedPreviewPartId (const SongsmithMainComponent& c) { return c.selectedPreviewPartId; }
        static void trackGhostToggled (SongsmithMainComponent& c, juce::int64 id, bool v) { c.trackGhostToggled (id, v); }
        static TrackListComponent& trackList (SongsmithMainComponent& c)
        {
            return c.trackList;
        }
        static PianoRollComponent& previewRoll (SongsmithMainComponent& c) { return c.previewRoll; }
        static lotro::dock::DockPanel* mixerDockPanel (SongsmithMainComponent& c) { return c.dockHost.panel ("mixer"); }
        static std::vector<lotro::dock::DockStack::Item> bottomStackItems (SongsmithMainComponent& c)
        {
            return c.bottomStack.items();
        }
        static bool activeEditorFollows (const SongsmithMainComponent& c)
        {
            return c.trackEditor->getFollowPlayhead();
        }
        static int activeEditorGridTicks (const SongsmithMainComponent& c)
        {
            return c.trackEditor->getGridTicks();
        }
        // A press/release on the editor roll's canvas (canvas pixels), through the same
        // handler the real Canvas::mouseDown/mouseUp call (minus their grabKeyboardFocus).
        static void editorMouseDown (SongsmithMainComponent& c, juce::Point<int> pos)
        {
            c.trackEditor->roll.handleEditorMouseDown (pos, {}, false);
        }
        static void editorMouseUp (SongsmithMainComponent& c, juce::Point<int> pos)
        {
            c.trackEditor->roll.handleEditorMouseUp (pos);
        }
        static void sectionPress (SongsmithMainComponent& c, juce::int64 trackId, SectionHit hit, int tick)
        {
            c.trackList.sectionPressed (trackId, hit, tick, {});
        }
        static void sectionDrag (SongsmithMainComponent& c, int tick) { c.trackList.sectionDragged (tick); }
        static bool sectionDragShown (const SongsmithMainComponent& c) { return c.trackList.sectionView.drag.has_value(); }
        // previewRegion is a child of lowerRegion, so its bounds are already
        // relative to lowerRegion's own local space -- compare against
        // lowerRegion's height (not lowerRegion.getBottom(), which is in
        // lowerRegion's PARENT's coordinate space, a different frame).
        static int lowerRegionHeight (const SongsmithMainComponent& c)
        {
            return c.lowerRegion.getHeight();
        }
        // In lowerRegion's own space (previewRegion now sits inside a dock panel in the bottom stack).
        static int previewRegionBottom (const SongsmithMainComponent& c)
        {
            return c.lowerRegion.getLocalArea (&c.previewRegion, c.previewRegion.getLocalBounds()).getBottom();
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
    CHECK (Access::editorTrackId (main) == trackId);
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
    REQUIRE (Access::editorTitle (main) == "Edit Track: Track A");
    const int stripBefore = PartStripComponentTestAccess::appearanceRepaints (Access::partStrip (main));
    const int editorBefore = Access::trackEditorAppearanceRepaints (main);

    doc.setProperty (track, SongIDs::playbackVolume, 40);
    CHECK (PartStripComponentTestAccess::appearanceRepaints (Access::partStrip (main)) == stripBefore);
    CHECK (Access::trackEditorAppearanceRepaints (main) == editorBefore);

    doc.setProperty (track, SongIDs::colorArgb, (int) 0xFF112233u);
    CHECK (PartStripComponentTestAccess::appearanceRepaints (Access::partStrip (main)) == stripBefore + 1);
    CHECK (Access::trackEditorAppearanceRepaints (main) == editorBefore + 1);

    doc.setProperty (track, SongIDs::name, "Harp");
    CHECK (Access::editorTitle (main) == "Edit Track: Harp");
    doc.getUndoManager().undo();
    CHECK (Access::editorTitle (main) == "Edit Track: Track A");
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

TEST_CASE ("SongsmithMainComponent: a pan change on a previewed part's track does not recompute the preview", "[track-editor][pan]")
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

    doc.setProperty (track, SongIDs::playbackPan, 40);
    CHECK_FALSE (Access::previewUpdatePending (main));
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
    CHECK (Access::editorTrackId (main) == idA);

    Access::trackDoubleClicked (main, idB);
    CHECK (Access::editorTrackId (main) == idB);
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

TEST_CASE ("SongsmithMainComponent: documentReplaced clears the editor's track and UI state outside the tree", "[session-reset]")
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

    CHECK (main.isTrackEditorOpen());   // the panel keeps its mode and visibility...
    CHECK (Access::editorTrackId (main) == -1);   // ...but holds no stale track handle
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
    CHECK (partTopInView() == stripTopInView() + TransportStrip::height + lotro::dock::DockPanel::headerHeight);

    const int before = stripTopInView();
    Access::setUpperFraction (view, 0.3);
    view.resized();
    CHECK (stripTopInView() < before);
    CHECK (partTopInView() == stripTopInView() + TransportStrip::height + lotro::dock::DockPanel::headerHeight);
}

TEST_CASE ("SongsmithMainComponent: hiding the MIDI source region lifts the transport strip to the top and showing it restores it", "[songsmith][transport][source]")
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
    auto stripTop = [&] { return view.getLocalArea (strip, strip->getLocalBounds()).getY(); };

    CHECK (view.isSourceVisible());
    const int before = stripTop();
    CHECK (before > 100);

    view.setSourceVisible (false);
    CHECK_FALSE (view.isSourceVisible());
    CHECK_FALSE (Access::upperRegion (view).isVisible());
    CHECK (stripTop() == 0);

    view.setSourceVisible (true);
    CHECK (stripTop() == before);
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

TEST_CASE ("SongsmithMainComponent: openTempoMeterEditor opens one window, re-fronts it, and documentReplaced closes it", "[tempo-meter][session-reset]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    SongsmithMainComponent main (doc);

    CHECK_FALSE (main.isTempoMeterEditorOpen());
    main.openTempoMeterEditor();
    REQUIRE (main.isTempoMeterEditorOpen());
    const auto* first = Access::tempoMeterWindow (main);
    main.openTempoMeterEditor();
    CHECK (Access::tempoMeterWindow (main) == first);

    main.documentReplaced();
    CHECK_FALSE (main.isTempoMeterEditorOpen());
}

TEST_CASE ("SongsmithMainComponent: closing the Tempo/Meter window lets the next open create a fresh one", "[tempo-meter]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    SongsmithMainComponent main (doc);

    main.openTempoMeterEditor();
    REQUIRE (main.isTempoMeterEditorOpen());
    Access::closeTempoMeterWindow (main);
    CHECK_FALSE (main.isTempoMeterEditorOpen());
    main.openTempoMeterEditor();
    CHECK (main.isTempoMeterEditorOpen());
}

TEST_CASE ("SongsmithMainComponent: the Tempo/Meter window's selected tick drives the track list's event marker", "[tempo-meter][event-marker]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    SongsmithMainComponent main (doc);
    main.openTempoMeterEditor();
    REQUIRE (main.isTempoMeterEditorOpen());
    CHECK (Access::eventMarkerTick (main) == std::nullopt);

    Access::selectInTempoMeterWindow (main, 1920);
    CHECK (Access::eventMarkerTick (main) == std::optional<int> (1920));
    Access::selectInTempoMeterWindow (main, std::nullopt);
    CHECK (Access::eventMarkerTick (main) == std::nullopt);

    Access::selectInTempoMeterWindow (main, 960);
    Access::closeTempoMeterWindow (main);   // the user closes the window
    CHECK (Access::eventMarkerTick (main) == std::nullopt);

    main.openTempoMeterEditor();
    Access::selectInTempoMeterWindow (main, 480);
    REQUIRE (Access::eventMarkerTick (main) == std::optional<int> (480));
    main.documentReplaced();
    CHECK (Access::eventMarkerTick (main) == std::nullopt);
}

TEST_CASE ("SongsmithMainComponent: a marker single-click does nothing while the Tempo/Meter editor is closed and moves the selection of an open one; a marker double-click opens it on that event", "[tempo-meter][marker-click]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    {
        miditest::TrackBody c;
        c.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 }).ev (0, { 0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08 }).eot (3840);
        miditest::TrackBody n;
        n.ev (0, { 0x90, 60, 100 }).ev (480, { 0x80, 60, 0x40 }).eot();
        const auto bytes = miditest::smf (1, 480, { c, n });
        auto file = juce::File::createTempFile (".mid");
        REQUIRE (file.replaceWithData (bytes.data(), bytes.size()));
        Diagnostics d;
        REQUIRE (importMidiFile (doc, file, 1, d));
        file.deleteFile();
    }
    REQUIRE_FALSE (setTempo (doc, 1920, 100));
    REQUIRE_FALSE (setMeter (doc, 3840, 3, 4));
    SongsmithMainComponent main (doc);

    REQUIRE_FALSE (main.isTempoMeterEditorOpen());
    CHECK_FALSE (main.selectTempoMeterEvent (TempoMeterKind::meter, 3840));   // closed: not handled
    CHECK_FALSE (main.isTempoMeterEditorOpen());
    CHECK (Access::eventMarkerTick (main) == std::nullopt);

    main.openTempoMeterEditorOnEvent (TempoMeterKind::meter, 3840);          // the double-click path
    REQUIRE (main.isTempoMeterEditorOpen());
    const auto first = Access::tempoMeterSelection (main);
    REQUIRE (first);
    CHECK (first->kind == TempoMeterKind::meter);
    CHECK (first->tick == 3840);
    CHECK (Access::eventMarkerTick (main) == std::optional<int> (3840));

    const auto* window = Access::tempoMeterWindow (main);
    CHECK (main.selectTempoMeterEvent (TempoMeterKind::tempo, 1920));         // open: handled
    CHECK (Access::tempoMeterWindow (main) == window);   // same window, selection moved
    const auto second = Access::tempoMeterSelection (main);
    REQUIRE (second);
    CHECK (second->kind == TempoMeterKind::tempo);
    CHECK (second->tick == 1920);
    CHECK (Access::eventMarkerTick (main) == std::optional<int> (1920));

    // Double-click on a marker while open moves the selection; a plain open leaves it.
    main.openTempoMeterEditorOnEvent (TempoMeterKind::meter, 3840);
    CHECK (Access::tempoMeterWindow (main) == window);
    CHECK (Access::tempoMeterSelection (main)->kind == TempoMeterKind::meter);
    main.openTempoMeterEditor();
    CHECK (Access::tempoMeterSelection (main)->tick == 3840);
}

TEST_CASE ("SongsmithMainComponent: zooming the track list repaints the open Tempo/Meter graph, and zooming after the window closes is safe", "[tempo-meter][graph]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    SongsmithMainComponent main (doc);
    main.setBounds (0, 0, 1000, 700);
    main.openTempoMeterEditor();
    REQUIRE (main.isTempoMeterEditorOpen());

    auto& list = Access::trackList (main);
    const double ppt = list.getTimelineView().getPixelsPerTick();
    const int before = Access::tempoGraphRepaints (main);

    // Ctrl+wheel resizes rows; the plain wheel over the note preview zooms.
    juce::MouseWheelDetails wheel {};
    wheel.deltaY = 1.0f;
    const auto pos = juce::Point<float> (list.getWidth() - 20.0f, 10.0f);
    list.mouseWheelMove (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, {},
                                           0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &list, &list,
                                           juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false),
                         wheel);
    CHECK (list.getTimelineView().getPixelsPerTick() != ppt);
    CHECK (Access::tempoGraphRepaints (main) > before);

    Access::closeTempoMeterWindow (main);
    REQUIRE_FALSE (main.isTempoMeterEditorOpen());
    list.mouseWheelMove (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, {},
                                           0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &list, &list,
                                           juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false),
                         wheel);
    main.fitTrackTimelineToDocument();
}

TEST_CASE ("SongsmithMainComponent: the first-run layout is the old splitter layout", "[songsmith][layout]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    lotro::playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    SongsmithMainComponent view (doc, &controller);
    view.setSize (1000, 700);

    using Access = SongsmithMainComponentTestAccess;
    auto& lower = Access::lowerRegion (view);
    auto& upper = Access::upperRegion (view);
    const auto roll = lower.getLocalArea (&Access::previewRoll (view), Access::previewRoll (view).getLocalBounds());
    const auto strip = view.getLocalArea (Access::transportStrip (view), Access::transportStrip (view)->getLocalBounds());
    const auto part = view.getLocalArea (&Access::partStrip (view), Access::partStrip (view).getLocalBounds());
    // Recorded from the unmodified splitter layout (1000x700): upper 0,0,1000,385; lower 0,391,1000,309;
    // roll in lower 160,158,840,151; transport 0,391,1000,28; part strip 0,419,1000,110. The part strip is
    // now the first docked panel of the bottom zone (under the transport, above the preview), so it sits
    // below its panel header and shares the zone's height by weight instead of being a fixed 110 px.
    // The stack's divider is laid out inside the height, so the split can differ by a few pixels.
    CHECK (std::abs (upper.getHeight() - 385) <= 4);
    CHECK (upper.getY() == 0);
    CHECK (lower.getY() == upper.getBottom() + 6);
    CHECK (lower.getBottom() == 700);
    CHECK (strip.getY() == lower.getY());
    CHECK (strip.getHeight() == 28);
    CHECK (part.getY() == strip.getBottom() + lotro::dock::DockPanel::headerHeight);
    CHECK (part.getHeight() > 60);
    CHECK (roll.getX() == 160);
    const auto partInLower = lower.getLocalArea (&Access::partStrip (view), Access::partStrip (view).getLocalBounds());
    CHECK (roll.getY() > partInLower.getBottom() + lotro::dock::DockPanel::headerHeight);
    CHECK (roll.getWidth() == 840);
    CHECK (roll.getBottom() == lower.getHeight());
}

TEST_CASE ("SongsmithMainComponent: the editor panel opens floating, docks between the tracks and the lower region, and keeps its track", "[songsmith][layout][track-editor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCCu, 0, 0);
    const auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);
    SongsmithMainComponent view (doc);
    view.setSize (1000, 700);

    CHECK_FALSE (view.isTrackEditorOpen());
    Access::trackDoubleClicked (view, trackId);
    CHECK (view.isTrackEditorOpen());
    CHECK (Access::editorTrackId (view) == trackId);
    CHECK (Access::editorTitle (view) == "Edit Track: Track A");
    auto* panel = Access::editorPanel (view);
    REQUIRE (panel != nullptr);
    CHECK (dynamic_cast<lotro::dock::DockFloatWindow*> (panel->getTopLevelComponent()) != nullptr);

    panel->onFloatToggle();   // dock it
    CHECK (panel->getParentComponent() != nullptr);
    CHECK (panel->getBounds().getY() >= Access::upperRegion (view).getBottom());
    CHECK (panel->getBottom() <= Access::lowerRegion (view).getY());
    CHECK (Access::editorTrackId (view) == trackId);

    view.documentReplaced();
    CHECK (view.isTrackEditorOpen());
    CHECK (Access::editorTrackId (view) == -1);
}

namespace
{
    bool sameLayout (const lotro::dock::LayoutState& a, const lotro::dock::LayoutState& b)
    {
        if (a.upperWeight != b.upperWeight || a.lowerWeight != b.lowerWeight || a.panels.size() != b.panels.size())
            return false;
        for (const auto& p : a.panels)
        {
            const auto* q = lotro::dock::find (b, p.id);
            if (q == nullptr || q->mode != p.mode || q->visible != p.visible || q->zone != p.zone
                || q->order != p.order || q->floatBounds.has_value() != p.floatBounds.has_value())
                return false;
        }
        return true;
    }
}

TEST_CASE ("SongsmithMainComponent: a saved dock layout restores exactly through JSON", "[songsmith][layout][dock]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    SongsmithMainComponent first (doc);
    first.setSize (1000, 700);

    auto layout = first.getDockLayout();
    lotro::dock::show (layout, "midiEditor");
    lotro::dock::setDocked (layout, "midiEditor");
    lotro::dock::hide (layout, "lotroPreview");
    first.setDockLayout (layout);
    const auto saved = lotro::dock::toJson (first.getDockLayout());

    SongDocument doc2;
    SongsmithMainComponent second (doc2);
    second.setSize (1000, 700);
    second.setDockLayout (lotro::dock::fromJson (saved));

    CHECK (second.isMidiEditorVisible());
    CHECK_FALSE (second.isLotroPreviewVisible());
    CHECK (sameLayout (second.getDockLayout(), first.getDockLayout()));
    CHECK (lotro::dock::find (second.getDockLayout(), "midiEditor")->mode == lotro::dock::Mode::docked);
}

TEST_CASE ("SongsmithMainComponent: an empty or corrupt stored layout gives the first-run page", "[songsmith][layout][dock]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    for (const char* stored : { "", "{{{", "[]" })
    {
        SongDocument doc;
        SongsmithMainComponent view (doc);
        view.setSize (1000, 700);
        view.setDockLayout (lotro::dock::fromJson (stored));
        CHECK (view.isLotroPreviewVisible());
        CHECK_FALSE (view.isMidiEditorVisible());
        CHECK (lotro::dock::find (view.getDockLayout(), "lotroPreview")->mode == lotro::dock::Mode::docked);
    }
}

TEST_CASE ("SongsmithMainComponent: toggling a panel flips its visibility and reports one layout change; applying reports none", "[songsmith][layout][dock]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    SongsmithMainComponent view (doc);
    view.setSize (1000, 700);
    int changes = 0;
    view.onDockLayoutChanged = [&] (const lotro::dock::LayoutState&) { ++changes; };

    view.setLotroPreviewVisible (false);
    CHECK_FALSE (view.isLotroPreviewVisible());
    CHECK (changes == 1);
    view.setLotroPreviewVisible (true);
    CHECK (view.isLotroPreviewVisible());
    CHECK (changes == 2);

    view.setMidiEditorVisible (true);
    CHECK (view.isMidiEditorVisible());
    CHECK (changes == 3);
    view.setMidiEditorVisible (false);
    CHECK_FALSE (view.isMidiEditorVisible());
    CHECK (changes == 4);

    view.setDockLayout (lotro::dock::defaultLayout());
    CHECK (changes == 4);
}

TEST_CASE ("SongsmithMainComponent: a floating panel's bounds are reported when its window moves, and an off-screen rectangle is brought back", "[songsmith][layout][dock]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    SongsmithMainComponent view (doc);
    view.setSize (1000, 700);

    auto layout = view.getDockLayout();
    lotro::dock::setFloating (layout, "midiEditor", lotro::dock::FloatBounds { -5000, -5000, 800, 600 });
    lotro::dock::show (layout, "midiEditor");
    view.setDockLayout (layout);

    auto* panel = SongsmithMainComponentTestAccess::editorPanel (view);
    REQUIRE (panel != nullptr);
    auto* window = dynamic_cast<lotro::dock::DockFloatWindow*> (panel->getTopLevelComponent());
    REQUIRE (window != nullptr);
    const auto primary = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
    CHECK (window->getBounds().intersects (primary));

    std::optional<lotro::dock::LayoutState> last;
    int reports = 0;
    view.onDockLayoutChanged = [&] (const lotro::dock::LayoutState& s) { last = s; ++reports; };
    window->setBounds (primary.getX() + 40, primary.getY() + 50, 500, 300);
    window->setBounds (primary.getX() + 45, primary.getY() + 55, 510, 310);
    window->setBounds (primary.getX() + 50, primary.getY() + 60, 520, 320);
    // A drag is a burst of moves: they settle into one report, not one per event.
    juce::MessageManager::getInstance()->runDispatchLoopUntil (600);
    CHECK (reports == 1);
    REQUIRE (last.has_value());
    const auto* saved = lotro::dock::find (*last, "midiEditor");
    REQUIRE (saved != nullptr);
    REQUIRE (saved->floatBounds.has_value());
    CHECK (saved->floatBounds->width == window->getWidth());
    CHECK (saved->floatBounds->height == window->getHeight());
}

TEST_CASE ("SongsmithMainComponent: after the Song is replaced, reopening a track starts on the default grid again", "[songsmith][layout][track-editor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCCu, 0, 0);
    const auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);
    SongsmithMainComponent view (doc);
    view.setSize (1000, 700);
    view.setDefaultGridSize (GridSize::Quarter);

    Access::trackDoubleClicked (view, trackId);
    const int ticksPerQuarter = (int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter, 480);
    CHECK (Access::activeEditorGridTicks (view) == gridSizeToTicks (GridSize::Quarter, ticksPerQuarter));

    view.setActiveEditorGridSize (GridSize::Sixteenth);   // the user changed it
    view.documentReplaced();                              // panel stays visible, nothing loaded
    Access::trackDoubleClicked (view, trackId);
    CHECK (Access::activeEditorGridTicks (view) == gridSizeToTicks (GridSize::Quarter, ticksPerQuarter));
}

TEST_CASE ("SongsmithMainComponent: with the preview docked elsewhere, showing diagnostics fills the lower region with no stray gap", "[songsmith][layout][dock]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    SongsmithMainComponent view (doc);
    view.setSize (1000, 700);

    view.setLotroPreviewVisible (false);
    view.setPartsVisible (false);          // bottom zone empty
    view.setDiagnosticsVisible (true);

    auto& diag = view.getDiagnostics();
    const auto d = Access::lowerRegion (view).getLocalArea (&diag, diag.getLocalBounds());
    const int chrome = 0;   // this view has no playback controller, so no transport strip
    CHECK (d.getY() == chrome);
    CHECK (d.getBottom() == Access::lowerRegionHeight (view));
}

namespace
{
std::vector<std::string> bottomIds (SongsmithMainComponent& view)
{
    std::vector<std::string> out;
    for (const auto& it : SongsmithMainComponentTestAccess::bottomStackItems (view))
        out.push_back (it.id);
    return out;
}
} // namespace

TEST_CASE ("SongsmithMainComponent: the Mixer is a hidden dock panel that the toggle shows last in the bottom zone", "[songsmith][layout][dock][mixer]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    SongsmithMainComponent view (doc, &controller);
    view.setSize (1000, 800);

    REQUIRE (Access::mixerDockPanel (view) != nullptr);
    CHECK_FALSE (view.isMixerVisible());
    CHECK (bottomIds (view) == std::vector<std::string> { "parts", "lotroPreview" });

    int changes = 0;
    view.onDockLayoutChanged = [&] (const lotro::dock::LayoutState&) { ++changes; };
    view.setMixerVisible (true);
    CHECK (view.isMixerVisible());
    CHECK (changes == 1);
    CHECK (bottomIds (view) == std::vector<std::string> { "parts", "lotroPreview", "mixer" });
    CHECK (Access::mixerDockPanel (view)->isVisible());
    CHECK (Access::mixerDockPanel (view)->getHeight() > 0);
    CHECK (lotro::dock::find (view.getDockLayout(), "mixer")->visible);

    view.setMixerVisible (false);
    CHECK_FALSE (view.isMixerVisible());
    CHECK (changes == 2);
    CHECK (bottomIds (view) == std::vector<std::string> { "parts", "lotroPreview" });
}

TEST_CASE ("SongsmithMainComponent: the docked Mixer keeps its 150 px minimum when the bottom zone is cramped", "[songsmith][layout][dock][mixer]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    SongsmithMainComponent view (doc, &controller);
    view.setSize (1000, 800);
    view.setMixerVisible (true);

    const auto items = Access::bottomStackItems (view);
    const auto it = std::find_if (items.begin(), items.end(), [] (const auto& i) { return i.id == "mixer"; });
    REQUIRE (it != items.end());
    CHECK (it->minSize == 150);

    auto layout = view.getDockLayout();   // starve the mixer of weight: its minimum still holds
    lotro::dock::find (layout, "mixer")->weight = 0.001;
    view.setDockLayout (layout);
    CHECK (Access::mixerDockPanel (view)->getHeight() >= 150);
}

TEST_CASE ("SongsmithMainComponent: without a playback controller there is no Mixer, even if the layout shows it", "[songsmith][layout][dock][mixer]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    SongsmithMainComponent view (doc);
    view.setSize (1000, 700);

    CHECK (Access::mixerDockPanel (view) == nullptr);
    CHECK_FALSE (view.isMixerVisible());

    int changes = 0;
    view.onDockLayoutChanged = [&] (const lotro::dock::LayoutState&) { ++changes; };
    view.setMixerVisible (true);
    CHECK_FALSE (view.isMixerVisible());
    CHECK (changes == 0);

    auto layout = view.getDockLayout();
    lotro::dock::show (layout, "mixer");
    view.setDockLayout (layout);
    CHECK_FALSE (view.isMixerVisible());
    CHECK (bottomIds (view) == std::vector<std::string> { "parts", "lotroPreview" });
}

TEST_CASE ("SongsmithMainComponent: the Parts strip is a dock panel that can be hidden, floated and restored", "[songsmith][layout][dock]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    SongsmithMainComponent view (doc);
    view.setSize (1000, 700);

    CHECK (view.isPartsVisible());
    const auto wide = Access::partStrip (view).getWidth();
    CHECK (wide > 0);

    view.setPartsVisible (false);
    CHECK_FALSE (view.isPartsVisible());
    CHECK (lotro::dock::find (view.getDockLayout(), "parts")->visible == false);

    view.setPartsVisible (true);
    CHECK (view.isPartsVisible());
    CHECK (Access::partStrip (view).getWidth() == wide);

    auto layout = view.getDockLayout();
    lotro::dock::setFloating (layout, "parts", lotro::dock::FloatBounds { 40, 40, 500, 200 });
    view.setDockLayout (layout);
    CHECK (view.isPartsVisible());
    CHECK (lotro::dock::find (view.getDockLayout(), "parts")->mode == lotro::dock::Mode::floating);
}

TEST_CASE ("SongsmithMainComponent: loop edges snap to the loaded editor's grid, else to the default grid at the document's PPQ", "[songsmith][loop][track-editor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCCu, 0, 0);
    const auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);
    doc.getSourceMidiNode().setProperty (SongIDs::ticksPerQuarter, 120, nullptr);   // not 480: a hardcoded PPQ fails
    SongsmithMainComponent view (doc);
    view.setSize (1000, 700);
    auto& list = Access::trackList (view);
    REQUIRE (list.loopSnapTicks != nullptr);

    // No editor loaded: the default grid.
    view.setDefaultGridSize (GridSize::Quarter);
    CHECK (view.currentLoopSnapTicks() == 120);
    CHECK (list.loopSnapTicks() == 120);

    // A loaded editor whose grid was changed from the default: its grid.
    Access::trackDoubleClicked (view, trackId);
    REQUIRE (view.isTrackEditorLoaded());
    view.setActiveEditorGridSize (GridSize::Sixteenth);
    CHECK (view.currentLoopSnapTicks() == 30);
    CHECK (list.loopSnapTicks() == 30);

    // Hidden again, the editor no longer decides: the (changed) default does.
    view.setDefaultGridSize (GridSize::Eighth);
    view.setMidiEditorVisible (false);
    REQUIRE_FALSE (view.isTrackEditorLoaded());
    CHECK (view.currentLoopSnapTicks() == 60);
    CHECK (list.loopSnapTicks() == 60);
}

TEST_CASE ("SongsmithMainComponent: Escape clears the loop range, and does nothing when there is none", "[songsmith][loop]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCCu, 0, 0);
    playbacktest::addNote (track, 60, 0, 9600);
    playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    controller.flushRebuild();
    SongsmithMainComponent view (doc, &controller);
    view.setSize (1000, 700);

    CHECK_FALSE (view.escapePressed());   // nothing to clear: the key is not consumed

    REQUIRE (controller.setLoopRange (1200.0, 2400.0));
    CHECK (view.escapePressed());
    CHECK_FALSE (controller.getLoopRange().has_value());
    CHECK_FALSE (view.escapePressed());
}

TEST_CASE ("SongsmithMainComponent: Escape during a note-editor drag leaves the loop range; after the release it clears it", "[songsmith][loop][track-editor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCCu, 0, 0);
    playbacktest::addNote (track, 60, 0, 9600);
    playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    controller.flushRebuild();
    SongsmithMainComponent view (doc, &controller);
    view.setSize (1000, 700);
    Access::trackDoubleClicked (view, (juce::int64) track.getProperty (SongIDs::trackId));
    REQUIRE (view.isTrackEditorLoaded());
    REQUIRE (controller.setLoopRange (1200.0, 2400.0));

    // A press on empty canvas (the pitch-127 row, far above the note) starts a rubber band.
    const juce::Point<int> empty { 300, 2 };
    Access::editorMouseDown (view, empty);
    CHECK_FALSE (view.escapePressed());
    CHECK (controller.getLoopRange().has_value());

    Access::editorMouseUp (view, empty);
    CHECK (view.escapePressed());   // a loaded editor alone does not block it
    CHECK_FALSE (controller.getLoopRange().has_value());
}

TEST_CASE ("SongsmithMainComponent: Escape during a section drag cancels only the drag; the next one clears the loop range", "[songsmith][loop][sections]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = playbacktest::addTrack (doc);
    playbacktest::addNote (track, 60, 0, 9600);
    const auto id = (juce::int64) track.getProperty (SongIDs::trackId);
    splitAt (doc, { id }, 4800);
    playbacktest::RecordingSink sink;
    PlaybackController controller (doc, sink);
    controller.flushRebuild();
    SongsmithMainComponent view (doc, &controller);
    view.setSize (1000, 700);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);   // let the track list build its rows
    REQUIRE (controller.setLoopRange (1200.0, 2400.0));

    const auto second = sectionsOf (track)[1];
    Access::sectionPress (view, id, { second.id, SectionZone::Body }, 6000);
    Access::sectionDrag (view, 6500);
    REQUIRE (Access::sectionDragShown (view));

    CHECK (view.escapePressed());
    CHECK_FALSE (Access::sectionDragShown (view));
    CHECK (controller.getLoopRange().has_value());

    CHECK (view.escapePressed());
    CHECK_FALSE (controller.getLoopRange().has_value());
}

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

// Verifies TrackListComponent::rebuild() clears a stale selection (and fires
// onTrackSelected with the cleared sentinel) once the previously-selected
// track disappears from SOURCE_MIDI — e.g. after SongDocument::removeTrack.

#include "UI/SongDocument.h"
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

TEST_CASE ("TrackListComponent: rebuild() leaves a live selection untouched", "[piano-roll]")
{
    SongDocument doc;
    auto track1 = doc.addTrack ("Track 1", (int) 0xFF7FA8D0, 0, 0);
    const auto track1Id = (juce::int64) track1.getProperty (SongIDs::trackId);

    TrackListComponent list (doc);

    int callbackCount = 0;
    list.onTrackSelected = [&] (juce::int64) { ++callbackCount; };

    Access::selectTrack (list, track1Id);
    CHECK (callbackCount == 1);

    Access::rebuild (list);

    // The selected track is still present, so rebuild() must not re-fire
    // onTrackSelected or clear the selection.
    CHECK (callbackCount == 1);
}

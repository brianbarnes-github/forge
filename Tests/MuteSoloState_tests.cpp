#include "PlaybackTestSupport.h"
#include "UI/Playback/MuteSoloState.h"
#include "UI/Playback/PlaybackSnapshot.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;
using namespace lotro::playbacktest;

TEST_CASE ("MuteSoloState: nothing muted or soloed means everything is audible", "[playback][mutesolo]")
{
    MuteSoloState s;
    CHECK (s.isAudible (5));
    CHECK (! s.anySolo());
}

TEST_CASE ("MuteSoloState: mute silences a track", "[playback][mutesolo]")
{
    MuteSoloState s;
    s.setMuted (1, true);
    CHECK (! s.isAudible (1));
    CHECK (s.isAudible (2));
}

TEST_CASE ("MuteSoloState: solo is additive and silences everything else", "[playback][mutesolo]")
{
    MuteSoloState s;
    s.setSoloed (1, true);
    s.setSoloed (2, true);
    CHECK (s.isAudible (1));
    CHECK (s.isAudible (2));
    CHECK (! s.isAudible (3));
    CHECK (s.isSilencedBySolo (3));
    CHECK (! s.isSilencedBySolo (1));
}

TEST_CASE ("MuteSoloState: mute beats solo", "[playback][mutesolo]")
{
    MuteSoloState s;
    s.setSoloed (1, true);
    s.setMuted (1, true);
    CHECK (! s.isAudible (1));
}

TEST_CASE ("MuteSoloState: clear resets everything", "[playback][mutesolo]")
{
    MuteSoloState s;
    s.setMuted (1, true);
    s.setSoloed (2, true);
    s.clear();
    CHECK (s.isAudible (1));
    CHECK (s.isAudible (3));
}

TEST_CASE ("MuteSoloState: apply writes the snapshot's audible flags (beyond 64 tracks too)", "[playback][mutesolo]")
{
    SongDocument doc;
    juce::int64 lastId = -1;
    for (int i = 0; i < 70; ++i)
        lastId = (juce::int64) addTrack (doc).getProperty (SongIDs::trackId);

    const auto snap = buildSnapshot (doc);
    MuteSoloState s;
    s.setMuted (lastId, true);
    s.apply (*snap);
    CHECK (! snap->isAudible (snap->trackIndexForId (lastId)));
    CHECK (snap->isAudible (1));
}

TEST_CASE ("MuteSoloState: entries survive for ids that are no longer in the song (undo of removeTrack)", "[playback][mutesolo]")
{
    MuteSoloState s;
    s.setMuted (42, true);
    SongDocument doc;                 // song without track 42
    s.apply (*buildSnapshot (doc));   // must not crash or drop the entry
    CHECK (s.isMuted (42));
}

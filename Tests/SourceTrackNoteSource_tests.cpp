// Verifies SourceTrackNoteSource reads NOTE children live off a MIDI_TRACK
// ValueTree (no internal cache) and answers PianoRollNoteSource's tick/pitch
// range queries correctly, including the empty-track edge case.

#include "UI/SourceTrackNoteSource.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace
{
    juce::ValueTree makeTrack (juce::int64 trackId, int colorArgb)
    {
        juce::ValueTree track (SongIDs::MIDI_TRACK);
        track.setProperty (SongIDs::trackId, trackId, nullptr);
        track.setProperty (SongIDs::name, "Test Track", nullptr);
        track.setProperty (SongIDs::colorArgb, colorArgb, nullptr);
        track.setProperty (SongIDs::sourceMidiChannel, 0, nullptr);
        track.setProperty (SongIDs::importBatch, 0, nullptr);
        return track;
    }

    juce::ValueTree makeNote (int pitch, int startTick, int durationTicks)
    {
        juce::ValueTree note (SongIDs::NOTE);
        note.setProperty (SongIDs::pitch, pitch, nullptr);
        note.setProperty (SongIDs::startTick, startTick, nullptr);
        note.setProperty (SongIDs::durationTicks, durationTicks, nullptr);
        note.setProperty (SongIDs::velocity, 100, nullptr);
        note.setProperty (SongIDs::isDrum, false, nullptr);
        note.setProperty (SongIDs::sourceTrackIndex, 0, nullptr);
        note.setProperty (SongIDs::sourceEventIndex, 0, nullptr);
        return note;
    }
}

TEST_CASE ("SourceTrackNoteSource: empty track has zero notes and start==end ranges", "[piano-roll]")
{
    auto track = makeTrack (1, (int) 0xFF7FA8D0);
    SourceTrackNoteSource source (track);

    CHECK (source.getNumNotes() == 0);
    CHECK (source.getTickRange().isEmpty());
    CHECK (source.getPitchRange().isEmpty());
}

TEST_CASE ("SourceTrackNoteSource: reads note fields and the track's colour verbatim", "[piano-roll]")
{
    auto track = makeTrack (1, (int) 0xFF7FA8D0);
    track.addChild (makeNote (/*pitch*/ 60, /*start*/ 100, /*dur*/ 240), -1, nullptr);
    SourceTrackNoteSource source (track);

    REQUIRE (source.getNumNotes() == 1);
    auto note = source.getNote (0);
    CHECK (note.pitch == 60);
    CHECK (note.startTick == 100);
    CHECK (note.durationTicks == 240);
    CHECK (note.colourArgb == 0xFF7FA8D0);
}

TEST_CASE ("SourceTrackNoteSource: tick/pitch ranges span every note, not just the first/last added", "[piano-roll]")
{
    auto track = makeTrack (1, (int) 0xFF000000);
    // Deliberately out of order and with the widest span note added in the
    // middle, so a buggy implementation that only looked at getChild(0) and
    // getChild(numNotes-1) would give a wrong answer instead of crashing.
    track.addChild (makeNote (/*pitch*/ 60, /*start*/ 500, /*dur*/ 100), -1, nullptr);
    track.addChild (makeNote (/*pitch*/ 72, /*start*/ 0,   /*dur*/ 50),  -1, nullptr);
    track.addChild (makeNote (/*pitch*/ 48, /*start*/ 900, /*dur*/ 200), -1, nullptr);
    SourceTrackNoteSource source (track);

    REQUIRE (source.getNumNotes() == 3);

    auto tickRange = source.getTickRange();
    CHECK (tickRange.getStart() == 0);
    CHECK (tickRange.getEnd() == 1100);   // last note's startTick + durationTicks

    auto pitchRange = source.getPitchRange();
    CHECK (pitchRange.getStart() == 48);
    CHECK (pitchRange.getEnd() == 73);    // exclusive: highest pitch (72) + 1
}

TEST_CASE ("SourceTrackNoteSource: reads live off the tree, no cache", "[piano-roll]")
{
    auto track = makeTrack (1, (int) 0xFF000000);
    SourceTrackNoteSource source (track);

    REQUIRE (source.getNumNotes() == 0);

    // Mutate the tree after construction — a cached implementation would
    // still report 0 notes here.
    track.addChild (makeNote (64, 0, 480), -1, nullptr);

    CHECK (source.getNumNotes() == 1);
    CHECK (source.getNote (0).pitch == 64);
}

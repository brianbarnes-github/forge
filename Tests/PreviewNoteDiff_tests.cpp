// Verifies diffPreviewNotes joins PreviewResult::assembled and ::pipelined
// notes by (sourceTrackIndex, sourceEventIndex), producing Normal/WillFold/
// Dropped states per the Songsmith plan's ghost-note diff section.

#include "UI/PreviewNoteDiff.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace lotro;

namespace
{
    Note makeNote (int pitch, int startTick, int dur, int velocity,
                   int sourceTrackIndex, int sourceEventIndex)
    {
        Note n;
        n.pitch            = pitch;
        n.startTick        = startTick;
        n.durationTicks    = dur;
        n.velocity         = velocity;
        n.isDrum           = false;
        n.sourceTrackIndex = sourceTrackIndex;
        n.sourceEventIndex = sourceEventIndex;
        return n;
    }

    const PreviewNote& findByKey (const std::vector<PreviewNote>& notes, int trackIdx, int eventIdx)
    {
        auto it = std::find_if (notes.begin(), notes.end(), [&] (const PreviewNote& n)
        {
            return n.sourceTrackIndex == trackIdx && n.sourceEventIndex == eventIdx;
        });
        REQUIRE (it != notes.end());
        return *it;
    }
}

TEST_CASE ("PreviewNoteDiff: an unchanged note (same pitch in both) is Normal with no postPitch", "[previewnotediff]")
{
    PreviewResult result;
    Track assembledTrack;
    assembledTrack.notes.push_back (makeNote (60, 0, 480, 100, 0, 0));
    result.assembled.tracks.push_back (assembledTrack);

    Track pipelinedTrack;
    pipelinedTrack.notes.push_back (makeNote (60, 0, 480, 100, 0, 0));
    result.pipelined.tracks.push_back (pipelinedTrack);

    auto diff = diffPreviewNotes (result);

    REQUIRE (diff.size() == 1);
    const auto& n = findByKey (diff, 0, 0);
    CHECK (n.prePitch == 60);
    CHECK (n.state == NoteState::Normal);
    CHECK_FALSE (n.postPitch.has_value());
    CHECK (n.startTick == 0);
    CHECK (n.durationTicks == 480);
    CHECK (n.velocity == 100);
}

TEST_CASE ("PreviewNoteDiff: a note whose pitch differs between assembled/pipelined (same key) is WillFold with the pipelined pitch as postPitch", "[previewnotediff]")
{
    PreviewResult result;
    Track assembledTrack;
    // Pre-fold pitch, out of range.
    assembledTrack.notes.push_back (makeNote (20, 0, 480, 100, 1, 3));
    result.assembled.tracks.push_back (assembledTrack);

    Track pipelinedTrack;
    // Post-fold destination pitch.
    pipelinedTrack.notes.push_back (makeNote (44, 0, 480, 100, 1, 3));
    result.pipelined.tracks.push_back (pipelinedTrack);

    auto diff = diffPreviewNotes (result);

    REQUIRE (diff.size() == 1);
    const auto& n = findByKey (diff, 1, 3);
    CHECK (n.prePitch == 20);
    CHECK (n.state == NoteState::WillFold);
    REQUIRE (n.postPitch.has_value());
    CHECK (*n.postPitch == 44);
}

TEST_CASE ("PreviewNoteDiff: a note present in assembled but absent from pipelined is Dropped with no postPitch", "[previewnotediff]")
{
    PreviewResult result;
    Track assembledTrack;
    assembledTrack.notes.push_back (makeNote (60, 0, 480, 100, 2, 5));
    result.assembled.tracks.push_back (assembledTrack);

    Track pipelinedTrack; // dropped somewhere in the pipeline — no matching key
    result.pipelined.tracks.push_back (pipelinedTrack);

    auto diff = diffPreviewNotes (result);

    REQUIRE (diff.size() == 1);
    const auto& n = findByKey (diff, 2, 5);
    CHECK (n.prePitch == 60);
    CHECK (n.state == NoteState::Dropped);
    CHECK_FALSE (n.postPitch.has_value());
}

TEST_CASE ("PreviewNoteDiff: joins across multiple tracks on both sides, not just track 0", "[previewnotediff]")
{
    PreviewResult result;

    Track assembledA;
    assembledA.notes.push_back (makeNote (60, 0, 480, 100, 0, 0));
    Track assembledB;
    assembledB.notes.push_back (makeNote (30, 0, 480, 100, 1, 0));
    result.assembled.tracks = { assembledA, assembledB };

    Track pipelinedA;
    pipelinedA.notes.push_back (makeNote (60, 0, 480, 100, 0, 0));
    Track pipelinedB;
    pipelinedB.notes.push_back (makeNote (42, 0, 480, 100, 1, 0));
    result.pipelined.tracks = { pipelinedA, pipelinedB };

    auto diff = diffPreviewNotes (result);

    REQUIRE (diff.size() == 2);
    CHECK (findByKey (diff, 0, 0).state == NoteState::Normal);
    const auto& folded = findByKey (diff, 1, 0);
    CHECK (folded.state == NoteState::WillFold);
    REQUIRE (folded.postPitch.has_value());
    CHECK (*folded.postPitch == 42);
}

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

TEST_CASE ("PreviewNoteDiff: two notes with the same provenance key are matched in start order", "[previewnotediff][sections]")
{
    PreviewResult result;
    Track assembledTrack;
    assembledTrack.notes.push_back (makeNote (60, 0, 400, 100, 2, 7));
    assembledTrack.notes.push_back (makeNote (60, 400, 560, 100, 2, 7));
    result.assembled.tracks.push_back (assembledTrack);

    Track pipelinedTrack;
    pipelinedTrack.notes.push_back (makeNote (72, 0, 400, 100, 2, 7));   // both fold up an octave
    pipelinedTrack.notes.push_back (makeNote (72, 400, 560, 100, 2, 7));
    result.pipelined.tracks.push_back (pipelinedTrack);

    const auto diff = diffPreviewNotes (result);
    REQUIRE (diff.size() == 2);
    for (const auto& n : diff)
    {
        CHECK (n.state == NoteState::WillFold);
        REQUIRE (n.postPitch.has_value());
        CHECK (*n.postPitch == 72);
    }
}

TEST_CASE ("PreviewNoteDiff: halves that the pipeline removed are both Dropped; a lone survivor pairs with the first", "[previewnotediff][sections]")
{
    PreviewResult dropped;
    Track a;
    a.notes.push_back (makeNote (60, 0, 400, 100, 2, 7));
    a.notes.push_back (makeNote (60, 400, 560, 100, 2, 7));
    dropped.assembled.tracks.push_back (a);
    dropped.pipelined.tracks.push_back (Track {});

    for (const auto& n : diffPreviewNotes (dropped))
        CHECK (n.state == NoteState::Dropped);

    PreviewResult partial;
    partial.assembled.tracks.push_back (a);
    Track survivor;
    survivor.notes.push_back (makeNote (60, 0, 400, 100, 2, 7));
    partial.pipelined.tracks.push_back (survivor);

    const auto diff = diffPreviewNotes (partial);
    REQUIRE (diff.size() == 2);
    CHECK (diff[0].startTick == 0);
    CHECK (diff[0].state == NoteState::Normal);
    CHECK (diff[1].startTick == 400);
    CHECK (diff[1].state == NoteState::Dropped);
}

TEST_CASE ("PreviewNoteDiff: halves are matched by start order regardless of storage order", "[previewnotediff][sections]")
{
    PreviewResult result;
    Track a;
    a.notes.push_back (makeNote (60, 400, 560, 100, 2, 7));   // later half stored first
    a.notes.push_back (makeNote (60, 0, 400, 100, 2, 7));
    result.assembled.tracks.push_back (a);

    Track p;
    p.notes.push_back (makeNote (60, 400, 560, 100, 2, 7));
    p.notes.push_back (makeNote (72, 0, 400, 100, 2, 7));     // only the early half folds
    result.pipelined.tracks.push_back (p);

    const auto diff = diffPreviewNotes (result);
    REQUIRE (diff.size() == 2);
    CHECK (diff[0].startTick == 400);
    CHECK (diff[0].state == NoteState::Normal);
    CHECK (diff[1].startTick == 0);
    CHECK (diff[1].state == NoteState::WillFold);
    REQUIRE (diff[1].postPitch.has_value());
    CHECK (*diff[1].postPitch == 72);
}

TEST_CASE ("PreviewNoteDiff: editor-created notes (-1/-1) pair by start order, not last-write-wins", "[previewnotediff][sections]")
{
    PreviewResult result;
    Track a;
    a.notes.push_back (makeNote (20, 0, 100, 100, -1, -1));
    a.notes.push_back (makeNote (60, 200, 100, 100, -1, -1));
    result.assembled.tracks.push_back (a);

    Track p;
    p.notes.push_back (makeNote (44, 0, 100, 100, -1, -1));    // first folds
    p.notes.push_back (makeNote (60, 200, 100, 100, -1, -1));
    result.pipelined.tracks.push_back (p);

    const auto diff = diffPreviewNotes (result);
    REQUIRE (diff.size() == 2);
    CHECK (diff[0].state == NoteState::WillFold);
    REQUIRE (diff[0].postPitch.has_value());
    CHECK (*diff[0].postPitch == 44);
    CHECK (diff[1].state == NoteState::Normal);
}

TEST_CASE ("PreviewNoteDiff: empty tracks yield an empty diff", "[previewnotediff][sections]")
{
    PreviewResult result;
    result.assembled.tracks.push_back (Track {});
    result.pipelined.tracks.push_back (Track {});
    CHECK (diffPreviewNotes (result).empty());
}

TEST_CASE ("PreviewNoteDiff: when the second half survives, the first half is the one reported Dropped", "[previewnotediff][sections]")
{
    PreviewResult result;
    Track a;
    a.notes.push_back (makeNote (60, 0, 400, 100, 2, 7));
    a.notes.push_back (makeNote (60, 400, 560, 100, 2, 7));
    result.assembled.tracks.push_back (a);
    Track p;
    p.notes.push_back (makeNote (72, 400, 560, 100, 2, 7));   // survivor, folded
    result.pipelined.tracks.push_back (p);

    const auto diff = diffPreviewNotes (result);
    REQUIRE (diff.size() == 2);
    CHECK (diff[0].startTick == 0);
    CHECK (diff[0].state == NoteState::Dropped);
    CHECK (diff[1].startTick == 400);
    CHECK (diff[1].state == NoteState::WillFold);
    REQUIRE (diff[1].postPitch.has_value());
    CHECK (*diff[1].postPitch == 72);
}

TEST_CASE ("PreviewNoteDiff: a same-tick chord of editor-created notes pairs each note with its own counterpart", "[previewnotediff][sections]")
{
    PreviewResult result;
    Track a;
    a.notes.push_back (makeNote (64, 0, 100, 100, -1, -1));
    a.notes.push_back (makeNote (60, 0, 100, 100, -1, -1));
    a.notes.push_back (makeNote (20, 0, 100, 100, -1, -1));
    result.assembled.tracks.push_back (a);
    Track p;
    p.notes.push_back (makeNote (64, 0, 100, 100, -1, -1));
    p.notes.push_back (makeNote (60, 0, 100, 100, -1, -1));
    p.notes.push_back (makeNote (44, 0, 100, 100, -1, -1));   // 20 folded up
    result.pipelined.tracks.push_back (p);

    const auto diff = diffPreviewNotes (result);
    REQUIRE (diff.size() == 3);
    int folded = 0;
    for (const auto& n : diff)
    {
        if (n.prePitch == 20)
        {
            CHECK (n.state == NoteState::WillFold);
            REQUIRE (n.postPitch.has_value());
            CHECK (*n.postPitch == 44);
            ++folded;
        }
        else
        {
            CHECK (n.state == NoteState::Normal);
        }
    }
    CHECK (folded == 1);
}

TEST_CASE ("PreviewNoteDiff: a dropped early editor-created note does not shift later pairings", "[previewnotediff][sections]")
{
    PreviewResult result;
    Track a;
    a.notes.push_back (makeNote (60, 0, 100, 100, -1, -1));     // dropped
    a.notes.push_back (makeNote (20, 200, 100, 100, -1, -1));   // folds
    a.notes.push_back (makeNote (62, 400, 100, 100, -1, -1));
    result.assembled.tracks.push_back (a);
    Track p;
    p.notes.push_back (makeNote (44, 200, 100, 100, -1, -1));
    p.notes.push_back (makeNote (62, 400, 100, 100, -1, -1));
    result.pipelined.tracks.push_back (p);

    const auto diff = diffPreviewNotes (result);
    REQUIRE (diff.size() == 3);
    CHECK (diff[0].state == NoteState::Dropped);
    CHECK (diff[1].state == NoteState::WillFold);
    REQUIRE (diff[1].postPitch.has_value());
    CHECK (*diff[1].postPitch == 44);
    CHECK (diff[2].state == NoteState::Normal);
}

TEST_CASE ("PreviewNoteDiff: rescaled start ticks fall back to start order", "[previewnotediff][sections]")
{
    PreviewResult result;
    Track a;
    a.notes.push_back (makeNote (60, 0, 400, 100, 2, 7));
    a.notes.push_back (makeNote (60, 400, 560, 100, 2, 7));
    result.assembled.tracks.push_back (a);
    Track p;
    p.notes.push_back (makeNote (60, 0, 200, 100, 2, 7));
    p.notes.push_back (makeNote (72, 200, 280, 100, 2, 7));   // tempo-collapsed to half the ticks
    result.pipelined.tracks.push_back (p);

    const auto diff = diffPreviewNotes (result);
    REQUIRE (diff.size() == 2);
    CHECK (diff[0].state == NoteState::Normal);
    CHECK (diff[1].state == NoteState::WillFold);
}

TEST_CASE ("PreviewNoteDiff: a fold that reorders a same-tick chord leaves its unchanged notes Normal", "[previewnotediff][sections]")
{
    PreviewResult result;
    Track a;
    for (int pitch : { 20, 60, 64 })
        a.notes.push_back (makeNote (pitch, 0, 100, 100, -1, -1));
    result.assembled.tracks.push_back (a);
    Track p;
    for (int pitch : { 60, 64, 80 })   // 20 folded up to 80
        p.notes.push_back (makeNote (pitch, 0, 100, 100, -1, -1));
    result.pipelined.tracks.push_back (p);

    const auto diff = diffPreviewNotes (result);
    REQUIRE (diff.size() == 3);
    CHECK (diff[0].prePitch == 20);
    CHECK (diff[0].state == NoteState::WillFold);
    REQUIRE (diff[0].postPitch.has_value());
    CHECK (*diff[0].postPitch == 80);
    CHECK (diff[1].state == NoteState::Normal);
    CHECK (diff[2].state == NoteState::Normal);
}

TEST_CASE ("PreviewNoteDiff: a dropped middle note of a same-tick chord does not mis-pair the notes above it", "[previewnotediff][sections]")
{
    PreviewResult result;
    Track a;
    for (int pitch : { 60, 62, 64, 67 })
        a.notes.push_back (makeNote (pitch, 0, 100, 100, -1, -1));
    result.assembled.tracks.push_back (a);
    Track p;
    for (int pitch : { 60, 64, 67 })
        p.notes.push_back (makeNote (pitch, 0, 100, 100, -1, -1));
    result.pipelined.tracks.push_back (p);

    const auto diff = diffPreviewNotes (result);
    REQUIRE (diff.size() == 4);
    CHECK (diff[0].state == NoteState::Normal);
    CHECK (diff[1].prePitch == 62);
    CHECK (diff[1].state == NoteState::Dropped);
    CHECK (diff[2].state == NoteState::Normal);
    CHECK (diff[3].state == NoteState::Normal);
}

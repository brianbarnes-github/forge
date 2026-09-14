// Verifies PreviewNoteSource maps PreviewNote's fields onto
// PianoRollNoteSource's PianoRollNote (pitch always the PRE-fold pitch, never
// postPitch), colours Normal notes differently from WillFold/Dropped notes,
// and computes tick/pitch ranges over prePitch/startTick/durationTicks.

#include "UI/PreviewNoteSource.h"
#include "UI/SongsmithColours.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace
{
    PreviewNote makeNormal (int prePitch, int startTick, int durationTicks,
                             int sourceTrackIndex = 0, int sourceEventIndex = 0)
    {
        PreviewNote n;
        n.sourceTrackIndex = sourceTrackIndex;
        n.sourceEventIndex = sourceEventIndex;
        n.prePitch         = prePitch;
        n.startTick        = startTick;
        n.durationTicks    = durationTicks;
        n.velocity         = 100;
        n.state            = NoteState::Normal;
        return n;
    }
}

TEST_CASE ("PreviewNoteSource: an empty source has zero notes and start==end ranges", "[previewnotesource]")
{
    PreviewNoteSource source ({});

    CHECK (source.getNumNotes() == 0);
    CHECK (source.getTickRange().getStart() == 0);
    CHECK (source.getTickRange().getEnd() == 0);
    CHECK (source.getPitchRange().getStart() == 0);
    CHECK (source.getPitchRange().getEnd() == 0);
}

TEST_CASE ("PreviewNoteSource: a Normal note is positioned/coloured at prePitch, provenance copied verbatim", "[previewnotesource]")
{
    std::vector<PreviewNote> notes { makeNormal (60, 100, 240, /*sourceTrackIndex*/ 2, /*sourceEventIndex*/ 5) };
    PreviewNoteSource source (notes);

    REQUIRE (source.getNumNotes() == 1);
    auto note = source.getNote (0);
    CHECK (note.pitch == 60);
    CHECK (note.startTick == 100);
    CHECK (note.durationTicks == 240);
    CHECK (note.sourceTrackIndex == 2);
    CHECK (note.sourceEventIndex == 5);
    CHECK (note.state == NoteState::Normal);
    CHECK_FALSE (note.postPitch.has_value());
    CHECK (note.colourArgb == SongsmithColours::previewNoteNormal);
}

TEST_CASE ("PreviewNoteSource: a WillFold note is positioned at prePitch (not postPitch) but coloured red, postPitch preserved", "[previewnotesource]")
{
    // Deliberately distinct pre/post pitches: a bug that positions the roll
    // rect at postPitch instead of prePitch would place this note 24
    // semitones away from the correct row, easy to catch numerically.
    PreviewNote n = makeNormal (84, 50, 100);
    n.state    = NoteState::WillFold;
    n.postPitch = 60;

    std::vector<PreviewNote> notes { n };
    PreviewNoteSource source (notes);

    REQUIRE (source.getNumNotes() == 1);
    auto note = source.getNote (0);
    CHECK (note.pitch == 84); // pre-fold pitch, not the 60 destination
    CHECK (note.state == NoteState::WillFold);
    REQUIRE (note.postPitch.has_value());
    CHECK (*note.postPitch == 60);
    CHECK (note.colourArgb == SongsmithColours::outOfRangeFill);
}

TEST_CASE ("PreviewNoteSource: a Dropped note is coloured red with no postPitch", "[previewnotesource]")
{
    PreviewNote n = makeNormal (30, 0, 480);
    n.state = NoteState::Dropped;

    std::vector<PreviewNote> notes { n };
    PreviewNoteSource source (notes);

    REQUIRE (source.getNumNotes() == 1);
    auto note = source.getNote (0);
    CHECK (note.state == NoteState::Dropped);
    CHECK_FALSE (note.postPitch.has_value());
    CHECK (note.colourArgb == SongsmithColours::outOfRangeFill);
}

TEST_CASE ("PreviewNoteSource: tick/pitch ranges span every note's prePitch/startTick/durationTicks", "[previewnotesource]")
{
    std::vector<PreviewNote> notes {
        makeNormal (60, 500, 100),
        makeNormal (72, 0,   50),
        makeNormal (48, 900, 200),
    };
    PreviewNoteSource source (notes);

    REQUIRE (source.getNumNotes() == 3);

    auto tickRange = source.getTickRange();
    CHECK (tickRange.getStart() == 0);
    CHECK (tickRange.getEnd() == 1100);

    auto pitchRange = source.getPitchRange();
    CHECK (pitchRange.getStart() == 48);
    CHECK (pitchRange.getEnd() == 73); // exclusive: highest pitch (72) + 1
}

TEST_CASE ("PreviewNoteSource: pitch range also covers a WillFold note's postPitch, even when it falls outside every prePitch", "[previewnotesource]")
{
    // A note that folds UPWARD past every other note's prePitch (e.g. pitch
    // 20 -> 44, LuteOfAges's range) must still pull the ghost's destination
    // row into the reported range — a bug that only unions prePitch would
    // place the ghost off the top of PianoRollComponent's canvas (C1).
    PreviewNote folding = makeNormal (20, 0, 480);
    folding.state    = NoteState::WillFold;
    folding.postPitch = 44;

    std::vector<PreviewNote> notes {
        folding,
        makeNormal (30, 480, 480), // an ordinary note whose prePitch alone would cap the range lower than 44
    };
    PreviewNoteSource source (notes);

    auto pitchRange = source.getPitchRange();
    CHECK (pitchRange.getStart() == 20);
    CHECK (pitchRange.getEnd() == 45); // exclusive: postPitch (44) + 1, not prePitch's own max (30)
}

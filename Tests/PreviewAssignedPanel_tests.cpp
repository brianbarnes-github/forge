// Verifies PreviewAssignedPanel's Output stats section counts WillFold
// notes separately from Dropped notes (I6) — the plan's Phase 6
// verification step names an "Out of range: N (will octave-shift)" readout,
// and this count was previously computed from the same diff vector the
// Dropped count already iterates but never accumulated.

#include "UI/PreviewAssignedPanel.h"
#include "UI/PreviewNoteDiff.h"
#include "UI/PreviewPipeline.h"
#include "UI/SongModelBridge.h"

#include "Core/LotroInstrument.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace lotro
{
    // Grants PreviewAssignedPanel_tests.cpp access to the private
    // willFoldNotes/droppedNotes counters — no production caller needs a
    // getter for these, so they stay private on the class itself.
    struct PreviewAssignedPanelTestAccess
    {
        static int willFoldNotes (const PreviewAssignedPanel& p) { return p.willFoldNotes; }
        static int droppedNotes (const PreviewAssignedPanel& p) { return p.droppedNotes; }
    };
}

namespace
{
    using Access = PreviewAssignedPanelTestAccess;

    Note makeNote (int pitch, int startTick, int dur, int sourceTrackIndex, int sourceEventIndex)
    {
        Note n;
        n.pitch            = pitch;
        n.startTick        = startTick;
        n.durationTicks    = dur;
        n.velocity         = 100;
        n.isDrum           = false;
        n.sourceTrackIndex = sourceTrackIndex;
        n.sourceEventIndex = sourceEventIndex;
        return n;
    }

    // Imports `imported` and wires its single track to a new LuteOfAges
    // part, returning the minted partId (mirrors PreviewPipeline_tests.cpp's
    // own helper of the same name).
    juce::int64 importOneTrackAsLuteOfAgesPart (SongDocument& doc, const Song& imported)
    {
        Diagnostics importDiags;
        appendImportedSong (doc, imported, 1, importDiags);

        auto track = doc.getTrack (doc.getNumTracks() - 1);
        auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);

        auto part = doc.addPart ("LuteOfAges", "Lead");
        doc.addAssignment (part, trackId, 0, 0, "octaveShift");
        return (juce::int64) part.getProperty (SongIDs::partId);
    }
}

TEST_CASE ("PreviewAssignedPanel: setPreview counts WillFold notes separately from Dropped notes", "[previewassignedpanel]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    Song raw;
    raw.ticksPerQuarter = 480;
    Track t;
    t.name = "Track Zero";
    // 20 is below LuteOfAges's native range (36..72); RangeConstraint folds
    // it up by whole octaves (20 -> 32 -> 44) rather than dropping it, so
    // this note is a WillFold in the diff, not a Dropped.
    t.notes.push_back (makeNote (20, 0, 480, 0, 0));
    raw.tracks.push_back (t);

    SongDocument doc;
    const auto partId = importOneTrackAsLuteOfAgesPart (doc, raw);

    auto result = computePartPreview (doc, partId);
    auto diff = diffPreviewNotes (result);

    PreviewAssignedPanel panel;
    panel.setPreview (doc, partId, result, diff);

    CHECK (Access::willFoldNotes (panel) == 1);
    CHECK (Access::droppedNotes (panel) == 0);
}

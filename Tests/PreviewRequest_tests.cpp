#include "PlaybackTestSupport.h"
#include "UI/NotePreview.h"
#include "UI/Playback/PreviewRequest.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace lotro;
using namespace lotro::playbacktest;

namespace
{
    std::vector<juce::ValueTree> notesOf (juce::ValueTree track)
    {
        std::vector<juce::ValueTree> out;
        const auto notes = SongDocument::getNotesNode (track);
        for (int i = 0; i < notes.getNumChildren(); ++i)
            out.push_back (notes.getChild (i));
        return out;
    }
}

TEST_CASE ("PreviewRequest: one note becomes a one-note request with its channel, pitch, velocity and span", "[note-preview]")
{
    SongDocument doc;
    auto track = addTrack (doc);
    addNote (track, 64, 480, 240, 90, 3);

    const auto r = makePreviewRequest (7, notesOf (track));
    REQUIRE (r.has_value());
    REQUIRE (r->count == 1);
    const auto& n = r->notes[0];
    CHECK (n.trackId == 7);
    CHECK (n.midiChannel == 3);
    CHECK (n.pitch == 64);
    CHECK (n.velocity == 90);
    CHECK (n.startTick == 480);
    CHECK (n.endTick == 720);
}

TEST_CASE ("PreviewRequest: up to six notes starting together are a chord", "[note-preview]")
{
    SongDocument doc;
    auto track = addTrack (doc);
    for (int i = 0; i < 6; ++i)
        addNote (track, 60 + i, 0, 480);

    const auto r = makePreviewRequest (1, notesOf (track));
    REQUIRE (r.has_value());
    REQUIRE (r->count == 6);
    for (int i = 0; i < 6; ++i)
        CHECK (r->notes[(size_t) i].pitch == 60 + i);
}

TEST_CASE ("PreviewRequest: seven notes, staggered starts, or no notes give no request", "[note-preview]")
{
    SongDocument doc;
    auto seven = addTrack (doc, "Seven");
    for (int i = 0; i < 7; ++i)
        addNote (seven, 60 + i, 0, 480);
    CHECK_FALSE (makePreviewRequest (1, notesOf (seven)).has_value());

    auto staggered = addTrack (doc, "Staggered");
    addNote (staggered, 60, 0, 480);
    addNote (staggered, 62, 480, 480);
    CHECK_FALSE (makePreviewRequest (1, notesOf (staggered)).has_value());

    CHECK_FALSE (makePreviewRequest (1, {}).has_value());
}

TEST_CASE ("NotePreview: previewWanted follows the matching checkbox", "[note-preview]")
{
    const NotePreviewSettings none;
    CHECK_FALSE (previewWanted (none, NoteChange::inserted));
    CHECK_FALSE (previewWanted (none, NoteChange::edited));
    CHECK_FALSE (previewWanted (none, NoteChange::velocityChanged));

    NotePreviewSettings insert; insert.onInsert = true;
    CHECK (previewWanted (insert, NoteChange::inserted));
    CHECK_FALSE (previewWanted (insert, NoteChange::edited));
    CHECK_FALSE (previewWanted (insert, NoteChange::velocityChanged));

    NotePreviewSettings edit; edit.onEdit = true;
    CHECK_FALSE (previewWanted (edit, NoteChange::inserted));
    CHECK (previewWanted (edit, NoteChange::edited));
    CHECK_FALSE (previewWanted (edit, NoteChange::velocityChanged));

    NotePreviewSettings velocity; velocity.onVelocity = true;
    CHECK_FALSE (previewWanted (velocity, NoteChange::inserted));
    CHECK_FALSE (previewWanted (velocity, NoteChange::edited));
    CHECK (previewWanted (velocity, NoteChange::velocityChanged));

    const NotePreviewSettings all { true, true, true };
    CHECK (previewWanted (all, NoteChange::inserted));
    CHECK (previewWanted (all, NoteChange::edited));
    CHECK (previewWanted (all, NoteChange::velocityChanged));
    CHECK (all != none);
    CHECK (all == NotePreviewSettings { true, true, true });
}

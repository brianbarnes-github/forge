#include "PlaybackTestSupport.h"
#include "UI/NoteClipboard.h"
#include "UI/SongDocument.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>

using namespace lotro;
using namespace lotro::playbacktest;

namespace
{
    using Note = std::array<int, 4>;   // pitch, start, duration, velocity

    Note fieldsOf (const juce::ValueTree& n)
    {
        return { (int) n.getProperty (SongIDs::pitch), (int) n.getProperty (SongIDs::startTick),
                 (int) n.getProperty (SongIDs::durationTicks), (int) n.getProperty (SongIDs::velocity) };
    }

    std::vector<Note> notesOf (const juce::ValueTree& track)
    {
        std::vector<Note> out;
        const auto notes = SongDocument::getNotesNode (track);
        for (int i = 0; i < notes.getNumChildren(); ++i)
            out.push_back (fieldsOf (notes.getChild (i)));
        std::sort (out.begin(), out.end());
        return out;
    }

    std::vector<juce::ValueTree> allNotes (const juce::ValueTree& track)
    {
        std::vector<juce::ValueTree> out;
        const auto notes = SongDocument::getNotesNode (track);
        for (int i = 0; i < notes.getNumChildren(); ++i)
            out.push_back (notes.getChild (i));
        return out;
    }
}

TEST_CASE ("note clipboard: a copy is detached, earliest start first, and remembers its origin", "[note-clipboard]")
{
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCCu, 0, 0);
    addNote (track, 64, 960, 240, 90);
    addNote (track, 60, 480, 480, 100);

    const auto clip = copyNotes (allNotes (track));
    REQUIRE (clip.notes.size() == 2);
    CHECK (fieldsOf (clip.notes[0]) == Note { 60, 480, 480, 100 });
    CHECK (fieldsOf (clip.notes[1]) == Note { 64, 960, 240, 90 });
    CHECK (clip.originTick() == 480);
    CHECK_FALSE (clip.empty());

    // Editing or deleting the originals afterwards leaves the clipboard alone.
    SongDocument::getNotesNode (track).getChild (0).setProperty (SongIDs::pitch, 70, nullptr);
    SongDocument::getNotesNode (track).removeAllChildren (nullptr);
    CHECK (fieldsOf (clip.notes[1]) == Note { 64, 960, 240, 90 });
    CHECK_FALSE (clip.notes[0].getParent().isValid());

    CHECK (copyNotes ({}).empty());
}

TEST_CASE ("note clipboard: a paste shifts the notes so the earliest starts at the tick, keeping their spacing, in one undo step", "[note-clipboard]")
{
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCCu, 0, 0);
    addNote (track, 60, 480, 480, 100);
    addNote (track, 64, 960, 240, 90);
    const auto clip = copyNotes (allNotes (track));

    const auto pasted = pasteNotes (doc, track, clip, 2000);
    REQUIRE (pasted.size() == 2);
    for (const auto& n : pasted)
        CHECK (n.getParent() == SongDocument::getNotesNode (track));
    CHECK (notesOf (track) == std::vector<Note> {
        { 60, 480, 480, 100 }, { 60, 2000, 480, 100 }, { 64, 960, 240, 90 }, { 64, 2480, 240, 90 } });

    doc.undo();
    CHECK (notesOf (track) == std::vector<Note> { { 60, 480, 480, 100 }, { 64, 960, 240, 90 } });

    // At its origin tick the copy lands exactly on top of the originals.
    pasteNotes (doc, track, clip, clip.originTick());
    CHECK (notesOf (track) == std::vector<Note> {
        { 60, 480, 480, 100 }, { 60, 480, 480, 100 }, { 64, 960, 240, 90 }, { 64, 960, 240, 90 } });
}

TEST_CASE ("note clipboard: pasted notes are new material for the target track, as a merge makes them", "[note-clipboard]")
{
    SongDocument doc;
    auto source = doc.addTrack ("A", (int) 0xFFAABBCCu, 0, 0);
    auto target = doc.addTrack ("B", (int) 0xFFDDEEFFu, 1, 0);
    target.setProperty (SongIDs::defaultChannel, 10, nullptr);
    addNote (source, 60, 480, 480, 100, 3);
    auto original = SongDocument::getNotesNode (source).getChild (0);
    original.setProperty (SongIDs::sourceTrackIndex, 4, nullptr);
    original.setProperty (SongIDs::sourceEventIndex, 17, nullptr);
    original.setProperty (SongIDs::onOrder, 5, nullptr);
    original.setProperty (SongIDs::offOrder, 6, nullptr);
    original.setProperty (SongIDs::sectionId, 99, nullptr);

    const auto pasted = pasteNotes (doc, target, copyNotes ({ original }), 480);
    REQUIRE (pasted.size() == 1);
    const auto& n = pasted[0];
    CHECK ((int) n.getProperty (SongIDs::channel) == 10);   // the target's channel
    CHECK ((bool) n.getProperty (SongIDs::isDrum));
    // A fresh provenance pair, so it never shares a join key with the original.
    CHECK ((int) n.getProperty (SongIDs::sourceTrackIndex) == -1);
    CHECK ((int) n.getProperty (SongIDs::sourceEventIndex) == -1);
    CHECK_FALSE (n.hasProperty (SongIDs::onOrder));
    CHECK_FALSE (n.hasProperty (SongIDs::offOrder));
    CHECK_FALSE (n.hasProperty (SongIDs::sectionId));   // the target has no stored sections
    CHECK ((int) original.getProperty (SongIDs::sourceEventIndex) == 17);   // the original is untouched
}

TEST_CASE ("note clipboard: an empty clipboard or no track pastes nothing and opens no undo step", "[note-clipboard]")
{
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCCu, 0, 0);
    doc.getUndoManager().clearUndoHistory();

    CHECK (pasteNotes (doc, track, {}, 0).empty());
    addNote (track, 60, 0, 480);
    CHECK (pasteNotes (doc, {}, copyNotes (allNotes (track)), 0).empty());
    CHECK_FALSE (doc.canUndo());
    CHECK (SongDocument::getNotesNode (track).getNumChildren() == 1);
}

TEST_CASE ("note clipboard: into a track whose notes use more than one channel, a pasted note keeps its own channel", "[note-clipboard]")
{
    SongDocument doc;
    auto source = doc.addTrack ("A", (int) 0xFFAABBCCu, 0, 0);
    auto mixed = doc.addTrack ("B", (int) 0xFFDDEEFFu, 1, 0);
    auto single = doc.addTrack ("C", (int) 0xFF112233u, 2, 0);
    mixed.setProperty (SongIDs::defaultChannel, 2, nullptr);
    single.setProperty (SongIDs::defaultChannel, 4, nullptr);
    addNote (mixed, 50, 0, 480, 100, 2);
    addNote (mixed, 52, 0, 480, 100, 5);
    addNote (single, 50, 0, 480, 100, 4);
    addNote (source, 60, 480, 480, 100, 10);
    auto original = SongDocument::getNotesNode (source).getChild (0);
    original.setProperty (SongIDs::isDrum, true, nullptr);
    const auto clip = copyNotes ({ original });

    const auto kept = pasteNotes (doc, mixed, clip, 960);
    REQUIRE (kept.size() == 1);
    CHECK ((int) kept[0].getProperty (SongIDs::channel) == 10);   // two channels in B: the note's own
    CHECK ((bool) kept[0].getProperty (SongIDs::isDrum));
    CHECK ((int) kept[0].getProperty (SongIDs::sourceEventIndex) == -1);   // still new material

    const auto taken = pasteNotes (doc, single, clip, 960);
    REQUIRE (taken.size() == 1);
    CHECK ((int) taken[0].getProperty (SongIDs::channel) == 4);   // one channel in C: the track's
    CHECK_FALSE ((bool) taken[0].getProperty (SongIDs::isDrum));
}

#include "PlaybackTestSupport.h"
#include "UI/NoteMerge.h"
#include "UI/SectionEdit.h"
#include "UI/SongDocument.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>

using namespace lotro;
using namespace lotro::playbacktest;

namespace
{
    using Note = std::array<int, 3>;   // pitch, start, duration

    std::vector<Note> notesOf (const juce::ValueTree& track)
    {
        std::vector<Note> out;
        const auto notes = SongDocument::getNotesNode (track);
        for (int i = 0; i < notes.getNumChildren(); ++i)
        {
            const auto n = notes.getChild (i);
            out.push_back ({ (int) n.getProperty (SongIDs::pitch), (int) n.getProperty (SongIDs::startTick),
                             (int) n.getProperty (SongIDs::durationTicks) });
        }
        std::sort (out.begin(), out.end());
        return out;
    }

    SectionRef wholeTrack (const juce::ValueTree& t)
    {
        return { (juce::int64) t.getProperty (SongIDs::trackId), 0 };   // 0 = the virtual section
    }

    juce::int64 idOf (const juce::ValueTree& t) { return (juce::int64) t.getProperty (SongIDs::trackId); }

    juce::ValueTree addStoredSection (SongDocument& doc, juce::ValueTree track, int start, int end)
    {
        auto sections = track.getChildWithName (SongIDs::SECTIONS);
        if (! sections.isValid())
        {
            sections = juce::ValueTree (SongIDs::SECTIONS);
            track.addChild (sections, -1, nullptr);
        }
        juce::ValueTree s (SongIDs::SECTION);
        s.setProperty (SongIDs::sectionId, doc.mintSectionId(), nullptr);
        s.setProperty (SongIDs::startTick, start, nullptr);
        s.setProperty (SongIDs::endTick, end, nullptr);
        sections.addChild (s, -1, nullptr);
        return s;
    }
}

TEST_CASE ("merge: a move puts the notes in the target and removes them from the source, in one undo step", "[merge]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S");
    auto target = addTrack (doc, "T");
    addNote (source, 60, 0, 480);
    addNote (source, 62, 480, 480);

    const auto r = mergeSections (doc, { wholeTrack (source) }, idOf (target), false);

    CHECK (r.changed);
    CHECK (r.inserted == 2);
    CHECK (notesOf (target) == std::vector<Note> { { 60, 0, 480 }, { 62, 480, 480 } });
    CHECK (notesOf (source).empty());

    doc.undo();
    CHECK (notesOf (source) == std::vector<Note> { { 60, 0, 480 }, { 62, 480, 480 } });
    CHECK (notesOf (target).empty());
    CHECK_FALSE (doc.canUndo());   // the whole merge was one transaction
}

TEST_CASE ("merge: a copy leaves the source alone", "[merge]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S");
    auto target = addTrack (doc, "T");
    addNote (source, 60, 0, 480);

    const auto r = mergeSections (doc, { wholeTrack (source) }, idOf (target), true);

    CHECK (r.changed);
    CHECK (notesOf (source) == std::vector<Note> { { 60, 0, 480 } });
    CHECK (notesOf (target) == std::vector<Note> { { 60, 0, 480 } });
}

TEST_CASE ("merge: only the referenced section's notes are carried", "[merge]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S");
    auto target = addTrack (doc, "T");
    addNote (source, 60, 0, 480);
    addNote (source, 62, 480, 480);
    splitAt (doc, { idOf (source) }, 480);
    const auto second = sectionsOf (source)[1];

    mergeSections (doc, { { idOf (source), second.id } }, idOf (target), false);

    CHECK (notesOf (target) == std::vector<Note> { { 62, 480, 480 } });
    CHECK (notesOf (source) == std::vector<Note> { { 60, 0, 480 } });
}

TEST_CASE ("merge: invalid requests change nothing and open no transaction", "[merge]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S");
    auto target = addTrack (doc, "T");
    addNote (source, 60, 0, 480);

    CHECK_FALSE (mergeSections (doc, { wholeTrack (source) }, idOf (source), false).changed);   // onto itself
    CHECK_FALSE (mergeSections (doc, { wholeTrack (source) }, idOf (doc.getConductorTrack()), false).changed);
    CHECK_FALSE (mergeSections (doc, { wholeTrack (source) }, 9999, false).changed);            // unknown target
    CHECK_FALSE (mergeSections (doc, { { 9999, 0 } }, idOf (target), false).changed);           // unknown source
    CHECK_FALSE (mergeSections (doc, { { idOf (source), 12345 } }, idOf (target), false).changed);   // unknown section
    CHECK_FALSE (mergeSections (doc, {}, idOf (target), false).changed);
    CHECK_FALSE (doc.canUndo());
    CHECK (notesOf (source).size() == 1);
}

TEST_CASE ("merge: canMergeInto needs a non-conductor target and a different source", "[merge]")
{
    SongDocument doc;
    auto a = addTrack (doc, "A");
    auto b = addTrack (doc, "B");
    addNote (a, 60, 0, 480);

    CHECK (canMergeInto (doc, { wholeTrack (a) }, idOf (b)));
    CHECK_FALSE (canMergeInto (doc, { wholeTrack (a) }, idOf (a)));
    CHECK_FALSE (canMergeInto (doc, { wholeTrack (a) }, idOf (doc.getConductorTrack())));
    CHECK_FALSE (canMergeInto (doc, { wholeTrack (a) }, 9999));
    CHECK (canMergeInto (doc, { wholeTrack (a), wholeTrack (b) }, idOf (b)));   // a still differs from the target
    CHECK_FALSE (canMergeInto (doc, {}, idOf (b)));
}

TEST_CASE ("merge: inserted notes are new material on the target's channel", "[merge][provenance]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S");
    auto target = addTrack (doc, "T");
    target.setProperty (SongIDs::defaultChannel, 10, nullptr);

    juce::ValueTree note (SongIDs::NOTE);
    note.setProperty (SongIDs::pitch, 60, nullptr);
    note.setProperty (SongIDs::startTick, 0, nullptr);
    note.setProperty (SongIDs::durationTicks, 480, nullptr);
    note.setProperty (SongIDs::velocity, 77, nullptr);
    note.setProperty (SongIDs::channel, 1, nullptr);
    note.setProperty (SongIDs::onOrder, 3, nullptr);
    note.setProperty (SongIDs::offOrder, 4, nullptr);
    note.setProperty (SongIDs::sourceTrackIndex, 2, nullptr);
    note.setProperty (SongIDs::sourceEventIndex, 5, nullptr);
    SongDocument::appendChildBulk (SongDocument::getNotesNode (source), note);

    mergeSections (doc, { wholeTrack (source) }, idOf (target), true);

    const auto merged = SongDocument::getNotesNode (target).getChild (0);
    CHECK_FALSE (merged.hasProperty (SongIDs::onOrder));
    CHECK_FALSE (merged.hasProperty (SongIDs::offOrder));
    CHECK ((int) merged.getProperty (SongIDs::sourceTrackIndex) == -1);
    CHECK ((int) merged.getProperty (SongIDs::sourceEventIndex) == -1);
    CHECK ((int) merged.getProperty (SongIDs::channel) == 10);
    CHECK ((bool) merged.getProperty (SongIDs::isDrum));
    CHECK ((int) merged.getProperty (SongIDs::velocity) == 77);   // everything else is kept
}

TEST_CASE ("merge: an inserted note is tagged with the target section that holds it, or the nearest", "[merge][sections]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S");
    auto target = addTrack (doc, "T");
    addNote (target, 72, 0, 100);
    const auto s1 = addStoredSection (doc, target, 0, 1000);
    const auto s2 = addStoredSection (doc, target, 2000, 3000);
    addNote (source, 60, 100, 100);    // inside s1
    addNote (source, 62, 5000, 100);   // beyond every section: nearest is s2

    mergeSections (doc, { wholeTrack (source) }, idOf (target), true);

    const auto notes = SongDocument::getNotesNode (target);
    REQUIRE (notes.getNumChildren() == 3);
    for (int i = 0; i < notes.getNumChildren(); ++i)
    {
        const auto n = notes.getChild (i);
        if ((int) n.getProperty (SongIDs::pitch) == 60)
            CHECK ((juce::int64) n.getProperty (SongIDs::sectionId) == (juce::int64) s1.getProperty (SongIDs::sectionId));
        if ((int) n.getProperty (SongIDs::pitch) == 62)
            CHECK ((juce::int64) n.getProperty (SongIDs::sectionId) == (juce::int64) s2.getProperty (SongIDs::sectionId));
    }
}

TEST_CASE ("merge: a note inside an existing same-pitch note is dropped", "[merge][overlap]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S");
    auto target = addTrack (doc, "T");
    addNote (target, 60, 0, 960);
    addNote (source, 60, 240, 240);

    const auto r = mergeSections (doc, { wholeTrack (source) }, idOf (target), true);

    CHECK (r.dropped == 1);
    CHECK_FALSE (r.changed);        // a copy that adds nothing
    CHECK_FALSE (doc.canUndo());    // and opens no transaction
    CHECK (notesOf (target) == std::vector<Note> { { 60, 0, 960 } });
}

TEST_CASE ("merge: a move of a contained note still removes it from the source", "[merge][overlap]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S");
    auto target = addTrack (doc, "T");
    addNote (target, 60, 0, 960);
    addNote (source, 60, 240, 240);

    const auto r = mergeSections (doc, { wholeTrack (source) }, idOf (target), false);

    CHECK (r.changed);
    CHECK (notesOf (target) == std::vector<Note> { { 60, 0, 960 } });
    CHECK (notesOf (source).empty());
}

TEST_CASE ("merge: an overlapping same-pitch note extends the existing one", "[merge][overlap]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S");
    auto target = addTrack (doc, "T");
    addNote (target, 60, 0, 480);
    SongDocument::getNotesNode (target).getChild (0).setProperty (SongIDs::onOrder, 5, nullptr);
    addNote (source, 60, 240, 480);

    const auto r = mergeSections (doc, { wholeTrack (source) }, idOf (target), true);

    CHECK (r.extended == 1);
    CHECK (notesOf (target) == std::vector<Note> { { 60, 0, 720 } });
    CHECK_FALSE (SongDocument::getNotesNode (target).getChild (0).hasProperty (SongIDs::onOrder));   // timing edited
}

TEST_CASE ("merge: touching same-pitch notes join", "[merge][overlap]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S");
    auto target = addTrack (doc, "T");
    addNote (target, 60, 0, 480);
    addNote (source, 60, 480, 480);

    mergeSections (doc, { wholeTrack (source) }, idOf (target), true);

    CHECK (notesOf (target) == std::vector<Note> { { 60, 0, 960 } });
}

TEST_CASE ("merge: a note bridging two existing notes collapses them into one", "[merge][overlap]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S");
    auto target = addTrack (doc, "T");
    addNote (target, 60, 0, 480);
    addNote (target, 60, 960, 480);
    addNote (source, 60, 240, 960);

    mergeSections (doc, { wholeTrack (source) }, idOf (target), true);

    CHECK (notesOf (target) == std::vector<Note> { { 60, 0, 1440 } });
    doc.undo();
    CHECK (notesOf (target) == std::vector<Note> { { 60, 0, 480 }, { 60, 960, 480 } });   // one undo step
}

TEST_CASE ("merge: notes of different pitch coexist as a chord", "[merge][overlap]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S");
    auto target = addTrack (doc, "T");
    addNote (target, 60, 0, 480);
    addNote (source, 64, 0, 480);

    mergeSections (doc, { wholeTrack (source) }, idOf (target), true);

    CHECK (notesOf (target) == std::vector<Note> { { 60, 0, 480 }, { 64, 0, 480 } });
}

TEST_CASE ("merge: the earliest-starting note's properties win", "[merge][overlap]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S");
    auto target = addTrack (doc, "T");
    addNote (target, 60, 240, 480, 90);
    addNote (source, 60, 0, 300, 50);   // starts earlier than the target's note

    mergeSections (doc, { wholeTrack (source) }, idOf (target), true);

    REQUIRE (notesOf (target) == std::vector<Note> { { 60, 0, 720 } });
    CHECK ((int) SongDocument::getNotesNode (target).getChild (0).getProperty (SongIDs::velocity) == 50);

    SongDocument doc2;
    auto source2 = addTrack (doc2, "S");
    auto target2 = addTrack (doc2, "T");
    addNote (target2, 60, 0, 480, 90);
    addNote (source2, 60, 240, 480, 50);   // starts later: the existing note keeps its velocity

    mergeSections (doc2, { wholeTrack (source2) }, idOf (target2), true);

    REQUIRE (notesOf (target2) == std::vector<Note> { { 60, 0, 720 } });
    CHECK ((int) SongDocument::getNotesNode (target2).getChild (0).getProperty (SongIDs::velocity) == 90);
}

TEST_CASE ("merge: overlaps that already exist in the target are left alone", "[merge][overlap]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S");
    auto target = addTrack (doc, "T");
    addNote (target, 60, 0, 960);
    addNote (target, 60, 480, 960);   // already overlaps the first
    addNote (source, 62, 0, 480);

    mergeSections (doc, { wholeTrack (source) }, idOf (target), true);

    CHECK (notesOf (target) == std::vector<Note> { { 60, 0, 960 }, { 60, 480, 960 }, { 62, 0, 480 } });
}

TEST_CASE ("merge: carried notes that collide with each other are joined too", "[merge][overlap]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S");
    auto target = addTrack (doc, "T");
    addNote (source, 60, 0, 480);
    addNote (source, 60, 240, 480);   // a stacked duplicate in the same source

    const auto r = mergeSections (doc, { wholeTrack (source) }, idOf (target), false);

    CHECK (notesOf (target) == std::vector<Note> { { 60, 0, 720 } });
    CHECK (r.inserted == 1);
    CHECK (r.extended == 1);
    CHECK (notesOf (source).empty());
}

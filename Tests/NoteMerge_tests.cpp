#include "PlaybackTestSupport.h"
#include "UI/MergeScope.h"
#include "UI/NoteMerge.h"
#include "UI/ProgramChanges.h"
#include "UI/SectionEdit.h"
#include "UI/SongDocument.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>

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

TEST_CASE ("merge: a section named twice (as 0 and by its id) is carried once", "[merge]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S");
    auto target = addTrack (doc, "T");
    addNote (source, 60, 0, 480);
    addNote (source, 62, 480, 480);
    splitAt (doc, { idOf (source) }, 480);
    const auto first = sectionsOf (source)[0];

    const auto r = mergeSections (doc, { { idOf (source), 0 }, { idOf (source), first.id } }, idOf (target), true);

    CHECK (r.inserted == 1);
    CHECK (r.dropped == 0);
    CHECK (notesOf (target) == std::vector<Note> { { 60, 0, 480 } });
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

namespace
{
    std::vector<std::vector<std::uint8_t>> eventBytesOf (const juce::ValueTree& track)
    {
        std::vector<std::vector<std::uint8_t>> out;
        const auto events = SongDocument::getEventsNode (track);
        for (int i = 0; i < events.getNumChildren(); ++i)
        {
            const auto* b = events.getChild (i).getProperty (SongIDs::data).getBinaryData();
            const auto* d = static_cast<const std::uint8_t*> (b->getData());
            out.emplace_back (d, d + b->getSize());
        }
        return out;
    }
}

TEST_CASE ("merge all events: events inside the section travel, others stay", "[merge][events]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S", 1);
    auto target = addTrack (doc, "T", 3);
    target.setProperty (SongIDs::defaultChannel, 3, nullptr);
    addNote (source, 60, 0, 480);
    addNote (source, 62, 960, 480);
    addEvent (source, 0, { 0xB0, 7, 100 });      // in the first section
    addEvent (source, 960, { 0xC0, 40 });        // at the second section's start
    addEvent (source, 1200, { 0xE0, 0, 64 });    // in the second section
    splitAt (doc, { idOf (source) }, 960);
    const auto second = sectionsOf (source)[1];

    const auto r = mergeSections (doc, { { idOf (source), second.id } }, idOf (target), false, MergeScope::allEvents);

    CHECK (r.changed);
    CHECK (r.eventsCarried == 2);
    // Channel rewritten to the target's (3 -> nibble 2).
    CHECK (eventBytesOf (target) == std::vector<std::vector<std::uint8_t>> { { 0xC2, 40 }, { 0xE2, 0, 64 } });
    CHECK (eventBytesOf (source) == std::vector<std::vector<std::uint8_t>> { { 0xB0, 7, 100 } });
}

TEST_CASE ("merge all events: nothing is synthesized for an instrument set before the section", "[merge][events]")
{
    SongDocument doc;
    auto source = addTrack (doc, "S", 1);
    auto target = addTrack (doc, "T", 1);
    addEvent (source, 0, { 0xC0, 73 });          // before the section
    addNote (source, 60, 0, 480);
    addNote (source, 62, 960, 480);
    splitAt (doc, { idOf (source) }, 960);
    const auto second = sectionsOf (source)[1];

    mergeSections (doc, { { idOf (source), second.id } }, idOf (target), true, MergeScope::allEvents);

    CHECK (programChangesOf (target).empty());
}

TEST_CASE ("merge all events: the section end is exclusive and track-name/end-of-track metas never travel", "[merge][events]")
{
    SongDocument doc;
    auto source = addTrack (doc);
    auto target = addTrack (doc);
    addNote (source, 60, 0, 480);
    addNote (source, 62, 960, 480);
    splitAt (doc, { idOf (source) }, 960);
    const auto first = sectionsOf (source)[0];   // [0, 960)
    addEvent (source, 959, { 0xB0, 10, 64 });    // last tick inside
    addEvent (source, 960, { 0xB0, 11, 64 });    // at the end: excluded
    addEvent (source, 0, { 0xFF, 0x03, 0x01, 'x' });   // track name
    addEvent (source, 500, { 0xFF, 0x2F, 0x00 });      // end of track

    const auto r = mergeSections (doc, { { idOf (source), first.id } }, idOf (target), true, MergeScope::allEvents);

    CHECK (r.eventsCarried == 1);
    CHECK (eventBytesOf (target) == std::vector<std::vector<std::uint8_t>> { { 0xB0, 10, 64 } });
}

TEST_CASE ("merge all events: tempo and meter events never travel", "[merge][events][tempo-sync]")
{
    SongDocument doc;
    auto source = addTrack (doc);
    auto target = addTrack (doc);
    addNote (source, 60, 0, 480);
    addNote (source, 62, 960, 480);
    splitAt (doc, { idOf (source) }, 960);
    const auto first = sectionsOf (source)[0];
    addEvent (source, 100, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 });
    addEvent (source, 200, { 0xFF, 0x58, 0x04, 0x03, 0x02, 0x18, 0x08 });
    addEvent (source, 300, { 0xB0, 10, 64 });

    const auto r = mergeSections (doc, { { idOf (source), first.id } }, idOf (target), true, MergeScope::allEvents);

    CHECK (r.eventsCarried == 1);
    CHECK (eventBytesOf (target) == std::vector<std::vector<std::uint8_t>> { { 0xB0, 10, 64 } });
}

TEST_CASE ("merge all events: a copy keeps the source's events and a move undoes in one step", "[merge][events]")
{
    SongDocument doc;
    auto source = addTrack (doc);
    auto target = addTrack (doc);
    addNote (source, 60, 0, 480);
    addEvent (source, 100, { 0xB0, 7, 90 });

    mergeSections (doc, { wholeTrack (source) }, idOf (target), true, MergeScope::allEvents);
    CHECK (eventBytesOf (source).size() == 1);
    CHECK (eventBytesOf (target).size() == 1);
    doc.undo();
    CHECK (eventBytesOf (target).empty());

    mergeSections (doc, { wholeTrack (source) }, idOf (target), false, MergeScope::allEvents);
    CHECK (eventBytesOf (source).empty());
    CHECK (eventBytesOf (target).size() == 1);
    doc.undo();
    CHECK (eventBytesOf (source).size() == 1);
    CHECK (eventBytesOf (target).empty());
}

TEST_CASE ("merge all events: events alone make the merge a change even when every note is dropped", "[merge][events]")
{
    SongDocument doc;
    auto source = addTrack (doc);
    auto target = addTrack (doc);
    addNote (source, 60, 0, 480);
    addNote (target, 60, 0, 480);                // the carried note lies inside this one
    addEvent (source, 100, { 0xB0, 7, 90 });

    const auto r = mergeSections (doc, { wholeTrack (source) }, idOf (target), true, MergeScope::allEvents);

    CHECK (r.dropped == 1);
    CHECK (r.changed);
    CHECK (eventBytesOf (target).size() == 1);
}

TEST_CASE ("merge notes only: events never travel", "[merge][events]")
{
    SongDocument doc;
    auto source = addTrack (doc);
    auto target = addTrack (doc);
    addNote (source, 60, 0, 480);
    addEvent (source, 100, { 0xB0, 7, 90 });

    const auto r = mergeSections (doc, { wholeTrack (source) }, idOf (target), false);

    CHECK (r.eventsCarried == 0);
    CHECK (eventBytesOf (target).empty());
    CHECK (eventBytesOf (source).size() == 1);
}

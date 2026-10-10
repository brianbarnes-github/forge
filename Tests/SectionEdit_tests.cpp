#include "PlaybackTestSupport.h"
#include "UI/MidiExport.h"
#include "UI/PreviewNoteDiff.h"
#include "UI/PreviewPipeline.h"
#include "UI/Playback/PlaybackSnapshot.h"
#include "UI/SectionEdit.h"
#include "UI/SongDocument.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace lotro;
using namespace lotro::playbacktest;

namespace
{
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

TEST_CASE ("sections: a track with notes and no stored sections reads as one virtual section", "[sections]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 480, 480);
    addNote (t, 62, 960, 960);

    const auto sections = sectionsOf (t);
    REQUIRE (sections.size() == 1);
    CHECK (sections[0].id == 0);
    CHECK (sections[0].startTick == 0);
    CHECK (sections[0].endTick == 1920);
}

TEST_CASE ("sections: an empty track and the conductor have no sections", "[sections]")
{
    SongDocument doc;
    CHECK (sectionsOf (addTrack (doc)).empty());
    CHECK (sectionsOf (doc.getConductorTrack()).empty());
}

TEST_CASE ("sections: stored sections are returned in stored order and ignore the virtual rule", "[sections]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    auto s1 = addStoredSection (doc, t, 0, 480);
    auto s2 = addStoredSection (doc, t, 480, 960);

    const auto sections = sectionsOf (t);
    REQUIRE (sections.size() == 2);
    CHECK (sections[0].id == (juce::int64) s1.getProperty (SongIDs::sectionId));
    CHECK (sections[1].endTick == 960);
    CHECK (sections[1].id == (juce::int64) s2.getProperty (SongIDs::sectionId));
}

TEST_CASE ("sections: a note's section is its tag, else the nearest section", "[sections]")
{
    const std::vector<SectionRange> sections { { 5, 0, 480 }, { 6, 480, 960 } };

    auto note = [] (int start, juce::int64 tag)
    {
        juce::ValueTree n (SongIDs::NOTE);
        n.setProperty (SongIDs::startTick, start, nullptr);
        if (tag != 0)
            n.setProperty (SongIDs::sectionId, tag, nullptr);
        return n;
    };

    CHECK (sectionIdOfNote (note (0, 6), sections) == 6);     // tag wins over position
    CHECK (sectionIdOfNote (note (500, 0), sections) == 6);   // untagged: by position
    CHECK (sectionIdOfNote (note (500, 99), sections) == 6);  // dangling tag: by position
    CHECK (sectionIdOfNote (note (5000, 0), sections) == 6);  // outside everything: the nearest (was: the first stored)
    CHECK (sectionIdOfNote (note (-30, 0), sections) == 5);
    CHECK (sectionIdOfNote (note (480, 0), sections) == 6);   // end is exclusive: 480 is inside the second
    CHECK (sectionIdOfNote (note (0, 0), {}) == 0);

    // In a gap the nearer section wins whatever the stored order; a tie goes to the earlier start.
    // Distance counts the exclusive end as one past the last tick, so abutting sections never tie a note inside one.
    const std::vector<SectionRange> gapped { { 7, 3000, 4000 }, { 8, 0, 1000 }, { 9, 1501, 2000 } };
    CHECK (sectionIdOfNote (note (1400, 0), gapped) == 9);
    CHECK (sectionIdOfNote (note (1250, 0), gapped) == 8);   // 251 from each: the earlier start wins
    CHECK (sectionIdOfNote (note (2700, 0), gapped) == 7);
    CHECK (sectionIdOfNote (note (1000, 0), { { 1, 0, 1000 }, { 2, 1000, 2000 } }) == 2);   // abutting: inside the second
}

TEST_CASE ("sections: hit testing finds edges within the slop, then the narrowest body", "[sections]")
{
    const std::vector<SectionRange> sections { { 1, 0, 1000 }, { 2, 400, 600 } };
    constexpr double ppt = 0.1;   // 5 px of slop = 50 ticks

    CHECK (hitTestSection (sections, 20, ppt, 5).zone == SectionZone::LeftEdge);
    CHECK (hitTestSection (sections, 20, ppt, 5).sectionId == 1);
    CHECK (hitTestSection (sections, 980, ppt, 5).zone == SectionZone::RightEdge);
    CHECK (hitTestSection (sections, 500, ppt, 5).sectionId == 2);   // narrowest body
    CHECK (hitTestSection (sections, 500, ppt, 5).zone == SectionZone::Body);
    CHECK (hitTestSection (sections, 200, ppt, 5).sectionId == 1);
    CHECK (hitTestSection (sections, 1500, ppt, 5).zone == SectionZone::None);
    CHECK (hitTestSection ({}, 10, ppt, 5).zone == SectionZone::None);
}

TEST_CASE ("sections: at a boundary shared by two sections the pointer side picks the edge", "[sections]")
{
    const std::vector<SectionRange> sections { { 1, 0, 100 }, { 2, 100, 200 } };
    constexpr double ppt = 0.1;   // 5 px of slop = 50 ticks

    CHECK (hitTestSection (sections, 90, ppt, 5).sectionId == 1);
    CHECK (hitTestSection (sections, 90, ppt, 5).zone == SectionZone::RightEdge);
    CHECK (hitTestSection (sections, 99, ppt, 5).sectionId == 1);
    CHECK (hitTestSection (sections, 100, ppt, 5).sectionId == 2);
    CHECK (hitTestSection (sections, 100, ppt, 5).zone == SectionZone::LeftEdge);
    CHECK (hitTestSection (sections, 130, ppt, 5).sectionId == 2);
    CHECK (hitTestSection (sections, 130, ppt, 5).zone == SectionZone::LeftEdge);
}

TEST_CASE ("sections: the nearest edge wins and an edge is only offered on its own side", "[sections]")
{
    constexpr double ppt = 0.1;

    const std::vector<SectionRange> nested { { 1, 0, 1000 }, { 2, 30, 500 } };
    const auto near = hitTestSection (nested, 20, ppt, 5);   // 20 from 1's start, 10 from 2's
    CHECK (near.sectionId == 2);
    CHECK (near.zone == SectionZone::LeftEdge);

    const std::vector<SectionRange> narrow { { 7, 100, 110 } };
    CHECK (hitTestSection (narrow, 60, ppt, 5).zone == SectionZone::LeftEdge);    // 40 ticks before
    CHECK (hitTestSection (narrow, 150, ppt, 5).zone == SectionZone::RightEdge);  // 40 ticks after
    CHECK (hitTestSection (narrow, 40, ppt, 5).zone == SectionZone::None);        // beyond the slop
}

namespace
{
    struct N { int pitch; int start; int dur; juce::int64 section; };

    std::vector<N> notesOf (const juce::ValueTree& track)
    {
        std::vector<N> out;
        const auto notes = SongDocument::getNotesNode (track);
        for (int i = 0; i < notes.getNumChildren(); ++i)
        {
            const auto n = notes.getChild (i);
            out.push_back ({ (int) n.getProperty (SongIDs::pitch), (int) n.getProperty (SongIDs::startTick),
                             (int) n.getProperty (SongIDs::durationTicks), (juce::int64) n.getProperty (SongIDs::sectionId, 0) });
        }
        std::sort (out.begin(), out.end(), [] (const N& a, const N& b) { return a.start < b.start; });
        return out;
    }

    juce::int64 idOf (const juce::ValueTree& t) { return (juce::int64) t.getProperty (SongIDs::trackId); }
}

TEST_CASE ("splitAt: divides a section and moves the later notes to the new one", "[sections][split]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    addNote (t, 62, 960, 480);

    splitAt (doc, { idOf (t) }, 720);

    const auto sections = sectionsOf (t);
    REQUIRE (sections.size() == 2);
    CHECK (sections[0].startTick == 0);
    CHECK (sections[0].endTick == 720);
    CHECK (sections[1].startTick == 720);
    CHECK (sections[1].endTick == 1440);
    CHECK (sections[0].id != 0);

    const auto notes = notesOf (t);
    REQUIRE (notes.size() == 2);
    CHECK (notes[0].section == sections[0].id);
    CHECK (notes[1].section == sections[1].id);
}

TEST_CASE ("splitAt: a note straddling the tick is cut in two with the same provenance", "[sections][split]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 960, 90, 3);
    auto note = SongDocument::getNotesNode (t).getChild (0);
    note.setProperty (SongIDs::sourceTrackIndex, 2, nullptr);
    note.setProperty (SongIDs::sourceEventIndex, 7, nullptr);
    note.setProperty (SongIDs::onOrder, 4, nullptr);
    note.setProperty (SongIDs::offOrder, 5, nullptr);

    splitAt (doc, { idOf (t) }, 400);

    const auto notes = notesOf (t);
    REQUIRE (notes.size() == 2);
    CHECK (notes[0].start == 0);
    CHECK (notes[0].dur == 400);
    CHECK (notes[1].start == 400);
    CHECK (notes[1].dur == 560);
    CHECK (notes[0].pitch == 60);
    CHECK (notes[1].pitch == 60);
    CHECK (notes[0].section != notes[1].section);

    const auto notesNode = SongDocument::getNotesNode (t);
    for (int i = 0; i < 2; ++i)
    {
        const auto n = notesNode.getChild (i);
        CHECK ((int) n.getProperty (SongIDs::velocity) == 90);
        CHECK ((int) n.getProperty (SongIDs::channel) == 3);
        CHECK ((int) n.getProperty (SongIDs::sourceTrackIndex) == 2);   // both halves keep the original pair
        CHECK ((int) n.getProperty (SongIDs::sourceEventIndex) == 7);
        CHECK_FALSE (n.hasProperty (SongIDs::onOrder));
        CHECK_FALSE (n.hasProperty (SongIDs::offOrder));
    }
}

TEST_CASE ("splitAt: the cut tail is not marked offSynthesized even when the original was", "[sections][split]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 960);
    auto note = SongDocument::getNotesNode (t).getChild (0);
    note.setProperty (SongIDs::offSynthesized, true, nullptr);
    note.setProperty (SongIDs::onOrder, 4, nullptr);
    note.setProperty (SongIDs::offOrder, 5, nullptr);

    splitAt (doc, { idOf (t) }, 480);

    const auto notesNode = SongDocument::getNotesNode (t);
    REQUIRE (notesNode.getNumChildren() == 2);
    for (int i = 0; i < 2; ++i)
    {
        const auto n = notesNode.getChild (i);
        CHECK_FALSE ((bool) n.getProperty (SongIDs::offSynthesized, false));
        CHECK_FALSE (n.hasProperty (SongIDs::onOrder));
        CHECK_FALSE (n.hasProperty (SongIDs::offOrder));
    }
}

TEST_CASE ("splitAt: a cut note reaches the preview pipeline as two Normal attacks", "[sections][split][previewpipeline]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 960);
    auto note = SongDocument::getNotesNode (t).getChild (0);
    note.setProperty (SongIDs::sourceTrackIndex, 0, nullptr);
    note.setProperty (SongIDs::sourceEventIndex, 0, nullptr);
    auto part = doc.addPart ("LuteOfAges", "Lead");
    doc.addAssignment (part, idOf (t), 0, 0, "octaveShift");
    const auto partId = (juce::int64) part.getProperty (SongIDs::partId);

    splitAt (doc, { idOf (t) }, 480);

    const auto result = computePartPreview (doc, partId);
    REQUIRE (result.pipelined.tracks.size() == 1);
    auto piped = result.pipelined.tracks[0].notes;
    REQUIRE (piped.size() == 2);
    std::sort (piped.begin(), piped.end(), [] (const Note& l, const Note& r) { return l.startTick < r.startTick; });
    CHECK (piped[0].startTick == 0);
    CHECK (piped[0].durationTicks == 480);
    CHECK (piped[1].startTick == 480);
    CHECK (piped[1].durationTicks == 480);

    const auto diff = diffPreviewNotes (result);
    REQUIRE (diff.size() == 2);
    for (const auto& n : diff)
        CHECK (n.state == NoteState::Normal);
}

TEST_CASE ("splitAt: a tick on a note's start or end cuts nothing and makes no zero-length note", "[sections][split]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    addNote (t, 62, 480, 480);

    splitAt (doc, { idOf (t) }, 480);

    const auto notes = notesOf (t);
    REQUIRE (notes.size() == 2);
    CHECK (notes[0].dur == 480);
    CHECK (notes[1].dur == 480);
    CHECK (notes[0].section != notes[1].section);
}

TEST_CASE ("splitAt: on an edge, outside every section, on an empty track or with no tracks does nothing", "[sections][split]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    auto empty = addTrack (doc, "E");
    addNote (t, 60, 0, 960);

    splitAt (doc, { idOf (t) }, 0);
    splitAt (doc, { idOf (t) }, 960);
    splitAt (doc, { idOf (t) }, 5000);
    splitAt (doc, { idOf (empty) }, 100);
    splitAt (doc, {}, 100);

    CHECK (sectionsOf (t).size() == 1);
    CHECK (sectionsOf (empty).empty());
    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("splitAt: applies to every named track in one undo step", "[sections][split]")
{
    SongDocument doc;
    auto a = addTrack (doc, "A");
    auto b = addTrack (doc, "B");
    addNote (a, 60, 0, 960);
    addNote (b, 64, 0, 960);

    splitAt (doc, { idOf (a), idOf (b) }, 480);
    CHECK (sectionsOf (a).size() == 2);
    CHECK (sectionsOf (b).size() == 2);

    doc.undo();
    CHECK (notesOf (a).size() == 1);     // the cut note is whole again
    CHECK (notesOf (b).size() == 1);
    CHECK (notesOf (a)[0].dur == 960);
    CHECK (sectionsOf (a).size() == 1);
    CHECK (sectionsOf (b).size() == 1);
    CHECK_FALSE (doc.canUndo());          // exactly one transaction was recorded
}

TEST_CASE ("splitAt: overlapping sections are both split by a tick inside both", "[sections][split]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    splitAt (doc, { idOf (t) }, 240);            // [0,240) [240,480)
    const auto first = sectionsOf (t);
    moveSections (doc, { { idOf (t), first[1].id } }, -120);   // second now [120,360): overlaps the first

    splitAt (doc, { idOf (t) }, 200);            // 200 is inside both [0,240) and [120,360)
    CHECK (sectionsOf (t).size() == 4);
}

TEST_CASE ("moveSections: shifts the range and its notes, leaving other sections alone", "[sections][move]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    addNote (t, 62, 960, 480);
    splitAt (doc, { idOf (t) }, 720);
    const auto sections = sectionsOf (t);

    moveSections (doc, { { idOf (t), sections[1].id } }, 480);

    const auto moved = sectionsOf (t);
    CHECK (moved[0].startTick == 0);
    CHECK (moved[1].startTick == 1200);
    CHECK (moved[1].endTick == 1920);
    const auto notes = notesOf (t);
    CHECK (notes[0].start == 0);
    CHECK (notes[1].start == 1440);
}

TEST_CASE ("moveSections: overlap keeps every note and the sections drag apart again", "[sections][move]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    addNote (t, 62, 960, 480);
    splitAt (doc, { idOf (t) }, 720);
    const auto s = sectionsOf (t);

    moveSections (doc, { { idOf (t), s[1].id } }, -720);   // onto the first section
    CHECK (notesOf (t).size() == 2);
    CHECK (notesOf (t)[1].start == 240);

    moveSections (doc, { { idOf (t), s[1].id } }, 720);     // and back out
    CHECK (notesOf (t)[1].start == 960);
}

TEST_CASE ("moveSections: clamps so no section starts before tick 0, moving companions together", "[sections][move]")
{
    SongDocument doc;
    auto a = addTrack (doc, "A");
    auto b = addTrack (doc, "B");
    addNote (a, 60, 100, 200);
    addNote (b, 64, 400, 200);
    splitAt (doc, { idOf (a), idOf (b) }, 50);   // each track: [0,50) holds no notes, the second section starts at 50
    const auto sa = sectionsOf (a);
    const auto sb = sectionsOf (b);

    // Move A's second section (start 50) and B's second (start 50) left by 500: clamps to -50.
    moveSections (doc, { { idOf (a), sa[1].id }, { idOf (b), sb[1].id } }, -500);

    CHECK (sectionsOf (a)[1].startTick == 0);
    CHECK (sectionsOf (b)[1].startTick == 0);
    CHECK (notesOf (a)[0].start == 50);    // 100 - 50
    CHECK (notesOf (b)[0].start == 350);   // 400 - 50: the same delta for both
}

TEST_CASE ("moveSections: a zero or fully clamped delta changes nothing and records no undo step", "[sections][move]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    splitAt (doc, { idOf (t) }, 240);
    doc.getUndoManager().clearUndoHistory();
    const auto s = sectionsOf (t);

    moveSections (doc, { { idOf (t), s[0].id } }, 0);
    moveSections (doc, { { idOf (t), s[0].id } }, -100);   // already at 0
    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("resizeSections: shrinking the right edge deletes later notes and trims a crossing note", "[sections][resize]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 300);
    addNote (t, 62, 400, 400);
    addNote (t, 64, 900, 100);
    const auto id = idOf (t);
    const auto sec = sectionsOf (t)[0];   // virtual id 0: resolved after materialising

    resizeSections (doc, { { id, sec.id } }, SectionEdge::Right, 600);

    CHECK (sectionsOf (t)[0].endTick == 600);
    const auto notes = notesOf (t);
    REQUIRE (notes.size() == 2);
    CHECK (notes[0].start == 0);
    CHECK (notes[0].dur == 300);
    CHECK (notes[1].start == 400);
    CHECK (notes[1].dur == 200);   // trimmed at 600
}

TEST_CASE ("resizeSections: shrinking the left edge deletes earlier notes and trims a crossing note", "[sections][resize]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 300);
    addNote (t, 62, 400, 400);
    addNote (t, 64, 900, 100);
    const auto id = idOf (t);
    const auto sec = sectionsOf (t)[0];   // virtual id 0: resolved after materialising

    resizeSections (doc, { { id, sec.id } }, SectionEdge::Left, 600);

    CHECK (sectionsOf (t)[0].startTick == 600);
    const auto notes = notesOf (t);
    REQUIRE (notes.size() == 2);
    CHECK (notes[0].start == 600);   // 400..800 trimmed to 600..800
    CHECK (notes[0].dur == 200);
    CHECK (notes[1].start == 900);
}

TEST_CASE ("resizeSections: growing only extends the range and keeps at least one tick", "[sections][resize]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    const auto id = idOf (t);
    const auto sec = sectionsOf (t)[0];

    resizeSections (doc, { { id, sec.id } }, SectionEdge::Right, 2000);
    CHECK (sectionsOf (t)[0].endTick == 2000);
    CHECK (notesOf (t).size() == 1);

    resizeSections (doc, { { id, sectionsOf (t)[0].id } }, SectionEdge::Right, -50);   // before the start
    CHECK (sectionsOf (t)[0].endTick == 1);   // start 0 + 1 tick
}

TEST_CASE ("deleteSections: removes the section and its notes only", "[sections][delete]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    addNote (t, 62, 960, 480);
    splitAt (doc, { idOf (t) }, 720);
    const auto s = sectionsOf (t);

    deleteSections (doc, { { idOf (t), s[1].id } });

    REQUIRE (sectionsOf (t).size() == 1);
    CHECK (sectionsOf (t)[0].id == s[0].id);
    REQUIRE (notesOf (t).size() == 1);
    CHECK (notesOf (t)[0].pitch == 60);

    doc.undo();
    CHECK (sectionsOf (t).size() == 2);
    CHECK (notesOf (t).size() == 2);
}

TEST_CASE ("deleteSections: deleting every section leaves an empty track with nothing to edit", "[sections][delete]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    deleteSections (doc, { { idOf (t), sectionsOf (t)[0].id } });

    CHECK (notesOf (t).empty());
    CHECK (sectionsOf (t).empty());
}

TEST_CASE ("sections: operations ignore the conductor, unknown tracks and unknown sections", "[sections]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    const auto conductor = (juce::int64) doc.getConductorTrack().getProperty (SongIDs::trackId);

    splitAt (doc, { conductor, 9999 }, 100);
    moveSections (doc, { { conductor, 1 }, { 9999, 1 }, { idOf (t), 424242 } }, 10);
    deleteSections (doc, { { conductor, 1 }, { idOf (t), 424242 } });

    CHECK (notesOf (t).size() == 1);
    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("sections: an edited note loses its raw-MIDI ordering and synthesized flag", "[sections][move]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    auto n = SongDocument::getNotesNode (t).getChild (0);
    n.setProperty (SongIDs::onOrder, 1, nullptr);
    n.setProperty (SongIDs::offOrder, 2, nullptr);
    n.setProperty (SongIDs::offSynthesized, true, nullptr);

    moveSections (doc, { { idOf (t), sectionsOf (t)[0].id } }, 10);

    CHECK_FALSE (n.hasProperty (SongIDs::onOrder));
    CHECK_FALSE (n.hasProperty (SongIDs::offOrder));
    CHECK_FALSE ((bool) n.getProperty (SongIDs::offSynthesized));
}

TEST_CASE ("splitAt: one undo restores the sections, the tags and the single uncut note", "[sections][split]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 960);
    addNote (t, 62, 600, 120);
    auto whole = SongDocument::getNotesNode (t).getChild (0);
    whole.setProperty (SongIDs::onOrder, 4, nullptr);
    whole.setProperty (SongIDs::offOrder, 5, nullptr);

    splitAt (doc, { idOf (t) }, 400);
    REQUIRE (notesOf (t).size() == 3);

    doc.undo();
    const auto sections = sectionsOf (t);
    REQUIRE (sections.size() == 1);
    CHECK (sections[0].startTick == 0);
    CHECK (sections[0].endTick == 960);
    const auto notes = notesOf (t);
    REQUIRE (notes.size() == 2);
    CHECK (notes[0].dur == 960);
    CHECK (notes[0].section == sections[0].id);   // tags point at the surviving section again
    CHECK (notes[1].section == sections[0].id);
    CHECK ((int) whole.getProperty (SongIDs::onOrder) == 4);
    CHECK ((int) whole.getProperty (SongIDs::offOrder) == 5);
    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("moveSections: the clamp also keeps a note tagged to a later section from going below 0", "[sections][move]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 960);
    splitAt (doc, { idOf (t) }, 480);
    const auto s = sectionsOf (t);
    deleteSections (doc, { { idOf (t), s[0].id } });   // only [480,960) is left
    addNote (t, 64, 100, 50);                           // a note drawn before every section: belongs to the first

    moveSections (doc, { { idOf (t), s[1].id } }, -480);

    const auto notes = notesOf (t);
    REQUIRE (notes.size() == 2);
    CHECK (notes[0].start == 0);                  // 100 - 100
    CHECK (notes[1].start == 380);                // 480 - 100
    CHECK (sectionsOf (t)[0].startTick == 380);
}

TEST_CASE ("sections: a note drawn in a gap is not carried away by a distant section", "[sections]")
{
    // Stored order A[0,1000), B[2000,2480), C[1000,2000); C is deleted, leaving a gap [1000,2000).
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 2480);
    splitAt (doc, { idOf (t) }, 2000);
    splitAt (doc, { idOf (t) }, 1000);
    const auto s = sectionsOf (t);
    REQUIRE (s.size() == 3);
    deleteSections (doc, { { idOf (t), s[2].id } });
    addNote (t, 64, 1900, 50);   // untagged, in the gap, right next to B

    SECTION ("deleting the far first section leaves it alone")
    {
        deleteSections (doc, { { idOf (t), s[0].id } });
        bool found = false;
        for (const auto& n : notesOf (t))
            found = found || n.start == 1900;
        CHECK (found);
    }

    SECTION ("moving the far first section does not move it")
    {
        moveSections (doc, { { idOf (t), s[0].id } }, 1000);
        bool found = false;
        for (const auto& n : notesOf (t))
            found = found || n.start == 1900;
        CHECK (found);
    }
}

namespace
{
    // (tick, status * 1000 + pitch) of every message EVENTS still holds, in stored order.
    std::vector<std::pair<int, int>> eventsLeft (const juce::ValueTree& track)
    {
        std::vector<std::pair<int, int>> out;
        for (auto e : SongDocument::getEventsNode (track))
        {
            const auto* block = e.getProperty (SongIDs::data).getBinaryData();
            const auto* bytes = static_cast<const std::uint8_t*> (block->getData());
            out.emplace_back ((int) e.getProperty (SongIDs::tick), bytes[0] * 1000 + bytes[1]);
        }
        return out;
    }
}

TEST_CASE ("resizeSections: shrinking an edge takes the stray note events in the band given up", "[sections][resize]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    addNote (t, 62, 960, 480);
    addNote (t, 64, 1920, 480);   // section [0, 2400)
    addEvent (t, 960, { 0x90, 62, 90 });    // stray duplicate of the note at 960
    addEvent (t, 1000, { 0xB0, 7, 100 });   // controller: stays
    addEvent (t, 1440, { 0x80, 62, 64 });
    addEvent (t, 100, { 0x90, 70, 90 });    // outside the band: stays, pair and all
    addEvent (t, 200, { 0x80, 70, 64 });
    const auto id = idOf (t);

    SECTION ("right edge")
    {
        resizeSections (doc, { { id, 0 } }, SectionEdge::Right, 900);   // gives up [900, 2400)
        CHECK (eventsLeft (t) == std::vector<std::pair<int, int>> { { 1000, 176007 }, { 100, 144070 }, { 200, 128070 } });
    }
    SECTION ("left edge")
    {
        resizeSections (doc, { { id, 0 } }, SectionEdge::Left, 1500);   // gives up [0, 1500)
        CHECK (eventsLeft (t) == std::vector<std::pair<int, int>> { { 1000, 176007 } });   // the pair at 100/200 was in the band too
    }
}

TEST_CASE ("moveSections: non-note events stay where they are", "[sections][move]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    addEvent (t, 240, { 0xB0, 7, 100 });

    moveSections (doc, { { idOf (t), sectionsOf (t)[0].id } }, 300);

    CHECK (notesOf (t)[0].start == 300);
    CHECK ((int) SongDocument::getEventsNode (t).getChild (0).getProperty (SongIDs::tick) == 240);
}

TEST_CASE ("deleteSections: stray note events inside the section go with it, other events stay", "[sections][delete]")
{
    // A stacked duplicate import leaves a note-on/off pair in EVENTS beside the Song note, so the
    // pair is audible after the note is deleted unless the delete takes it too.
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    addNote (t, 62, 960, 480);
    addEvent (t, 100, { 0x90, 64, 90 });    // stray on before the section: kept ...
    addEvent (t, 1000, { 0x80, 64, 64 });   // ... and so is its off, although that falls inside the section
    addEvent (t, 960, { 0x90, 62, 90 });    // stray pair duplicating the note at 960
    addEvent (t, 1000, { 0xB0, 7, 100 });   // controller: stays
    addEvent (t, 1440, { 0x80, 62, 64 });
    addEvent (t, 2000, { 0x90, 65, 90 });   // after the section: kept
    splitAt (doc, { idOf (t) }, 480);
    splitAt (doc, { idOf (t) }, 1920);

    deleteSections (doc, { { idOf (t), sectionsOf (t)[1].id } });

    std::vector<std::pair<int, int>> left;   // (tick, status+pitch) of what EVENTS still holds
    for (auto e : SongDocument::getEventsNode (t))
    {
        const auto* block = e.getProperty (SongIDs::data).getBinaryData();
        const auto* bytes = static_cast<const std::uint8_t*> (block->getData());
        left.emplace_back ((int) e.getProperty (SongIDs::tick), bytes[0] * 1000 + bytes[1]);
    }
    CHECK (left == std::vector<std::pair<int, int>> { { 100, 144064 }, { 1000, 128064 }, { 1000, 176007 }, { 2000, 144065 } });
}

TEST_CASE ("moveSections: stray note pairs whose on lies in the section move with it, in one undo step", "[sections][move]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    addNote (t, 62, 960, 480);
    addEvent (t, 100, { 0x90, 70, 90 });    // stray pair in the first section: stays
    addEvent (t, 200, { 0x80, 70, 64 });
    addEvent (t, 960, { 0x90, 62, 90 });    // stray duplicate of the note at 960: moves, off included
    addEvent (t, 1000, { 0xB0, 7, 100 });   // controller: stays
    addEvent (t, 1440, { 0x80, 62, 64 });
    splitAt (doc, { idOf (t) }, 480);
    const auto before = eventsLeft (t);

    moveSections (doc, { { idOf (t), sectionsOf (t)[1].id } }, 960);

    CHECK (eventsLeft (t) == std::vector<std::pair<int, int>> { { 100, 144070 }, { 200, 128070 }, { 1920, 144062 }, { 1000, 176007 }, { 2400, 128062 } });

    doc.undo();
    CHECK (eventsLeft (t) == before);
}

TEST_CASE ("moveSections: a stray pair shifted into a neighbouring section's range moves only once", "[sections][move]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 100);
    addNote (t, 62, 480, 100);   // sections A [0, 480) and B [480, 580) once split
    addEvent (t, 100, { 0x90, 70, 90 });   // stray pair in A
    addEvent (t, 200, { 0x80, 70, 64 });
    addEvent (t, 500, { 0x90, 71, 90 });   // stray pair in B
    addEvent (t, 550, { 0x80, 71, 64 });
    splitAt (doc, { idOf (t) }, 480);
    const auto sections = sectionsOf (t);

    moveSections (doc, { { idOf (t), sections[0].id }, { idOf (t), sections[1].id } }, 400);

    // A's pair lands at 500/600, inside B's old range, but is still shifted once.
    CHECK (eventsLeft (t) == std::vector<std::pair<int, int>> { { 500, 144070 }, { 600, 128070 }, { 900, 144071 }, { 950, 128071 } });
}

TEST_CASE ("sections: a call that changes nothing does not materialise the track", "[sections]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);

    moveSections (doc, { { idOf (t), 0 } }, 0);
    resizeSections (doc, { { idOf (t), 0 } }, SectionEdge::Right, 480);
    splitAt (doc, { idOf (t) }, 480);

    CHECK (sectionsOf (t)[0].id == 0);   // still the virtual section
    CHECK_FALSE (t.getChildWithName (SongIDs::SECTIONS).isValid());

    auto empty = addTrack (doc, "E");
    moveSections (doc, { { idOf (empty), 0 } }, 100);
    resizeSections (doc, { { idOf (empty), 0 } }, SectionEdge::Left, 100);
    deleteSections (doc, { { idOf (empty), 0 } });
    moveSections (doc, {}, 100);
    resizeSections (doc, {}, SectionEdge::Right, 100);
    deleteSections (doc, {});
    CHECK_FALSE (empty.getChildWithName (SongIDs::SECTIONS).isValid());
    CHECK (notesOf (t).size() == 1);
    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("resizeSections: a left-edge resize trims only at the left edge, never the note's end", "[sections][resize]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 960);
    splitAt (doc, { idOf (t) }, 480);   // [0,480) holds 0..480, [480,960) holds 480..960
    const auto s = sectionsOf (t);
    auto left = SongDocument::getNotesNode (t).getChild (0);
    REQUIRE ((int) left.getProperty (SongIDs::startTick) == 0);
    doc.setProperty (left, SongIDs::durationTicks, 600);   // the note editor lengthened it past its section's end

    resizeSections (doc, { { idOf (t), s[0].id } }, SectionEdge::Left, 100);

    CHECK ((int) left.getProperty (SongIDs::startTick) == 100);
    CHECK ((int) left.getProperty (SongIDs::durationTicks) == 500);   // still ends at 600
}

TEST_CASE ("resizeSections: companions resize together in one undo step", "[sections][resize]")
{
    SongDocument doc;
    auto a = addTrack (doc, "A");
    auto b = addTrack (doc, "B");
    addNote (a, 60, 0, 960);
    addNote (b, 64, 0, 480);

    resizeSections (doc, { { idOf (a), 0 }, { idOf (b), 0 } }, SectionEdge::Right, 300);
    CHECK (sectionsOf (a)[0].endTick == 300);
    CHECK (sectionsOf (b)[0].endTick == 300);
    CHECK (notesOf (a)[0].dur == 300);
    CHECK (notesOf (b)[0].dur == 300);

    doc.undo();
    CHECK (notesOf (a)[0].dur == 960);
    CHECK (notesOf (b)[0].dur == 480);
    CHECK (sectionsOf (a)[0].endTick == 960);
    CHECK_FALSE (doc.canUndo());
}

namespace
{
    // A track whose only section is [480,960) and which holds a note at 100..150
    // drawn after the first section was deleted: untagged, it belongs to [480,960).
    juce::ValueTree trackWithMemberBeforeItsSection (SongDocument& doc)
    {
        auto t = addTrack (doc);
        addNote (t, 60, 0, 960);
        splitAt (doc, { idOf (t) }, 480);
        deleteSections (doc, { { idOf (t), sectionsOf (t)[0].id } });
        addNote (t, 64, 100, 50);
        return t;
    }

    // A track whose only section is [0,480) and which holds a note at 700..750
    // drawn in the gap after it: untagged, it belongs to [0,480).
    juce::ValueTree trackWithMemberAfterItsSection (SongDocument& doc)
    {
        auto t = addTrack (doc);
        addNote (t, 60, 0, 480);
        addNote (t, 62, 700, 50);
        splitAt (doc, { idOf (t) }, 480);
        deleteSections (doc, { { idOf (t), sectionsOf (t)[1].id } });
        addNote (t, 64, 700, 50);
        return t;
    }
}

TEST_CASE ("resizeSections: growing the left edge past a member note outside the section keeps it whole", "[sections][resize]")
{
    SongDocument doc;
    auto t = trackWithMemberBeforeItsSection (doc);
    REQUIRE (sectionsOf (t).size() == 1);

    resizeSections (doc, { { idOf (t), sectionsOf (t)[0].id } }, SectionEdge::Left, 300);   // grows 480 -> 300

    CHECK (sectionsOf (t)[0].startTick == 300);
    const auto notes = notesOf (t);
    REQUIRE (notes.size() == 2);
    CHECK (notes[0].start == 100);
    CHECK (notes[0].dur == 50);
    CHECK (notes[1].start == 480);
}

TEST_CASE ("resizeSections: growing the left edge to cross an outside member note does not trim it", "[sections][resize]")
{
    SongDocument doc;
    auto t = trackWithMemberBeforeItsSection (doc);

    resizeSections (doc, { { idOf (t), sectionsOf (t)[0].id } }, SectionEdge::Left, 120);   // inside 100..150

    const auto notes = notesOf (t);
    REQUIRE (notes.size() == 2);
    CHECK (notes[0].start == 100);
    CHECK (notes[0].dur == 50);
}

TEST_CASE ("resizeSections: growing the right edge past a member note outside the section keeps it", "[sections][resize]")
{
    SongDocument doc;
    auto t = trackWithMemberAfterItsSection (doc);
    REQUIRE (sectionsOf (t).size() == 1);

    resizeSections (doc, { { idOf (t), sectionsOf (t)[0].id } }, SectionEdge::Right, 600);   // grows 480 -> 600

    CHECK (sectionsOf (t)[0].endTick == 600);
    const auto notes = notesOf (t);
    REQUIRE (notes.size() == 2);
    CHECK (notes[1].start == 700);
    CHECK (notes[1].dur == 50);
}

TEST_CASE ("resizeSections: shrinking only deletes notes in the band given up", "[sections][resize]")
{
    SongDocument doc;
    auto t = trackWithMemberAfterItsSection (doc);
    addNote (t, 65, 350, 50);   // inside [0,480), in the band given up

    resizeSections (doc, { { idOf (t), sectionsOf (t)[0].id } }, SectionEdge::Right, 300);

    const auto notes = notesOf (t);
    REQUIRE (notes.size() == 2);
    CHECK (notes[0].dur == 300);    // 0..480 trimmed at the new edge
    CHECK (notes[1].start == 700);  // the gap note was never inside the section: kept
}

TEST_CASE ("sections: a track whose referenced section is unknown is not materialised", "[sections]")
{
    SongDocument doc;
    auto t = addTrack (doc, "A");
    auto u = addTrack (doc, "B");
    addNote (t, 60, 0, 480);
    addNote (u, 62, 0, 480);

    moveSections (doc, { { idOf (t), 0 }, { idOf (u), 424242 } }, 10);

    CHECK (notesOf (t)[0].start == 10);
    CHECK (notesOf (u)[0].start == 0);
    CHECK_FALSE (u.getChildWithName (SongIDs::SECTIONS).isValid());
}

TEST_CASE ("sections: a stale id that materialising would mint does not join the edit", "[sections]")
{
    SongDocument doc;
    auto t = addTrack (doc, "A");
    auto u = addTrack (doc, "B");
    addNote (t, 60, 0, 480);
    addNote (u, 62, 0, 480);

    // Section id 1 does not exist yet; materialising t first would mint it.
    moveSections (doc, { { idOf (t), 1 }, { idOf (u), 0 } }, 10);

    CHECK (notesOf (t)[0].start == 0);
    CHECK (notesOf (u)[0].start == 10);
}

TEST_CASE ("sections: playback hears a cut note as two attacks and a deleted section as silence", "[sections][integration]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 960);

    const auto countOns = [&]
    {
        const auto snap = buildSnapshot (doc);
        int ons = 0;
        for (const auto& e : snap->events())
            ons += e.kind == PlaybackEventKind::NoteOn ? 1 : 0;
        return ons;
    };

    splitAt (doc, { idOf (t) }, 480);
    CHECK (countOns() == 2);

    deleteSections (doc, { { idOf (t), sectionsOf (t)[1].id } });
    CHECK (countOns() == 1);
}

TEST_CASE ("sections: a moved section exports at its new position", "[sections][integration]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    splitAt (doc, { idOf (t) }, 240);
    moveSections (doc, { { idOf (t), sectionsOf (t)[1].id } }, 960);

    const auto file = buildRawMidiFile (doc);
    REQUIRE (file.tracks.size() >= 2);
    int latestOn = -1;
    for (const auto& e : file.tracks[1].events)
        if (! e.bytes.empty() && (e.bytes[0] & 0xF0) == 0x90 && e.bytes.size() > 2 && e.bytes[2] > 0)
            latestOn = std::max (latestOn, e.tick);
    CHECK (latestOn == 1200);   // the second half: 240 + 960
}

TEST_CASE ("resizeSectionsBy: each section's edge moves by the same delta, clamped per section", "[sections][resize]")
{
    SongDocument doc;
    auto a = addTrack (doc);
    auto b = addTrack (doc);
    addNote (a, 60, 0, 960);
    addNote (b, 60, 0, 1000);
    addNote (b, 62, 3000, 2000);   // B: [0, 5000)
    const std::vector<SectionRef> refs { { idOf (a), 0 }, { idOf (b), 0 } };

    resizeSectionsBy (doc, refs, SectionEdge::Right, 40);
    CHECK (sectionsOf (a)[0].endTick == 1000);
    CHECK (sectionsOf (b)[0].endTick == 5040);
    CHECK (notesOf (b).size() == 2);

    resizeSectionsBy (doc, { { idOf (a), sectionsOf (a)[0].id }, { idOf (b), sectionsOf (b)[0].id } }, SectionEdge::Right, -2000);
    CHECK (sectionsOf (a)[0].endTick == 1);      // clamped to one tick
    CHECK (sectionsOf (b)[0].endTick == 3040);   // moved by the full delta
    CHECK (notesOf (b).size() == 2);             // the 3000 note is trimmed, not deleted
    CHECK (notesOf (b)[1].dur == 40);

    doc.undo();   // one step per call
    CHECK (sectionsOf (a)[0].endTick == 1000);
    CHECK (sectionsOf (b)[0].endTick == 5040);
    CHECK (notesOf (b)[1].dur == 2000);
}

TEST_CASE ("resizeSectionsBy: a zero delta or one every section already clamps away changes nothing", "[sections][resize]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 960);
    resizeSectionsBy (doc, { { idOf (t), 0 } }, SectionEdge::Right, 0);
    resizeSectionsBy (doc, { { idOf (t), 0 } }, SectionEdge::Left, -100);   // already at 0
    CHECK (t.getChildWithName (SongIDs::SECTIONS).getNumChildren() == 0);   // not materialised
    CHECK_FALSE (doc.canUndo());
}

TEST_CASE ("sections: undoing delete, draw and split back to the start leaves just the original section", "[sections][undo]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 960);
    const auto original = sectionsOf (t);   // the track's one (virtual) section
    REQUIRE (original.size() == 1);

    // Materialise the original as a stored section, as any first edit does.
    splitAt (doc, { idOf (t) }, 480);
    doc.undo();
    REQUIRE (sectionsOf (t).size() == 1);
    const auto stored = sectionsOf (t);

    deleteSections (doc, { { idOf (t), stored[0].id } });
    REQUIRE (sectionsOf (t).empty());

    // The user draws a note on the now-empty track: undoable, like SourceRollEditor::createNoteAt.
    doc.getUndoManager().beginNewTransaction();
    juce::ValueTree drawn (SongIDs::NOTE);
    drawn.setProperty (SongIDs::pitch, 64, nullptr);
    drawn.setProperty (SongIDs::startTick, 0, nullptr);
    drawn.setProperty (SongIDs::durationTicks, 960, nullptr);
    doc.addChild (SongDocument::getNotesNode (t), drawn, false);

    splitAt (doc, { idOf (t) }, 480);
    REQUIRE (sectionsOf (t).size() == 2);

    doc.undo();   // the split
    doc.undo();   // the drawn note
    doc.undo();   // the delete

    const auto after = sectionsOf (t);
    REQUIRE (after.size() == 1);
    CHECK (after[0].startTick == stored[0].startTick);
    CHECK (after[0].endTick == stored[0].endTick);
    CHECK (notesOf (t).size() == 1);
}

TEST_CASE ("sections: documents edited with the real section operations pass validateLoaded", "[sections][songdocument]")
{
    SongDocument doc;
    doc.mintImportBatch();   // addTrack() tags its track with batch 1, which validateLoaded requires to have been minted
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    addNote (t, 62, 960, 480);
    addNote (t, 64, 1920, 480);
    CHECK_FALSE (SongDocument::validateLoaded (doc.getTree()).has_value());   // virtual section only

    splitAt (doc, { idOf (t) }, 720);          // materialises two stored sections
    REQUIRE (sectionsOf (t).size() == 2);
    CHECK_FALSE (SongDocument::validateLoaded (doc.getTree()).has_value());

    splitAt (doc, { idOf (t) }, 1500);
    REQUIRE (sectionsOf (t).size() == 3);
    CHECK_FALSE (SongDocument::validateLoaded (doc.getTree()).has_value());

    const auto sections = sectionsOf (t);
    resizeSections (doc, { { idOf (t), sections[1].id } }, SectionEdge::Right, 1400);
    CHECK_FALSE (SongDocument::validateLoaded (doc.getTree()).has_value());

    resizeSections (doc, { { idOf (t), sections[2].id } }, SectionEdge::Right, 0);   // clamped to one tick wide
    CHECK_FALSE (SongDocument::validateLoaded (doc.getTree()).has_value());

    resizeSectionsBy (doc, { { idOf (t), sections[0].id } }, SectionEdge::Left, 100);
    CHECK_FALSE (SongDocument::validateLoaded (doc.getTree()).has_value());

    // Overlap: move the first section onto the second.
    moveSections (doc, { { idOf (t), sections[0].id } }, 800);
    const auto moved = sectionsOf (t);
    REQUIRE (moved.size() == 3);
    CHECK (moved[0].startTick < moved[1].endTick);
    CHECK (moved[0].endTick > moved[1].startTick);   // really overlapping
    CHECK_FALSE (SongDocument::validateLoaded (doc.getTree()).has_value());
}

TEST_CASE ("sections: notesInSection returns the notes of one section", "[sections]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    addNote (t, 62, 480, 480);
    const auto id = (juce::int64) t.getProperty (SongIDs::trackId);
    splitAt (doc, { id }, 480);

    const auto sections = sectionsOf (t);
    REQUIRE (sections.size() == 2);
    const auto first = notesInSection (t, sections[0].id);
    const auto second = notesInSection (t, sections[1].id);
    REQUIRE (first.size() == 1);
    REQUIRE (second.size() == 1);
    CHECK ((int) first[0].getProperty (SongIDs::pitch) == 60);
    CHECK ((int) second[0].getProperty (SongIDs::pitch) == 62);
}

TEST_CASE ("sections: notesInSection on a virtual section (id 0) returns every note", "[sections]")
{
    SongDocument doc;
    auto t = addTrack (doc);
    addNote (t, 60, 0, 480);
    addNote (t, 62, 480, 480);
    CHECK (notesInSection (t, 0).size() == 2);
}

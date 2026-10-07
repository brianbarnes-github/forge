#include "PlaybackTestSupport.h"
#include "UI/SectionEdit.h"
#include "UI/SongDocument.h"

#include <catch2/catch_test_macros.hpp>

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

TEST_CASE ("sections: a note's section is its tag, else the first section containing its start, else the first", "[sections]")
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
    CHECK (sectionIdOfNote (note (5000, 0), sections) == 5);  // outside everything: the first
    CHECK (sectionIdOfNote (note (0, 0), {}) == 0);
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

TEST_CASE ("sections: companions are the sections starting at the same tick on the other selected tracks", "[sections]")
{
    SongDocument doc;
    auto a = addTrack (doc, "A");
    auto b = addTrack (doc, "B");
    auto c = addTrack (doc, "C");
    for (auto t : { a, b, c })
        addNote (t, 60, 0, 960);
    const auto sa = addStoredSection (doc, a, 0, 480);
    const auto sb = addStoredSection (doc, b, 0, 960);
    addStoredSection (doc, c, 480, 960);

    const auto idA = (juce::int64) a.getProperty (SongIDs::trackId);
    const auto idB = (juce::int64) b.getProperty (SongIDs::trackId);
    const auto idC = (juce::int64) c.getProperty (SongIDs::trackId);
    const SectionRef clicked { idA, (juce::int64) sa.getProperty (SongIDs::sectionId) };

    const auto both = withCompanions (doc, { idA, idB, idC }, clicked);
    REQUIRE (both.size() == 2);   // C's section starts at 480, not 0
    CHECK (both[0] == clicked);
    CHECK (both[1].trackId == idB);
    CHECK (both[1].sectionId == (juce::int64) sb.getProperty (SongIDs::sectionId));

    // A clicked track outside the selection acts alone.
    CHECK (withCompanions (doc, { idB, idC }, clicked).size() == 1);
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

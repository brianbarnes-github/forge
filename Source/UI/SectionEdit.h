#pragma once

#include "UI/SongDocument.h"

#include <juce_data_structures/juce_data_structures.h>

#include <set>
#include <vector>

// Sections of a track (spec: docs/superpowers/specs/2026-10-06-songsmith-sections-design.md).
// Pure document logic: no UI, no Source/Core.
namespace lotro
{

struct SectionRange
{
    juce::int64 id = 0;   // 0 = the virtual default section of a track that has none stored
    int startTick = 0;    // half-open [startTick, endTick)
    int endTick = 0;
};

struct SectionRef
{
    juce::int64 trackId = 0;
    juce::int64 sectionId = 0;
    bool operator< (const SectionRef& o) const { return trackId != o.trackId ? trackId < o.trackId : sectionId < o.sectionId; }
    bool operator== (const SectionRef& o) const { return trackId == o.trackId && sectionId == o.sectionId; }
};

enum class SectionZone { None, Body, LeftEdge, RightEdge };

struct SectionHit
{
    juce::int64 sectionId = 0;
    SectionZone zone = SectionZone::None;
};

enum class SectionEdge { Left, Right };

// Stored sections in stored order. A non-conductor track with notes but none
// stored reads as one virtual section {0, [0, last note end)}; a track with no
// notes (and the conductor) has none. Never mutates.
std::vector<SectionRange> sectionsOf (const juce::ValueTree& track);

// The note's section: its tag if that names a section, else the first section
// containing its startTick, else the first section, else 0.
juce::int64 sectionIdOfNote (const juce::ValueTree& note, const std::vector<SectionRange>& sections);

// The section under `tick`: an edge when within edgeSlopPixels of a section's
// start or end (nearest wins), else the narrowest section containing the tick.
SectionHit hitTestSection (const std::vector<SectionRange>& sections, int tick,
                           double pixelsPerTick, int edgeSlopPixels);

// `clicked` plus, when its track is in `selectedTrackIds`, the section starting
// at the same tick on every other selected track. `clicked` is first.
std::vector<SectionRef> withCompanions (const SongDocument& doc, const std::set<juce::int64>& selectedTrackIds,
                                        SectionRef clicked);

// Clears the raw-MIDI ordering of a note whose timing changed, so it exports as
// new material with a real note-off. Joins the caller's open transaction.
void markNoteTimingEdited (SongDocument& doc, juce::ValueTree note);

} // namespace lotro

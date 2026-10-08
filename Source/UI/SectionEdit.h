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

// The note's section: its tag if that names a section, else the nearest section
// to its startTick (distance 0 inside [startTick, endTick); ties go to the earlier
// startTick, then stored order), else 0.
juce::int64 sectionIdOfNote (const juce::ValueTree& note, const std::vector<SectionRange>& sections);

// The section under `tick`: an edge when within edgeSlopPixels of a section's
// start or end (nearest wins), else the narrowest section containing the tick.
SectionHit hitTestSection (const std::vector<SectionRange>& sections, int tick,
                           double pixelsPerTick, int edgeSlopPixels);

// Clears the raw-MIDI ordering of a note whose timing changed, so it exports as
// new material with a real note-off. Joins the caller's open transaction.
void markNoteTimingEdited (SongDocument& doc, juce::ValueTree note);

// Mutations. Each materialises the tracks it touches (non-undoable), resolves a
// sectionId of 0 to the track's first section, and runs in exactly one undo
// transaction. A call that would change nothing touches nothing and opens none.
// The conductor, unknown tracks and unknown sections are ignored.

// On each track, every section with start < tick < end becomes [start, tick)
// and a new [tick, end); notes starting at or after tick move to the new one, a
// note straddling tick is cut in two (both halves keep their provenance).
void splitAt (SongDocument& doc, const std::vector<juce::int64>& trackIds, int tick);

// Shifts each section and its notes by one delta, clamped so nothing starts below 0.
void moveSections (SongDocument& doc, const std::vector<SectionRef>& refs, int deltaTicks);

// A single-target / explicit-tick helper: every section's edge goes to the same
// absolute `tick`, so with companions of different lengths it shrinks the longer
// ones and deletes their notes. The drag gesture uses resizeSectionsBy (delta,
// companion-safe) instead.
// Moves one edge of each section to `tick` (at least 1 tick wide). Shrinking
// deletes the notes that start in the band given up and trims a note crossing
// the new edge; growing only extends the range and never touches a note.
void resizeSections (SongDocument& doc, const std::vector<SectionRef>& refs, SectionEdge edge, int tick);

// As resizeSections, but each section's edge moves by deltaTicks from where it is
// (still at least 1 tick wide, each clamped on its own), so sections with
// different ends all move by the same amount.
void resizeSectionsBy (SongDocument& doc, const std::vector<SectionRef>& refs, SectionEdge edge, int deltaTicks);

// Removes the sections and their notes.
void deleteSections (SongDocument& doc, const std::vector<SectionRef>& refs);

} // namespace lotro

#pragma once

#include "UI/SectionEdit.h"
#include "UI/MergeScope.h"
#include "UI/SongDocument.h"

#include <vector>

// Merging a track's notes into another track (spec:
// docs/superpowers/specs/2026-10-10-songsmith-merge-tracks-design.md).
// Pure document logic: no UI, no Source/Core.
namespace lotro
{

struct MergeResult
{
    int inserted = 0;   // carried notes that became new notes in the target
    int dropped = 0;    // carried notes that lay inside an existing same-pitch note
    int extended = 0;   // carried notes joined with existing same-pitch material
    int eventsCarried = 0;   // non-note events that travelled (allEvents only)
    bool changed = false;
};

// True when `targetTrackId` is a non-conductor MIDI track and at least one ref
// names a different non-conductor track.
bool canMergeInto (const SongDocument& doc, const std::vector<SectionRef>& refs, juce::int64 targetTrackId);

// Merges the notes of the referenced sections into the target track at their own
// ticks. copy=false removes them from their source tracks. One undo transaction;
// a call that would change nothing opens none and writes nothing. Refs on the
// target itself, the conductor and unknown tracks/sections are ignored.
// With MergeScope::allEvents the non-note events whose tick lies in a referenced
// section's [start, end) travel too, at the same ticks, channel messages re-addressed
// to the target's channel; End-of-Track and track-name metas never do.
MergeResult mergeSections (SongDocument& doc, const std::vector<SectionRef>& refs,
                           juce::int64 targetTrackId, bool copy,
                           MergeScope scope = MergeScope::notesOnly);

} // namespace lotro

#pragma once

#include <juce_data_structures/juce_data_structures.h>

// Songsmith's editable-document ValueTree schema. See the Songsmith plan's
// "Data model" section for the full node/property layout; this header owns
// the SongDocument class plus the shared node-type/property Identifiers so
// later phases (SongModelBridge, PianoRollComponent, ...) reference the
// schema by these exact names rather than ad-hoc string literals.
namespace lotro
{

namespace SongIDs
{
    // Node types
    extern const juce::Identifier SONG;
    extern const juce::Identifier SOURCE_MIDI;
    extern const juce::Identifier MIDI_TRACK;
    extern const juce::Identifier NOTE;
    extern const juce::Identifier PARTS;
    extern const juce::Identifier PART;
    extern const juce::Identifier ASSIGNMENT;
    extern const juce::Identifier TEMPO_MAP;
    extern const juce::Identifier TEMPO_CHANGE;
    extern const juce::Identifier METER_MAP;
    extern const juce::Identifier METER_CHANGE;

    // SONG properties
    extern const juce::Identifier title;
    extern const juce::Identifier transcriber;
    extern const juce::Identifier tempoBpm;
    extern const juce::Identifier globalTranspose;
    extern const juce::Identifier inputMidiPath;

    // SOURCE_MIDI properties
    extern const juce::Identifier ticksPerQuarter;

    // MIDI_TRACK properties
    extern const juce::Identifier trackId;
    extern const juce::Identifier name;
    extern const juce::Identifier colorArgb;
    extern const juce::Identifier sourceMidiChannel;
    extern const juce::Identifier importBatch;

    // NOTE properties — mirror lotro::Note's field names 1:1 (see
    // Source/Core/Note.h) so the Phase 2 translation layer is a straight
    // property copy.
    extern const juce::Identifier pitch;
    extern const juce::Identifier startTick;
    extern const juce::Identifier durationTicks;
    extern const juce::Identifier velocity;
    extern const juce::Identifier isDrum;
    extern const juce::Identifier sourceTrackIndex;
    extern const juce::Identifier sourceEventIndex;

    // PART properties
    extern const juce::Identifier partId;
    extern const juce::Identifier x;
    extern const juce::Identifier instrumentName;
    extern const juce::Identifier label;
    extern const juce::Identifier drumMapPath;

    // ASSIGNMENT properties (trackId re-used: refs MIDI_TRACK.trackId)
    extern const juce::Identifier transposeSemitones;
    extern const juce::Identifier volumePercent;
    extern const juce::Identifier rangePolicy;

    // TEMPO_CHANGE/METER_CHANGE properties
    extern const juce::Identifier tick;      // shared by both
    extern const juce::Identifier bpm;       // TEMPO_CHANGE only
    extern const juce::Identifier numerator;   // METER_CHANGE only
    extern const juce::Identifier denominator; // METER_CHANGE only
}

/**
 * Owns the Songsmith document ValueTree (rooted at a SONG node) and the
 * single juce::UndoManager for the document.
 *
 * Synthetic id minting: trackId/partId are monotonically-increasing
 * juce::int64 counters stored as hidden bookkeeping properties on the root
 * SONG node ("nextTrackId"/"nextPartId", independent of each other).
 * Counter increments are deliberately non-undoable (nullptr UndoManager) —
 * undoing an addTrack/addPart must not decrement the counter, so an id is
 * never re-minted even if the action that minted it is later undone and a
 * different action is redone in its place.
 *
 * title/transcriber/tempoBpm are deliberately left ABSENT on a freshly
 * constructed SONG node (no default value written), because they mirror
 * forge_core::Config's std::optional<std::string>/std::optional<double>
 * override fields (Config.title/Config.transcriber/Config.tempo) — the
 * bridge that copies these into Config (Phase 2) must be able to tell "no
 * override, fall back to the imported MIDI's own value" apart from "user
 * explicitly set it to empty/some value". Query these with
 * juce::ValueTree::hasProperty() before reading, not just getProperty()'s
 * fallback. PART.x is NOT synthetic like partId — it is minted positionally
 * (getNumParts() + 1 at add-time, mirroring synthesiseConfig's i+1
 * convention in Source/Core/Pipeline.cpp) and is expected to be
 * renumbered/non-stable across removals, matching forge_core::Track::x's
 * own "ABC X: index" semantics.
 */
class SongDocument
{
public:
    SongDocument();

    juce::ValueTree getTree() const noexcept { return tree; }
    juce::UndoManager& getUndoManager() noexcept { return undoManager; }

    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;

    // --- Query helpers ---
    juce::ValueTree getSourceMidiNode() const;
    juce::ValueTree getPartsNode() const;
    juce::ValueTree getTempoMapNode() const;
    juce::ValueTree getMeterMapNode() const;

    int getNumTracks() const;
    juce::ValueTree getTrack (int index) const;
    juce::ValueTree findTrackById (juce::int64 trackIdToFind) const;

    int getNumParts() const;
    juce::ValueTree getPart (int index) const;
    juce::ValueTree findPartById (juce::int64 partIdToFind) const;

    static int getNumAssignments (const juce::ValueTree& part);
    static juce::ValueTree getAssignment (const juce::ValueTree& part, int index);

    // --- Mutations (undoable: one call = one undo transaction) ---
    juce::ValueTree addTrack (const juce::String& trackName, int colorArgb,
                               int sourceMidiChannel, int importBatch);
    void removeTrack (juce::int64 trackIdToRemove);

    // Non-undoable track creation — for MIDI import (SongModelBridge). Mints
    // a trackId the same way addTrack does, but never touches the
    // UndoManager: bulk import is not a user-undoable "edit" (same rationale
    // as appendChildBulk).
    juce::ValueTree addTrackBulk (const juce::String& trackName, int colorArgb,
                                    int sourceMidiChannel, int importBatch);

    juce::ValueTree addPart (const juce::String& instrumentName, const juce::String& label);
    void removePart (juce::int64 partIdToRemove);

    juce::ValueTree addAssignment (juce::ValueTree part, juce::int64 refTrackId,
                                    int transposeSemitones, int volumePercent,
                                    const juce::String& rangePolicy);
    void removeAssignment (juce::ValueTree part, juce::ValueTree assignment);

    // Generic undoable property setter. newTransaction=true (default) begins
    // a fresh undo transaction; pass false to batch multiple property
    // changes into the caller's already-open transaction.
    void setProperty (juce::ValueTree targetTree, const juce::Identifier& propertyId,
                       const juce::var& newValue, bool newTransaction = true);

    // Non-undoable bulk append — used by SongModelBridge's MIDI-import path.
    // Bulk import is not a user-undoable "edit" per the Songsmith plan.
    static void appendChildBulk (juce::ValueTree parent, juce::ValueTree child);

private:
    juce::int64 mintTrackId();
    juce::int64 mintPartId();

    juce::ValueTree tree;
    juce::UndoManager undoManager;
};

} // namespace lotro

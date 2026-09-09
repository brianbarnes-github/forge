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

    // Non-undoable bulk append — for future MIDI-import use (Phase 3).
    // Bulk import is not a user-undoable "edit" per the Songsmith plan.
    static void appendChildBulk (juce::ValueTree parent, juce::ValueTree child);

private:
    juce::int64 mintTrackId();
    juce::int64 mintPartId();

    juce::ValueTree tree;
    juce::UndoManager undoManager;
};

} // namespace lotro

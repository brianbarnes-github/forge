#pragma once

#include "EditorGrid.h"
#include "GridLength.h"
#include "KeyboardLabels.h"
#include "NoteDisplay.h"
#include "PianoRollGeometry.h"
#include "PianoRollNoteSource.h"
#include "Playback/PlaybackController.h"
#include "Playback/PlayheadOverlay.h"
#include "SourceRollEditor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <optional>
#include <vector>

// The shared piano-roll component: rectangles, keyboard gutter, gridlines,
// scroll, wheel zoom/resize, for both roles. Role::Source (Phase 7) owns a
// SourceRollEditor when constructed with a SongDocument, forwarding Canvas
// mouse/keyboard events to it for note create/move/resize/delete/quantize,
// each one undo transaction. Role::Preview (Phase 6) instead overlays a
// translucent playable-range band plus ghost/dropped-note rendering and
// stays read-only.
namespace lotro
{
struct NoteStart; // NoteNavigation.h

class PianoRollComponent : public juce::Component, private PlaybackController::Listener
{
public:
    enum class Role { Source, Preview };

    explicit PianoRollComponent (Role roleIn = Role::Source, SongDocument* editableDocument = nullptr);
    ~PianoRollComponent() override;

    // Source role only (a no-op for Preview, whose canvas has no playback):
    // shows the shared playhead over the notes (beneath the pinned keyboard
    // gutter) and, while playing, page-scrolls to keep it in view. nullptr
    // removes the playhead. Not owned: the controller must outlive this roll
    // or be detached first.
    void setPlayback (PlaybackController* controller);

    // Scroll-aware tick <-> x in this component's own coordinates (x at the
    // pinned gutter's right edge is tick == content origin when unscrolled).
    int xForTickInComponent (double tick) const;
    double tickForXInComponent (int x) const;

    // Width of the pinned keyboard gutter: x positions left of it are covered.
    int getGutterWidth() const noexcept { return geometry.getKeyboardGutterWidth(); }

    // Fired whenever the tick -> x mapping changes (zoom, scroll, resize), so a
    // timing bar above the roll can repaint. Source role with playback only.
    std::function<void()> onViewChanged;
    // Fired whenever the playhead moves, so that bar can move its playhead cap.
    std::function<void()> onPlayheadMoved;
    // Fired whenever the hovered row changes, so that bar can name it in its corner.
    std::function<void()> onHoveredPitchChanged;
    // Pitch of the row under the mouse; -1 for none.
    int getHoveredPitch() const noexcept { return hoveredPitch; }

    bool hasPlayheadForTesting() const noexcept { return playhead != nullptr; }
    bool hasMarkerOverlayForTesting() const noexcept { return markerOverlay != nullptr; }
    bool hasLoopOverlayForTesting() const noexcept { return loopOverlay != nullptr; }

    // Source role with an editor: a note move/resize/rubber-band drag is in progress.
    bool isEditorGestureActive() const noexcept { return sourceEditor != nullptr && sourceEditor->isGestureActive(); }

    Role getRole() const noexcept { return role; }

    // Where the wheel pointer is (Role::Source): over the pinned keyboard gutter
    // or over the note canvas. Decides the plain wheel's meaning.
    enum class WheelRegion { Gutter, Canvas };

    // Source role's pitch-row height (session-only; survives setNoteSource /
    // setEditableTrack). Ctrl/Cmd+wheel changes it within [min, max].
    static constexpr int defaultRowHeight = 14;
    static constexpr int minRowHeight = 10;
    static constexpr int maxRowHeight = 40;
    int getRowHeight() const noexcept { return geometry.getRowHeight(); }
    double getPixelsPerQuarterNote() const noexcept { return geometry.getPixelsPerQuarterNote(); }

    // Repoints the roll at a new note source (or nullptr for "no track
    // selected", which paints an empty roll) and refits the geometry/content
    // size to it. Role::Source always lays out the whole MIDI pitch range
    // (0..127) and scrolls vertically to centre the track's own notes (middle
    // C for an empty track); its timeline then refits to the width on every
    // resize until the user wheel-zooms. Does not take ownership of `source` — the caller
    // (SongsmithMainComponent) owns the SourceTrackNoteSource and must keep
    // it alive at least as long as it stays set here. ticksPerQuarter and
    // meterMapNode drive bar-boundary gridlines (every meter change).
    void setNoteSource (PianoRollNoteSource* source, int ticksPerQuarter, juce::ValueTree meterMapNode);

    // Preview role only: the instrument's playable MIDI range (half-open,
    // [midiLow, midiHigh+1), matching getPitchRange()'s convention), painted
    // as a translucent band with red out-of-range zones above/below it. An
    // empty range (the default, and Role::Source's permanent state) means
    // "don't paint a band" — never call this for a Role::Source roll.
    void setPreviewRangeBand (juce::Range<int> midiRange);
    // What the keyboard gutter shows: piano keys (the default) or each row's MIDI
    // note number over the roll's row bands. Only the MIDI editor's roll sets it.
    void setKeyboardLabels (KeyboardLabels labels);
    KeyboardLabels getKeyboardLabels() const noexcept { return keyboardLabels; }
    // What a Source note's colour shows (View > Color Note by); the Preview roll
    // keeps each note's own colour whatever this says.
    void setNoteColourMode (NoteColourMode mode);
    NoteColourMode getNoteColourMode() const noexcept { return noteColourMode; }
    // How a Source note is drawn and hit (View > Piano Roll Notes); the Preview
    // roll keeps rectangles whatever this says.
    void setNoteShape (NoteShape shape);
    NoteShape getNoteShape() const noexcept { return noteShape; }
    // The text on each Source note (View > Piano Roll Notes' Show Note Names /
    // Show Velocity Numbers), spilling past notes too short for it and hidden
    // below noteLabelMinPixelsPerQuarter; the Preview roll draws none whatever
    // this says.
    void setNoteLabels (NoteLabels labels);
    NoteLabels getNoteLabels() const noexcept { return noteLabels; }
    // Whether a playing playhead scrolls the view to stay visible.
    void setFollowPlayhead (bool on) noexcept { followEnabled = on; }
    bool getFollowPlayhead() const noexcept { return followEnabled; }
    // Whether a click that drops the start marker lands on the nearest grid line
    // (the grid from setGrid or setGridTicks; none when it is 0), and whether note moves,
    // resizes and double-click inserts snap too (SourceRollEditor::setSnapToGrid).
    // Off by default.
    void setSnapToGrid (bool on) noexcept;
    bool getSnapToGrid() const noexcept { return snapEnabled; }
    // A key the canvas's editor left alone, offered before the Viewport (which
    // scrolls on the arrow keys) sees it; return true to claim it.
    std::function<bool (const juce::KeyPress&)> onUnhandledKey;
    // Source role only: the semitone row the last canvas click landed on (-1 for
    // none, and always for Preview), painted lighter and its key tinted. Display
    // state only -- not saved, not undoable. Kept across setNoteSource; cleared
    // when the roll is left with no note source.
    int getActivePitch() const noexcept { return activePitch; }
    // Moves the active row `semitones` up (positive) or down, clamped to 0..127,
    // scrolling just enough to keep it in view. False (nothing done) when no row
    // is active; true at the clamp too, so the key is still claimed.
    bool stepActivePitch (int semitones);
    // Source role with an editor: the track has notes to pick from.
    bool hasNotes() const;
    // Source role with an editor: selects only the note starting nearest `tick`
    // on any pitch (ties: nearest the active row, then higher; see
    // nearestNoteIndex), scrolling just enough to show it. No undo step; the
    // active row stays. False when there is no note.
    bool selectNoteNearest (double tick);
    // Source role with an editor: some note of the track is not selected.
    bool hasUnselectedNotes() const;
    // As selectNoteNearest, but adds the nearest note not yet selected to the
    // selection, so repeated calls grow it outward from `tick`. False when
    // every note is selected (or there is none).
    bool addNoteNearestToSelection (double tick);
    // Source role with an editor: there is a note for selectSteppedNote to move to.
    bool hasSteppedNote (double tick, int direction) const;
    // Source role with an editor: selects only the next (`direction` +1) or
    // previous (-1) note from `tick` on any pitch (see steppedNoteIndex; a
    // selected note starting at `tick` anchors the step), makes its pitch the
    // active row and scrolls just enough to show it. No undo step. Returns the
    // note's start for the caller to move the cursor to; nullopt (nothing
    // changed) when there is no such note.
    std::optional<int> selectSteppedNote (double tick, int direction);
    // Source role with an editor and exactly one note selected: there is a note
    // of its pitch for selectSamePitchNote to move to.
    bool hasSamePitchNote (int direction) const;
    // Selects only the nearest note after (`direction` +1) or before (-1) the one
    // selected note on its pitch (see samePitchNoteIndex), makes that pitch the
    // active row and scrolls just enough to show it. No undo step. Returns the
    // note's start for the caller to move the cursor to; nullopt (nothing
    // changed) unless exactly one note is selected and there is such a note.
    std::optional<int> selectSamePitchNote (int direction);
    // Exactly one note is selected and another note shares its pitch.
    bool canSelectAllOfSamePitch() const;
    // Selects every note of the one selected note's pitch, without scrolling or
    // moving the active row. No undo step. False (nothing changed) unless
    // canSelectAllOfSamePitch.
    bool selectAllOfSamePitch();
    // Source role with an editor: replaces the selection with every note whose
    // pitch `wanted` accepts (none clears it), without scrolling or moving the
    // active row. No undo step. False (nothing changed) without an editor.
    bool selectNotesWithPitch (const std::function<bool (int pitch)>& wanted);
    // Source role with an editor: deletes the selected notes as Delete on the
    // canvas does (one undo step). False when nothing was deleted.
    bool deleteSelectedNotes();
    // Source role with an editor: the selected notes (for Edit > Copy / Cut);
    // empty without an editor.
    std::vector<juce::ValueTree> selectedNotes();
    // Source role with an editor: selects every note of the track (Edit > Select
    // All). No undo step. False when the track has no notes.
    bool selectAllNotes();
    // Source role with an editor: pastes the clipboard with its earliest note at
    // `startTick` and selects the copy (see SourceRollEditor::paste).
    bool pasteNotes (const NoteClipboard& clipboard, int startTick);
    // Source role with an editor: Edit > Insert Note adds the note a double-click
    // at `tick` on the active row would, and selects it (see
    // SourceRollEditor::insertNote). False (nothing changed) without an active row,
    // on a row Snap to Key refuses, or inside a note on that row.
    bool canInsertNote (int tick) const;
    bool insertNote (int tick);
    // Source role with an editor: Edit > Split Notes at `tick` / Join Notes /
    // Legato on the selection (see SourceRollEditor). False when nothing changed.
    bool canSplitNotes (int tick) const;
    bool splitNotes (int tick);
    bool canJoinNotes() const;
    bool joinNotes();
    bool canLegatoNotes() const;
    bool legatoNotes();
    // Source role with an editor: the whole document's undo / redo, dropping any
    // selected note it took away. False (nothing done) without an editor.
    bool undo();
    bool redo();
    // Source role with an editor: there is a note for addSteppedNoteToSelection to add.
    bool hasExtendableNote (int direction) const;
    // Adds the note right after the last selected one (`direction` +1) or right
    // before the first (-1) on any pitch (see extendedNoteIndex), makes its pitch
    // the active row and scrolls just enough to show it. No undo step. Returns
    // the note's start for the caller to move the cursor to; nullopt (nothing
    // changed) with nothing selected or no note beyond that end.
    std::optional<int> addSteppedNoteToSelection (int direction);
    // The earliest start among the selected notes; nullopt with none selected.
    std::optional<int> selectionStart() const;
    // Scrolls horizontally just enough to show `tick` clear of the gutter.
    void scrollTickIntoView (double tick);
    juce::Component& canvasForTesting() noexcept { return canvas; }

    // Source role only (no-op otherwise, or if this roll has no editable
    // document): repoints the roll's SourceRollEditor at a different
    // MIDI_TRACK node. Call this alongside setNoteSource, for the SAME
    // track, whenever the selected track changes -- drawNotes's selection
    // highlight indexes sourceEditor's track by the same child index i as
    // noteSource->getNote(i), an invariant nothing else enforces.
    void setEditableTrack (juce::ValueTree trackNode);

    // Source role only: pushes a raw grid spacing into the roll's SourceRollEditor.
    // It has no length, so the roll draws its zoom-following grid (getGridLength()
    // reads Off); setGrid is the editor's own grid.
    void setGridTicks (int ticks);

    // The MIDI editor's Options > Grid Length / Grid Division: the editor's grid
    // spacing (editorGridTicks at this roll's PPQ, worked out again when
    // setNoteSource brings a new PPQ), and its lines drawn in the canvas.
    void setGrid (GridLength length, GridDivision division);
    GridLength getGridLength() const noexcept { return gridLength; }
    GridDivision getGridDivision() const noexcept { return gridDivision; }

    // Source role only: Options ▸ Snap to Key's filter on double-click note
    // creation (SourceRollEditor::setPitchFilter); a no-op without an editor.
    void setPitchFilter (std::function<bool (int pitch)> allowed);
    // Options ▸ Note Length / Note Division: forwards to SourceRollEditor::setNoteLength;
    // a no-op without an editor (whose getters then read Off, Straight).
    void setNoteLength (GridLength length, GridDivision division);
    GridLength getNoteLength() const noexcept;
    GridDivision getNoteDivision() const noexcept;
    // Options ▸ Note Preview: forwards to SourceRollEditor::setNoteChangeListener;
    // a no-op without an editor.
    void setNoteChangeListener (NoteChangeListener listener);

    // Reads back the current grid spacing (setGrid or setGridTicks, at the roll's PPQ); 0 for a roll with no editor (and
    // for "grid off", which is the same thing as far as snapping goes).
    int getGridTicks() const;

    // The Swing division's strength (percent, clamped to +-100). Repaints the canvas;
    // reports nothing -- the editor reads the grid when it next needs it.
    void setSwing (int percent);
    int getSwing() const noexcept { return swingPercent; }

    // The editor's grid as a value, built on demand from the live METER_MAP (the
    // source drawGridlines uses) so a meter edit needs no hook: a Grid Length of Off
    // gives the raw setGridTicks spacing.
    EditorGrid getEditorGrid() const;

    // Source role only: grid-snaps the current selection. Returns true if
    // anything changed (mirrors SourceRollEditor::quantizeSelection).
    bool quantizeSelection();

    // Other MIDI_TRACK nodes whose notes should render translucently, non-
    // interactively, behind this roll's active track. Meaningless for
    // Role::Preview. Transient UI state -- never persisted, never part of
    // undo history (2026-09-15 upper-region-track-timeline design).
    void setGhostTracks (std::vector<juce::ValueTree> tracks);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    friend struct PianoRollComponentTestAccess;
    friend struct SongsmithMainComponentTestAccess;   // drives a note gesture through handleEditorMouseDown/Up
    friend struct TrackEditorPanelTestAccess;         // clicks a row through handleEditorMouseDown/Up

    class Canvas : public juce::Component
    {
    public:
        explicit Canvas (PianoRollComponent& ownerIn) : owner (ownerIn) { setWantsKeyboardFocus (true); }

        void paint (juce::Graphics& g) override { owner.paintCanvas (g, g.getClipBounds()); }
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDoubleClick (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;
        void mouseMove (const juce::MouseEvent& e) override;
        void mouseExit (const juce::MouseEvent& e) override;
        bool keyPressed (const juce::KeyPress& key) override;

    private:
        PianoRollComponent& owner;
    };

    // The keyboard gutter, painted outside the Viewport's scrollable content
    // so it stays pinned at the Viewport's left edge and isn't affected by
    // horizontal scroll — notes/gridlines scroll underneath it. Sits on top
    // of `viewport` in z-order (added after it) but passes all mouse events
    // through, so wheel events over the gutter's screen area still
    // reaches the canvas beneath it.
    class Gutter : public juce::Component
    {
    public:
        explicit Gutter (PianoRollComponent& ownerIn) : owner (ownerIn) { setInterceptsMouseClicks (false, false); }

        void paint (juce::Graphics& g) override { owner.paintGutter (g); }

    private:
        PianoRollComponent& owner;
    };

    // A plain juce::Viewport has no listener interface — the recommended way
    // to observe scrolling is subclassing and overriding visibleAreaChanged()
    // (see the class's own doc comment). The gutter's key-row labels are
    // drawn at a vertical offset derived from the current scroll position
    // (see paintGutter), so vertical scrolling has to repaint it too.
    class ScrollAwareViewport : public juce::Viewport
    {
    public:
        explicit ScrollAwareViewport (PianoRollComponent& ownerIn) : owner (ownerIn) {}

        void visibleAreaChanged (const juce::Rectangle<int>&) override
        {
            owner.gutter.repaint();
            owner.refreshPlayhead(); // horizontal scroll moves tick -> x under an unchanged position
        }

    private:
        PianoRollComponent& owner;
    };

    void paintCanvas (juce::Graphics& g, juce::Rectangle<int> clip) const;
    void drawRowBands (juce::Graphics& g, juce::Rectangle<int> clip) const;
    void drawRangeBand (juce::Graphics& g, juce::Rectangle<int> clip) const;
    void drawGridlines (juce::Graphics& g, juce::Rectangle<int> clip) const;
    void paintGutter (juce::Graphics& g) const;
    void paintNumberGutter (juce::Graphics& g) const;
    void drawGhostTracks (juce::Graphics& g, juce::Rectangle<int> clip) const;
    void drawNotes (juce::Graphics& g, juce::Rectangle<int> clip) const;

    bool handleEditorMouseDown (juce::Point<int> pos, juce::ModifierKeys mods, bool isDoubleClick);
    bool handleEditorMouseDrag (juce::Point<int> pos, juce::ModifierKeys mods = {});
    bool handleEditorMouseUp (juce::Point<int> pos, juce::ModifierKeys mods = {});
    bool handleEditorKeyPressed (const juce::KeyPress& key);
    void afterEditorGesture (bool changed);

    // Pitch of the row under the mouse (-1 for none), so the keyboard gutter
    // can tint that key and name it. The gutter is mouse-transparent, so the
    // Canvas beneath it tracks this for both the gutter and the note rows.
    void setHoveredPitch (int pitch);
    // Sets the active row (clamped to 0..127) and repaints the canvas and gutter.
    void setActivePitch (int pitch);
    // Scrolls the Viewport just enough that `area` (canvas space) is in view,
    // clear of the pinned keyboard gutter; its top-left wins when it can't all fit.
    void scrollIntoView (juce::Rectangle<int> area);
    // Selects (or with `add`, adds) the nearest note to `tick` not yet selected
    // when `add`, and scrolls to it; false when there is none.
    bool pickNoteNearest (double tick, bool add);
    // The index steppedNoteIndex picks from the track's notes and selection; -1 for none.
    int steppedNote (double tick, int direction) const;
    // The index samePitchNoteIndex picks from the track's notes and selection; -1 for none.
    int samePitchNote (int direction) const;
    // The index extendedNoteIndex picks from the track's notes and selection; -1 for none.
    int extendedNote (int direction) const;
    // The track's note starts and which are selected, index for index (empty with no notes).
    void noteStartsAndSelection (std::vector<NoteStart>& starts, std::vector<bool>& selected) const;
    // Scrolls just enough to show note `index` of the note source.
    void scrollNoteIntoView (int index);

    void rebuildContentSize();
    void fitTimeline();
    // Diamond shapes on the Source roll: half a row, so a tick-0 diamond's
    // left half clears the gutter. Otherwise 0.
    int leadInWidth() const noexcept;
    // Puts leadInWidth() into the geometry, refitting a fitted timeline so it still fits.
    void applyLeadIn();
    void centreOnTrackPitches();
    void layoutGutter();
    void zoom (float wheelDeltaY);
    void wheelScroll (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel);

    // The one wheel decision. Preview: ctrl/cmd zooms, anything else scrolls.
    // Source: ctrl/cmd resizes the pitch rows (1 px per notch, wheel up grows),
    // keeping the pitch under `pointerY` (component coordinates) in place; shift
    // pans horizontally; plain over the Canvas zooms, over the Gutter scrolls
    // vertically (50 px per notch, wheel up = higher pitches; no-op when nothing
    // to scroll, never zooms).
    // Returns false when the event was not consumed (Preview's plain wheel).
    bool handleWheel (WheelRegion region, const juce::ModifierKeys& mods,
                      const juce::MouseWheelDetails& wheel, int pointerY);
    void resizeRowsBy (double pixels, int pointerY);

    // PlaybackController::Listener
    void playbackMarkerChanged() override { if (onViewChanged) onViewChanged(); }
    // The timing bar above draws the loop's shading and brackets but is not a controller listener.
    void playbackLoopChanged() override { if (onViewChanged) onViewChanged(); }
    void playbackPositionChanged() override
    {
        followPlayhead (playback != nullptr && playback->isPlaying());
        if (onPlayheadMoved)
            onPlayheadMoved();
    }
    void followPlayhead (bool playing);
    void layoutPlayhead();
    // The overlay skips repainting when the playhead x is unchanged, so every
    // change to the tick -> x mapping must force one.
    void refreshPlayhead();

    // The viewport area the canvas can fill without scrolling, once the
    // scroll bars a canvas of `contentSize` would force on are subtracted.
    // viewport.getMaximumVisibleWidth/Height only reflect the bars shown
    // for the *current* canvas, which is stale while resizing it.
    juce::Point<int> visibleSizeFor (juce::Point<int> contentSize) const;
    int contentHeight() const;

    // Role::Source: always the whole MIDI range, so notes can be drawn or
    // dragged to any pitch. Role::Preview: union of the note source's own
    // pitch range with `rangeBand`. The band
    // is supplied independently of the note source (via setPreviewRangeBand,
    // which can be called before or after setNoteSource), so neither side
    // alone is guaranteed to cover the other — a note that folds outside the
    // band, or a band drawn wider than every note in view, must both still
    // land on-canvas.
    juce::Range<int> effectivePitchRange() const;

    Role role;
    PianoRollNoteSource* noteSource = nullptr;
    std::unique_ptr<SourceRollEditor> sourceEditor; // Role::Source with an editable document only.
    PianoRollGeometry geometry;
    int ticksPerQuarter = 480;
    juce::ValueTree meterMap;
    juce::Range<int> rangeBand; // Preview role only; empty means "no band".
    bool followEnabled = true;
    bool snapEnabled = false;
    GridLength gridLength = GridLength::Off;
    GridDivision gridDivision = GridDivision::Straight;
    int swingPercent = 0;
    int rawGridTicks = 0;   // setGridTicks's spacing, read while gridLength is Off
    std::vector<juce::ValueTree> ghostTracks; // Source role only; see setGhostTracks.
    KeyboardLabels keyboardLabels = KeyboardLabels::pianoNotes;
    NoteColourMode noteColourMode = NoteColourMode::track;
    NoteShape noteShape = NoteShape::rectangle;
    NoteLabels noteLabels;
    int hoveredPitch = -1;
    int activePitch = -1; // Source role only; see getActivePitch.
    // Unrounded row height (accumulates fractional wheel deltas) and the rounded
    // value pushed into `geometry` after every geometry rebuild (fitTimeline and
    // setNoteSource replace the whole geometry, which would reset it to 14).
    double rowHeightExact = defaultRowHeight;
    int rowHeight = defaultRowHeight;
    bool timelineFitted = false; // Source role only: refit on resize until the user zooms.

    PlaybackController* playback = nullptr; // Source role only; not owned.
    std::unique_ptr<PlayheadOverlay> playhead;
    std::unique_ptr<MarkerOverlay> markerOverlay;
    std::unique_ptr<LoopRangeOverlay> loopOverlay;   // under the playhead and marker lines
    // A plain left click on empty canvas (no drag) drops the start marker; the
    // press decides eligibility, the release (still near the press) commits.
    bool markerClickCandidate = false;
    juce::Point<int> markerPressPos;
    int playheadRepaintCount = 0; // test observability

    ScrollAwareViewport viewport { *this };
    Canvas canvas { *this };
    Gutter gutter { *this };
};

} // namespace lotro

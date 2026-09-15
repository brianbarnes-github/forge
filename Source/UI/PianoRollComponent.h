#pragma once

#include "PianoRollGeometry.h"
#include "PianoRollNoteSource.h"
#include "SourceRollEditor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

// The shared piano-roll component: rectangles, keyboard gutter, gridlines,
// scroll, ctrl+wheel zoom, for both roles. Role::Source (Phase 7) owns a
// SourceRollEditor when constructed with a SongDocument, forwarding Canvas
// mouse/keyboard events to it for note create/move/resize/delete/quantize,
// each one undo transaction. Role::Preview (Phase 6) instead overlays a
// translucent playable-range band plus ghost/dropped-note rendering and
// stays read-only.
namespace lotro
{

class PianoRollComponent : public juce::Component
{
public:
    enum class Role { Source, Preview };

    explicit PianoRollComponent (Role roleIn = Role::Source, SongDocument* editableDocument = nullptr);

    Role getRole() const noexcept { return role; }

    // Repoints the roll at a new note source (or nullptr for "no track
    // selected", which paints an empty roll) and refits the geometry/content
    // size to it. Does not take ownership of `source` — the caller
    // (SongsmithMainComponent) owns the SourceTrackNoteSource and must keep
    // it alive at least as long as it stays set here. ticksPerQuarter and
    // meterMapNode drive bar-boundary gridlines (first meter entry only, per
    // this project's one-meter-timeline convention elsewhere).
    void setNoteSource (PianoRollNoteSource* source, int ticksPerQuarter, juce::ValueTree meterMapNode);

    // Preview role only: the instrument's playable MIDI range (half-open,
    // [midiLow, midiHigh+1), matching getPitchRange()'s convention), painted
    // as a translucent band with red out-of-range zones above/below it. An
    // empty range (the default, and Role::Source's permanent state) means
    // "don't paint a band" — never call this for a Role::Source roll.
    void setPreviewRangeBand (juce::Range<int> midiRange);

    // Source role only (no-op otherwise, or if this roll has no editable
    // document): repoints the roll's SourceRollEditor at a different
    // MIDI_TRACK node. Call this alongside setNoteSource, for the SAME
    // track, whenever the selected track changes -- drawNotes's selection
    // highlight indexes sourceEditor's track by the same child index i as
    // noteSource->getNote(i), an invariant nothing else enforces.
    void setEditableTrack (juce::ValueTree trackNode);

    // Source role only: pushes the toolbar's current grid-size selection
    // into the roll's SourceRollEditor.
    void setGridTicks (int ticks);

    // Source role only: grid-snaps the current selection. Returns true if
    // anything changed (mirrors SourceRollEditor::quantizeSelection).
    bool quantizeSelection();

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    friend struct PianoRollComponentTestAccess;

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
        bool keyPressed (const juce::KeyPress& key) override;

    private:
        PianoRollComponent& owner;
    };

    // The keyboard gutter, painted outside the Viewport's scrollable content
    // so it stays pinned at the Viewport's left edge and isn't affected by
    // horizontal scroll — notes/gridlines scroll underneath it. Sits on top
    // of `viewport` in z-order (added after it) but passes all mouse events
    // through, so scroll/ctrl+wheel-zoom over the gutter's screen area still
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

        void visibleAreaChanged (const juce::Rectangle<int>&) override { owner.gutter.repaint(); }

    private:
        PianoRollComponent& owner;
    };

    void paintCanvas (juce::Graphics& g, juce::Rectangle<int> clip) const;
    void drawRowBands (juce::Graphics& g, juce::Rectangle<int> clip) const;
    void drawRangeBand (juce::Graphics& g, juce::Rectangle<int> clip) const;
    void drawGridlines (juce::Graphics& g, juce::Rectangle<int> clip) const;
    void paintGutter (juce::Graphics& g) const;
    void drawNotes (juce::Graphics& g, juce::Rectangle<int> clip) const;

    bool handleEditorMouseDown (juce::Point<int> pos, juce::ModifierKeys mods, bool isDoubleClick);
    bool handleEditorMouseDrag (juce::Point<int> pos);
    bool handleEditorMouseUp (juce::Point<int> pos);
    bool handleEditorKeyPressed (const juce::KeyPress& key);
    void afterEditorGesture (bool changed);

    void rebuildContentSize();
    void zoom (float wheelDeltaY);

    // Union of the note source's own pitch range with `rangeBand`. The band
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

    ScrollAwareViewport viewport { *this };
    Canvas canvas { *this };
    Gutter gutter { *this };
};

} // namespace lotro

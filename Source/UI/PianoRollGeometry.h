#pragma once

#include "PianoRollNoteSource.h"

namespace lotro
{

// A plain pixel rectangle for one note. Deliberately not juce::Rectangle:
// that type lives in juce_graphics, which forge_tests does not link (only
// juce_core/juce_data_structures) — this struct keeps PianoRollGeometry
// usable headlessly. Source/UI callers can build a juce::Rectangle<int>
// from these four fields directly.
struct PianoRollNoteBounds
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// Pure, headless pixel<->model coordinate math for the piano roll. Owns no
// UI state beyond the numbers needed to place notes: the fixed keyboard
// gutter/row-height layout constants, which pitch is drawn at the top of the
// visible area, and a tick->pixel mapping driven by an independent zoom
// factor (pixels per quarter note) plus a scrollable visible-tick window.
//
// The visible-tick window is exposed as an explicit Range<double> getter/
// setter (rather than being read back from a juce::Viewport's scroll
// position) because Phase 6 needs to synchronize two roll instances on one
// shared time axis — this is the seam that makes that a wiring change later
// rather than a redesign.
class PianoRollGeometry
{
public:
    PianoRollGeometry() = default;

    int getKeyboardGutterWidth() const noexcept { return keyboardGutterWidth; }
    int getRowHeight() const noexcept { return rowHeight; }

    void setTopPitch (int pitch) noexcept { topPitch = pitch; }
    int getTopPitch() const noexcept { return topPitch; }

    void setTicksPerQuarter (int ticksPerQuarterIn) noexcept { ticksPerQuarter = ticksPerQuarterIn; }
    int getTicksPerQuarter() const noexcept { return ticksPerQuarter; }

    void setPixelsPerQuarterNote (double pixelsPerQuarterNoteIn) noexcept { pixelsPerQuarterNote = pixelsPerQuarterNoteIn; }
    double getPixelsPerQuarterNote() const noexcept { return pixelsPerQuarterNote; }

    void setVisibleTickRange (juce::Range<double> range) noexcept { visibleTickRange = range; }
    juce::Range<double> getVisibleTickRange() const noexcept { return visibleTickRange; }

    // Horizontal: tick <-> x, driven by pixelsPerQuarterNote/ticksPerQuarter
    // (zoom) and visibleTickRange's start (scroll offset). No clamping is
    // applied here — a negative or out-of-content scroll position is a
    // valid (if blank) view; callers that want clamping do it when setting
    // the range.
    int xForTick (int tick) const noexcept;
    int tickForX (int x) const noexcept;

    // Vertical: pitch <-> y. Higher pitch draws at a smaller y (nearer the
    // top), matching the mockup's C6-at-top keyboard gutter. Well-defined
    // (not clamped/garbage) for a pitch/y outside the current visible band.
    int yForPitch (int pitch) const noexcept;
    int pitchForY (int y) const noexcept;

    PianoRollNoteBounds noteBounds (const PianoRollNote& note) const noexcept;

    // Default view when a track is first selected: computes a zoom factor
    // and visible tick window so the whole tick range fits the viewport
    // width without horizontal scrolling, and a topPitch placing the
    // highest pitch in range at the top of the viewport. ticksPerQuarter is
    // the document's PPQ (for the zoom-factor conversion), independent of
    // this geometry's own pixelsPerQuarterNote zoom value.
    static PianoRollGeometry fitToContent (juce::Range<int> tickRange,
                                            juce::Range<int> pitchRange,
                                            int ticksPerQuarter,
                                            int viewportWidth,
                                            int viewportHeight);

private:
    int keyboardGutterWidth = 60;
    int rowHeight = 14;
    int topPitch = 84; // C6, per the mockup's default keyboard-gutter alignment
    int ticksPerQuarter = 480;
    double pixelsPerQuarterNote = 40.0;
    juce::Range<double> visibleTickRange { 0.0, 1920.0 };
};

} // namespace lotro

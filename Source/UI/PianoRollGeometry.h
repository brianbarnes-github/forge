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
// factor (pixels per quarter note) plus a content-space origin tick.
//
// Actual scroll position lives in PianoRollComponent's juce::Viewport, not
// here — this class has no notion of "currently visible" ticks. contentOriginTick
// is set once by fitToContent and is the tick that maps to content-space
// x == getKeyboardGutterWidth(); everything to its right is positive content
// x, scrolled by the Viewport like any other content.
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

    void setContentOriginTick (double tick) noexcept { contentOriginTick = tick; }
    double getContentOriginTick() const noexcept { return contentOriginTick; }

    // Horizontal: tick <-> x, driven by pixelsPerQuarterNote/ticksPerQuarter
    // (zoom) and contentOriginTick (the tick at content-space x ==
    // getKeyboardGutterWidth()). No clamping is applied here — a negative or
    // out-of-content origin is a valid (if blank) view; callers that want
    // clamping do it when setting the origin.
    int xForTick (int tick) const noexcept;
    int tickForX (int x) const noexcept;

    // Vertical: pitch <-> y. Higher pitch draws at a smaller y (nearer the
    // top), matching the mockup's C6-at-top keyboard gutter. Well-defined
    // (not clamped/garbage) for a pitch/y outside the current visible band.
    int yForPitch (int pitch) const noexcept;
    int pitchForY (int y) const noexcept;

    PianoRollNoteBounds noteBounds (const PianoRollNote& note) const noexcept;

    // True for the 5 black-key pitch classes {1,3,6,8,10} (C#/D#/F#/G#/A#).
    // `pitch` is a raw MIDI note number; only its pitch class matters, so
    // negative pitches are handled correctly too. Drives the row-band
    // shading in PianoRollComponent — semitone-parity alternation disagrees
    // with the real keyboard at the E/F and B/C boundaries.
    static bool isBlackKey (int pitch) noexcept;

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
    double contentOriginTick = 0.0;
};

} // namespace lotro

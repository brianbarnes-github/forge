#include "PianoRollComponent.h"
#include "SongDocument.h"
#include "SongsmithColours.h"

#include <cmath>

namespace lotro
{

namespace
{
    // Row-band/gutter shades taken verbatim from the Songsmith UI Guide
    // mockup's piano-roll pane background and keyboard gutter, not part of
    // the general SongsmithColours palette (those are piano-roll-specific).
    constexpr juce::uint32 rowBandLight = 0xFF252525;
    constexpr juce::uint32 rowBandDark  = 0xFF202020;
    constexpr juce::uint32 gutterFill   = 0xFF1E1E1E;
    constexpr juce::uint32 gridline     = 0xFF333333;
}

PianoRollComponent::PianoRollComponent (Role roleIn) : role (roleIn)
{
    viewport.setViewedComponent (&canvas, false);
    viewport.setScrollBarsShown (true, true);
    addAndMakeVisible (viewport);
    addAndMakeVisible (gutter); // added after viewport so it paints on top, pinned over the left edge
}

void PianoRollComponent::setNoteSource (PianoRollNoteSource* source, int ticksPerQuarterIn,
                                         juce::ValueTree meterMapNode)
{
    noteSource = source;
    ticksPerQuarter = ticksPerQuarterIn;
    meterMap = meterMapNode;

    geometry = noteSource != nullptr
                   ? PianoRollGeometry::fitToContent (noteSource->getTickRange(), noteSource->getPitchRange(),
                                                       ticksPerQuarter, viewport.getWidth(), viewport.getHeight())
                   : PianoRollGeometry();

    rebuildContentSize();
    canvas.repaint();
}

void PianoRollComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (SongsmithColours::background));
}

void PianoRollComponent::resized()
{
    viewport.setBounds (getLocalBounds());
    rebuildContentSize();

    // Trim the gutter above the viewport's horizontal scrollbar (when shown)
    // so the scrollbar's thumb isn't painted over by the gutter's opaque
    // fill in that bottom-left corner (M1).
    auto gutterBounds = getLocalBounds().withWidth (geometry.getKeyboardGutterWidth());
    if (viewport.getHorizontalScrollBar().isVisible())
        gutterBounds = gutterBounds.withTrimmedBottom (viewport.getScrollBarThickness());
    gutter.setBounds (gutterBounds);
}

void PianoRollComponent::rebuildContentSize()
{
    int width = viewport.getWidth();
    int height = viewport.getHeight();

    if (noteSource != nullptr)
    {
        const auto tickRange = noteSource->getTickRange();
        const auto pitchRange = noteSource->getPitchRange();

        const int contentTickEnd = tickRange.isEmpty() ? tickRange.getStart() : tickRange.getEnd();
        width = juce::jmax (width, geometry.xForTick (contentTickEnd));

        const int pitchSpan = juce::jmax (1, pitchRange.getLength());
        height = juce::jmax (height, pitchSpan * geometry.getRowHeight());
    }

    canvas.setSize (juce::jmax (1, width), juce::jmax (1, height));
}

void PianoRollComponent::zoom (float wheelDeltaY)
{
    if (noteSource == nullptr)
        return;

    const double factor = wheelDeltaY > 0.0f ? 1.1 : (1.0 / 1.1);
    geometry.setPixelsPerQuarterNote (juce::jlimit (2.0, 400.0, geometry.getPixelsPerQuarterNote() * factor));

    rebuildContentSize();
    canvas.repaint();
}

void PianoRollComponent::Canvas::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
        owner.zoom (wheel.deltaY);
    else
        Component::mouseWheelMove (e, wheel);
}

void PianoRollComponent::paintCanvas (juce::Graphics& g, juce::Rectangle<int> clip) const
{
    g.fillAll (juce::Colour (SongsmithColours::background));

    drawRowBands (g, clip);
    drawGridlines (g, clip);
    drawNotes (g, clip);
}

void PianoRollComponent::drawRowBands (juce::Graphics& g, juce::Rectangle<int> clip) const
{
    const int rowHeight = geometry.getRowHeight();
    if (rowHeight <= 0 || clip.isEmpty())
        return;

    const int topPitch = geometry.getTopPitch();
    const int firstRow = clip.getY() / rowHeight;
    const int lastRow = clip.getBottom() / rowHeight;

    for (int row = firstRow; row <= lastRow; ++row)
    {
        const int pitch = topPitch - row;
        g.setColour (juce::Colour (PianoRollGeometry::isBlackKey (pitch) ? rowBandDark : rowBandLight));
        g.fillRect (clip.getX(), row * rowHeight, clip.getWidth(), rowHeight);
    }
}

void PianoRollComponent::drawGridlines (juce::Graphics& g, juce::Rectangle<int> clip) const
{
    int numerator = 4;
    int denominator = 4;
    if (meterMap.isValid() && meterMap.getNumChildren() > 0)
    {
        auto first = meterMap.getChild (0);
        numerator = (int) first.getProperty (SongIDs::numerator, 4);
        denominator = (int) first.getProperty (SongIDs::denominator, 4);
    }
    if (numerator <= 0 || denominator <= 0)
    {
        numerator = 4;
        denominator = 4;
    }

    const int barTicks = (int) std::lround ((double) ticksPerQuarter * numerator * 4.0 / (double) denominator);
    if (barTicks <= 0 || clip.isEmpty())
        return;

    g.setColour (juce::Colour (gridline));

    const int firstBarTick = juce::jmax (0, (geometry.tickForX (clip.getX()) / barTicks) * barTicks);
    for (int tick = firstBarTick; ; tick += barTicks)
    {
        const int x = geometry.xForTick (tick);
        if (x > clip.getRight())
            break;
        if (x >= clip.getX())
            g.drawVerticalLine (x, (float) clip.getY(), (float) clip.getBottom());
    }
}

void PianoRollComponent::paintGutter (juce::Graphics& g) const
{
    const int gutterWidth = geometry.getKeyboardGutterWidth();
    const int gutterHeight = gutter.getHeight();

    g.setColour (juce::Colour (gutterFill));
    g.fillRect (0, 0, gutterWidth, gutterHeight);
    g.setColour (juce::Colour (SongsmithColours::border));
    g.drawVerticalLine (gutterWidth - 1, 0.0f, (float) gutterHeight);

    const int rowHeight = geometry.getRowHeight();
    if (rowHeight <= 0)
        return;

    // The gutter isn't inside the Viewport's scrollable content, so its rows
    // have to be placed manually at the canvas's current vertical scroll
    // offset — this is what keeps its key labels in sync with the note rows
    // scrolling underneath, per the class comment on `visibleAreaChanged`.
    const int scrollY = viewport.getViewPositionY();
    const int topPitch = geometry.getTopPitch();
    const int firstRow = scrollY / rowHeight;
    const int lastRow = (scrollY + gutterHeight) / rowHeight;

    g.setFont (juce::Font (juce::FontOptions (9.0f)));
    g.setColour (juce::Colour (SongsmithColours::textMuted));

    for (int row = firstRow; row <= lastRow; ++row)
    {
        const int pitch = topPitch - row;
        if (((pitch % 12) + 12) % 12 != 0)
            continue; // only label C notes, per the mockup

        const int octave = pitch / 12 - 1; // MIDI convention: pitch 60 == C4
        const int y = geometry.yForPitch (pitch) - scrollY;
        g.drawFittedText ("C" + juce::String (octave), 0, y, gutterWidth - 4, rowHeight,
                           juce::Justification::centredRight, 1);
    }
}

void PianoRollComponent::drawNotes (juce::Graphics& g, juce::Rectangle<int> clip) const
{
    if (noteSource == nullptr)
        return;

    const int numNotes = noteSource->getNumNotes();
    for (int i = 0; i < numNotes; ++i)
    {
        const auto note = noteSource->getNote (i);
        const auto bounds = geometry.noteBounds (note);
        const juce::Rectangle<int> rect (bounds.x, bounds.y, bounds.width, bounds.height);
        if (! rect.intersects (clip))
            continue;

        const auto fill = juce::Colour (note.colourArgb);
        g.setColour (fill);
        g.fillRect (rect);
        g.setColour (fill.brighter (0.4f));
        g.drawRect (rect, 1);
    }
}

} // namespace lotro

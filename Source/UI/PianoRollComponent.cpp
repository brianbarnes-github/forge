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

PianoRollComponent::PianoRollComponent (Role roleIn, SongDocument* editableDocument) : role (roleIn)
{
    if (role == Role::Source && editableDocument != nullptr)
        sourceEditor = std::make_unique<SourceRollEditor> (*editableDocument);

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
                   ? PianoRollGeometry::fitToContent (noteSource->getTickRange(), effectivePitchRange(),
                                                       ticksPerQuarter, viewport.getWidth(), viewport.getHeight())
                   : PianoRollGeometry();

    if (sourceEditor != nullptr)
        sourceEditor->setGeometry (geometry);

    rebuildContentSize();
    canvas.repaint();
}

void PianoRollComponent::setPreviewRangeBand (juce::Range<int> midiRange)
{
    rangeBand = midiRange;

    // The band can arrive after setNoteSource already ran fitToContent's
    // vertical fit off the note source alone — re-derive topPitch so a band
    // wider than every note in view (or a note that folds outside the band)
    // still lands on-canvas, mirroring fitToContent's own topPitch rule.
    if (noteSource != nullptr)
    {
        const auto pitchRange = effectivePitchRange();
        if (! pitchRange.isEmpty())
            geometry.setTopPitch (pitchRange.getEnd() - 1);
    }

    rebuildContentSize();
    canvas.repaint();
}

juce::Range<int> PianoRollComponent::effectivePitchRange() const
{
    auto range = noteSource != nullptr ? noteSource->getPitchRange() : juce::Range<int>();
    if (! rangeBand.isEmpty())
        range = range.isEmpty() ? rangeBand : range.getUnionWith (rangeBand);
    return range;
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
        const auto pitchRange = effectivePitchRange();

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

    if (sourceEditor != nullptr)
        sourceEditor->setGeometry (geometry);

    rebuildContentSize();
    canvas.repaint();
}

void PianoRollComponent::setEditableTrack (juce::ValueTree trackNode)
{
    // Invariant relied on by drawNotes's selection highlight: this must be
    // called with the same MIDI_TRACK node passed to setNoteSource (so index
    // i from noteSource->getNote(i) lines up with sourceEditor's track's
    // child i). Nothing else enforces this -- it just happens to hold today
    // because both are always called together for the same track.
    if (sourceEditor != nullptr)
        sourceEditor->setTrack (trackNode);
}

void PianoRollComponent::setGridTicks (int ticks)
{
    if (sourceEditor != nullptr)
        sourceEditor->setGridTicks (ticks);
}

bool PianoRollComponent::quantizeSelection()
{
    if (sourceEditor == nullptr)
        return false;
    const bool changed = sourceEditor->quantizeSelection();
    afterEditorGesture (changed);
    return changed;
}

void PianoRollComponent::afterEditorGesture (bool changed)
{
    if (! changed)
        return;
    rebuildContentSize();
    canvas.repaint();
}

bool PianoRollComponent::handleEditorMouseDown (juce::Point<int> pos, juce::ModifierKeys mods, bool isDoubleClick)
{
    if (sourceEditor == nullptr)
        return false;
    const bool changed = sourceEditor->mouseDown (pos, mods, isDoubleClick);
    afterEditorGesture (changed);
    return changed;
}

bool PianoRollComponent::handleEditorMouseDrag (juce::Point<int> pos)
{
    if (sourceEditor == nullptr)
        return false;
    const bool changed = sourceEditor->mouseDrag (pos);
    afterEditorGesture (changed);
    return changed;
}

bool PianoRollComponent::handleEditorMouseUp (juce::Point<int> pos)
{
    if (sourceEditor == nullptr)
        return false;
    const bool changed = sourceEditor->mouseUp (pos);
    afterEditorGesture (changed);
    return changed;
}

bool PianoRollComponent::handleEditorKeyPressed (const juce::KeyPress& key)
{
    if (sourceEditor == nullptr)
        return false;
    const bool changed = sourceEditor->keyPressed (key);
    afterEditorGesture (changed);
    return changed;
}

void PianoRollComponent::Canvas::mouseDown (const juce::MouseEvent& e)
{
    // Deliberately thin (just this + the line below): the gesture logic
    // lives in the already-tested handleEditorMouseDown. Not covered by a
    // real-juce::MouseEvent test: grabKeyboardFocus() asserts
    // (isShowing() || isOnDesktop()) on a Component that was never added
    // to a real window, which every Component in the headless test binary
    // is -- see Tests/PianoRollComponent_tests.cpp's mouseDrag/mouseUp
    // end-to-end test comment for why this line is exercised manually
    // (run-ui.sh) rather than by a unit test.
    grabKeyboardFocus();
    owner.handleEditorMouseDown (e.getPosition(), e.mods, false);
}

void PianoRollComponent::Canvas::mouseDoubleClick (const juce::MouseEvent& e)
{
    owner.handleEditorMouseDown (e.getPosition(), e.mods, true);
}

void PianoRollComponent::Canvas::mouseDrag (const juce::MouseEvent& e)
{
    owner.handleEditorMouseDrag (e.getPosition());
}

void PianoRollComponent::Canvas::mouseUp (const juce::MouseEvent& e)
{
    owner.handleEditorMouseUp (e.getPosition());
}

bool PianoRollComponent::Canvas::keyPressed (const juce::KeyPress& key)
{
    return owner.handleEditorKeyPressed (key);
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
    drawRangeBand (g, clip);
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

void PianoRollComponent::drawRangeBand (juce::Graphics& g, juce::Rectangle<int> clip) const
{
    if (role != Role::Preview || rangeBand.isEmpty() || clip.isEmpty())
        return;

    const int rowHeight = geometry.getRowHeight();
    const int bandTop = geometry.yForPitch (rangeBand.getEnd() - 1);
    const int bandBottom = geometry.yForPitch (rangeBand.getStart()) + rowHeight;

    g.setColour (juce::Colour (SongsmithColours::outOfRangeZoneFillHi));
    g.fillRect (clip.withBottom (bandTop));

    g.setColour (juce::Colour (SongsmithColours::outOfRangeZoneFillLo));
    g.fillRect (clip.withTop (bandBottom));

    g.setColour (juce::Colour (SongsmithColours::rangeBandFill));
    g.fillRect (clip.getX(), bandTop, clip.getWidth(), bandBottom - bandTop);

    g.setColour (juce::Colour (SongsmithColours::rangeBandBorder));
    g.drawHorizontalLine (bandTop, (float) clip.getX(), (float) clip.getRight());
    g.drawHorizontalLine (bandBottom, (float) clip.getX(), (float) clip.getRight());
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

        const bool hasGhost = role == Role::Preview && note.state == NoteState::WillFold && note.postPitch.has_value();
        const juce::Rectangle<int> ghostRect = hasGhost
            ? juce::Rectangle<int> (bounds.x, geometry.yForPitch (*note.postPitch), bounds.width, geometry.getRowHeight())
            : juce::Rectangle<int>();

        // A WillFold note's ghost can sit many rows away from its solid
        // rect — cull against the union of both, so a partial repaint (e.g.
        // a Viewport scroll exposing only a newly-visible strip) that
        // contains the ghost's row but not the solid rect's row still draws
        // the note.
        if (! (hasGhost ? rect.getUnion (ghostRect) : rect).intersects (clip))
            continue;

        const auto fill = juce::Colour (note.colourArgb);
        g.setColour (fill);
        g.fillRect (rect);

        if (role == Role::Preview)
        {
            const auto borderArgb = note.state == NoteState::Normal
                                         ? SongsmithColours::previewNoteBorder
                                         : SongsmithColours::outOfRangeBorder;
            g.setColour (juce::Colour (borderArgb));
        }
        else
        {
            g.setColour (fill.brighter (0.4f));
        }
        g.drawRect (rect, 1);

        if (role == Role::Source && sourceEditor != nullptr
            && sourceEditor->isSelected (sourceEditor->getTrackNode().getChild (i)))
        {
            g.setColour (juce::Colour (SongsmithColours::selectionHighlight));
            g.drawRect (rect, 2);
        }

        if (role != Role::Preview)
            continue;

        if (hasGhost)
        {
            g.setColour (juce::Colour (SongsmithColours::accentAmber).withAlpha (0.7f));
            g.drawRect (ghostRect, 1);
        }
        else if (note.state == NoteState::Dropped)
        {
            juce::Graphics::ScopedSaveState hatchClip (g);
            g.reduceClipRegion (rect);
            g.setColour (fill.darker (0.3f));
            const int step = juce::jmax (2, rect.getWidth() / 3);
            for (int x = rect.getX(); x < rect.getRight(); x += step)
                g.drawLine ((float) x, (float) rect.getBottom(), (float) (x + rect.getHeight()), (float) rect.getY());
        }
    }

    if (role == Role::Source && sourceEditor != nullptr)
    {
        const auto bandRect = sourceEditor->getRubberBandRect();
        if (! bandRect.isEmpty())
        {
            g.setColour (juce::Colour (SongsmithColours::selectionHighlight).withAlpha (0.15f));
            g.fillRect (bandRect);
            g.setColour (juce::Colour (SongsmithColours::selectionHighlight).withAlpha (0.6f));
            g.drawRect (bandRect, 1);
        }
    }
}

} // namespace lotro

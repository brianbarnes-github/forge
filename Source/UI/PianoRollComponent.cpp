#include "PianoRollComponent.h"
#include "NoteColours.h"
#include "NoteFootprint.h"
#include "NoteLabel.h"
#include "NoteNavigation.h"
#include "GridLinePaint.h"
#include "SongDocument.h"
#include "SongsmithColours.h"
#include "Theme/PianoRollPalette.h"
#include "Theme/ThemePaint.h"
#include "WheelResize.h"

#include <cmath>

namespace lotro
{

using namespace PianoRollPalette;

namespace
{
    constexpr float blackKeyWidthRatio = 0.6f;

    constexpr int middleC = 60;
    constexpr juce::Range<int> fullMidiPitchRange { 0, 128 }; // half-open, like getPitchRange()
}

PianoRollComponent::PianoRollComponent (Role roleIn, SongDocument* editableDocument) : role (roleIn)
{
    if (role == Role::Source && editableDocument != nullptr)
    {
        sourceEditor = std::make_unique<SourceRollEditor> (*editableDocument);
        sourceEditor->setGridSource ([this] { return getEditorGrid(); });
    }

    viewport.setViewedComponent (&canvas, false);
    viewport.setScrollBarsShown (true, true);
    addAndMakeVisible (viewport);
    addAndMakeVisible (gutter); // added after viewport so it paints on top, pinned over the left edge
}

PianoRollComponent::~PianoRollComponent()
{
    if (playback != nullptr)
        playback->removeListener (this);
    markerOverlay.reset();
    loopOverlay.reset();
    playhead.reset(); // before the viewport: its scroll callbacks touch the playhead
}

int PianoRollComponent::xForTickInComponent (double tick) const
{
    // geometry.xForTick already includes the pinned gutter's width (the canvas
    // starts at the viewport's x == 0 and notes scroll underneath the gutter),
    // so only the horizontal scroll offset is subtracted.
    return geometry.xForTick ((int) tick) - viewport.getViewPositionX();
}

double PianoRollComponent::tickForXInComponent (int x) const
{
    return (double) geometry.tickForX (x + viewport.getViewPositionX());
}

void PianoRollComponent::setPlayback (PlaybackController* controller)
{
    if (role != Role::Source)
        return;

    if (playback != nullptr)
        playback->removeListener (this);
    markerOverlay.reset();
    loopOverlay.reset();
    playhead.reset();
    playback = controller;
    if (playback == nullptr)
        return;

    playback->addListener (this);
    // Under the playhead and marker lines, so they stay visible inside the range.
    loopOverlay = std::make_unique<LoopRangeOverlay> (*playback, [this] (double tick) { return xForTickInComponent (tick); });
    addAndMakeVisible (*loopOverlay);
    playhead = std::make_unique<PlayheadOverlay> (*playback, [this] (double tick) { return xForTickInComponent (tick); });
    addAndMakeVisible (*playhead);
    markerOverlay = std::make_unique<MarkerOverlay> (*playback, [this] (double tick) { return xForTickInComponent (tick); });
    addAndMakeVisible (*markerOverlay);
    gutter.toFront (false); // order: viewport < loop < playhead < marker < gutter, so nothing draws over the keys
    layoutPlayhead();
}

void PianoRollComponent::layoutPlayhead()
{
    if (playhead == nullptr)
        return;
    // Over the scrolling canvas only, not over the scroll bars.
    playhead->setBounds (viewport.getBounds()
                             .withTrimmedBottom (viewport.getHorizontalScrollBar().isVisible() ? viewport.getScrollBarThickness() : 0)
                             .withTrimmedRight (viewport.getVerticalScrollBar().isVisible() ? viewport.getScrollBarThickness() : 0));
    if (markerOverlay != nullptr)
        markerOverlay->setBounds (playhead->getBounds());
    if (loopOverlay != nullptr)
        loopOverlay->setBounds (playhead->getBounds());
}

void PianoRollComponent::refreshPlayhead()
{
    if (playhead == nullptr)
        return;
    ++playheadRepaintCount;
    playhead->repaint();
    if (onViewChanged)
        onViewChanged();
    if (markerOverlay != nullptr)
        markerOverlay->repaint();
    if (loopOverlay != nullptr)
        loopOverlay->repaint();
}

void PianoRollComponent::followPlayhead (bool playing)
{
    if (! followEnabled || ! playing || playback == nullptr || playhead == nullptr)
        return;

    const double tick = playback->getPositionTicks();
    const int visibleWidth = viewport.getMaximumVisibleWidth();
    const int x = xForTickInComponent (tick);
    if (x >= geometry.getKeyboardGutterWidth() && x < visibleWidth)
        return;

    // Clamp exactly as the viewport would. If that leaves the view where it
    // is (e.g. the playhead is at the very end of a fitted song), there is
    // nothing to flip: leave fitted mode and the playhead alone.
    const int maxViewX = juce::jmax (0, canvas.getWidth() - visibleWidth);
    const int target = juce::jlimit (0, maxViewX, geometry.xForTick ((int) tick) - geometry.getKeyboardGutterWidth());
    if (target == viewport.getViewPositionX())
        return;

    viewport.setViewPosition (target, viewport.getViewPositionY()); // page-flip: the playhead lands at the gutter edge
    timelineFitted = false; // do not let a resize refit fight the follow
    refreshPlayhead();
}

void PianoRollComponent::setNoteSource (PianoRollNoteSource* source, int ticksPerQuarterIn,
                                         juce::ValueTree meterMapNode)
{
    noteSource = source;
    ticksPerQuarter = ticksPerQuarterIn;
    meterMap = meterMapNode;

    if (noteSource != nullptr)
        fitTimeline();
    else
    {
        geometry = PianoRollGeometry();
        activePitch = -1;
        gutter.repaint();
    }
    geometry.setRowHeight (rowHeight);
    timelineFitted = role == Role::Source && noteSource != nullptr;

    if (sourceEditor != nullptr)
        sourceEditor->setGeometry (geometry);

    rebuildContentSize();
    if (role == Role::Source && noteSource != nullptr)
        centreOnTrackPitches();
    canvas.repaint();
}

void PianoRollComponent::fitTimeline()
{
    const int availableWidth = visibleSizeFor ({ 0, contentHeight() }).x;
    geometry = PianoRollGeometry::fitToContent (noteSource->getTickRange(), effectivePitchRange(),
                                                 ticksPerQuarter, availableWidth, viewport.getHeight(),
                                                 leadInWidth());
    geometry.setRowHeight (rowHeight); // fitToContent knows only the default
}

int PianoRollComponent::leadInWidth() const noexcept
{
    return role == Role::Source && noteShape != NoteShape::rectangle ? rowHeight / 2 : 0;
}

void PianoRollComponent::applyLeadIn()
{
    if (timelineFitted && noteSource != nullptr)
        fitTimeline();
    else
        geometry.setLeadIn (leadInWidth());
}

void PianoRollComponent::centreOnTrackPitches()
{
    const auto pitches = noteSource->getPitchRange();
    const int centrePitch = pitches.isEmpty() ? middleC : (pitches.getStart() + pitches.getEnd() - 1) / 2;
    const int rowCentreY = geometry.yForPitch (centrePitch) + geometry.getRowHeight() / 2;

    // Viewport::setViewPosition clamps to the canvas, so pitches near either
    // end of the MIDI range just scroll as far as they can.
    viewport.setViewPosition (0, rowCentreY - viewport.getMaximumVisibleHeight() / 2);
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
    if (role == Role::Source)
        return fullMidiPitchRange;

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

    if (timelineFitted && noteSource != nullptr)
    {
        fitTimeline();
        if (sourceEditor != nullptr)
            sourceEditor->setGeometry (geometry);
    }

    rebuildContentSize();
}

void PianoRollComponent::layoutGutter()
{
    // Trim the gutter above the viewport's horizontal scrollbar (when shown)
    // so the scrollbar's thumb isn't painted over by the gutter's opaque
    // fill in that bottom-left corner (M1). Re-run on every content resize,
    // not just resized(): zooming alone can show or hide that scrollbar.
    auto gutterBounds = getLocalBounds().withWidth (geometry.getKeyboardGutterWidth());
    if (viewport.getHorizontalScrollBar().isVisible())
        gutterBounds = gutterBounds.withTrimmedBottom (viewport.getScrollBarThickness());
    gutter.setBounds (gutterBounds);

    // Runs on every content resize (zoom, fit, resize, new source), i.e. on
    // every change of the tick -> x mapping that is not a scroll.
    layoutPlayhead();
    refreshPlayhead();
}

int PianoRollComponent::contentHeight() const
{
    if (noteSource == nullptr)
        return 0;
    return juce::jmax (1, effectivePitchRange().getLength()) * geometry.getRowHeight();
}

juce::Point<int> PianoRollComponent::visibleSizeFor (juce::Point<int> contentSize) const
{
    const int thickness = viewport.getScrollBarThickness();
    const int width = viewport.getWidth();
    const int height = viewport.getHeight();

    bool needsVertical = contentSize.y > height;
    const bool needsHorizontal = contentSize.x > (needsVertical ? width - thickness : width);
    if (needsHorizontal && ! needsVertical)
        needsVertical = contentSize.y > height - thickness;

    return { juce::jmax (0, needsVertical ? width - thickness : width),
             juce::jmax (0, needsHorizontal ? height - thickness : height) };
}

void PianoRollComponent::rebuildContentSize()
{
    juce::Point<int> content;

    if (noteSource != nullptr)
    {
        const auto tickRange = noteSource->getTickRange();
        const int contentTickEnd = tickRange.isEmpty() ? tickRange.getStart() : tickRange.getEnd();
        content = { geometry.xForTick (contentTickEnd), contentHeight() };
    }

    // Filling exactly the visible area (not the whole viewport) keeps a
    // canvas that fits from overhanging by a scroll bar's thickness and
    // summoning the other scroll bar for nothing.
    const auto visible = visibleSizeFor (content);
    canvas.setSize (juce::jmax (1, visible.x, content.x), juce::jmax (1, visible.y, content.y));
    layoutGutter();
}

void PianoRollComponent::zoom (float wheelDeltaY)
{
    if (noteSource == nullptr)
        return;

    // Zoom closes in on the start marker (brought to the middle of the visible
    // notes first); with no marker, on whatever is already at the middle.
    const int centreX = geometry.getKeyboardGutterWidth()
                        + (viewport.getMaximumVisibleWidth() - geometry.getKeyboardGutterWidth()) / 2;
    double anchorTick = tickForXInComponent (centreX);
    if (playback != nullptr)
        if (const auto marker = playback->getMarkerTick())
            anchorTick = *marker;

    timelineFitted = false;

    const double factor = wheelDeltaY > 0.0f ? 1.1 : (1.0 / 1.1);
    geometry.setPixelsPerQuarterNote (juce::jlimit (2.0, 400.0, geometry.getPixelsPerQuarterNote() * factor));

    if (sourceEditor != nullptr)
        sourceEditor->setGeometry (geometry);

    rebuildContentSize();
    viewport.setViewPosition (geometry.xForTick ((int) anchorTick) - centreX, viewport.getViewPositionY());
    refreshPlayhead();
    canvas.repaint();
}

void PianoRollComponent::wheelScroll (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // The canvas sits under the pinned gutter and scrolls with the viewport, so
    // the region comes from the pointer's position in this component's own
    // (unscrolled) coordinates.
    const auto inOwner = e.getEventRelativeTo (this);
    const auto region = inOwner.x < geometry.getKeyboardGutterWidth() ? WheelRegion::Gutter : WheelRegion::Canvas;
    if (! handleWheel (region, e.mods, wheel, inOwner.y))
        canvas.Component::mouseWheelMove (e, wheel); // Preview's plain wheel: the viewport scrolls
}

bool PianoRollComponent::handleWheel (WheelRegion region, const juce::ModifierKeys& mods,
                                      const juce::MouseWheelDetails& wheel, int pointerY)
{
    const bool ctrl = mods.isCtrlDown() || mods.isCommandDown();

    // The LOTRO preview canvas keeps its original wheel mapping
    // (ctrl+wheel zooms, plain wheel scrolls); only the source editor follows
    // the main-view-style mapping.
    if (role == Role::Preview)
    {
        if (! ctrl)
            return false;
        zoom (wheel.deltaY);
        return true;
    }

    constexpr float pixelsPerNotch = 50.0f;
    constexpr double rowPixelsPerNotch = 1.0;

    if (ctrl)
    {
        resizeRowsBy (wheel.deltaY * rowPixelsPerNotch, pointerY);
    }
    else if (mods.isShiftDown())
    {
        viewport.setViewPosition (viewport.getViewPositionX()
                                      + juce::roundToInt ((-wheel.deltaX - wheel.deltaY) * pixelsPerNotch),
                                  viewport.getViewPositionY());
    }
    else if (region == WheelRegion::Gutter)
    {
        // Wheel up = towards the higher pitches at the top. Nothing to scroll: nothing happens.
        const int maxY = juce::jmax (0, canvas.getHeight() - viewport.getMaximumVisibleHeight());
        viewport.setViewPosition (viewport.getViewPositionX(),
                                  juce::jlimit (0, maxY, viewport.getViewPositionY()
                                                             - juce::roundToInt (wheel.deltaY * pixelsPerNotch)));
    }
    else
    {
        zoom (wheel.deltaY);
    }
    return true;
}

void PianoRollComponent::resizeRowsBy (double pixels, int pointerY)
{
    rowHeightExact = wheelresize::accumulateClamped (rowHeightExact, pixels, (double) minRowHeight, (double) maxRowHeight);
    const int newHeight = juce::roundToInt (rowHeightExact);
    if (newHeight == rowHeight)
        return;

    // Keep the same fractional position of the content (so the same pitch) under the pointer.
    const int pointerInViewport = juce::jlimit (0, juce::jmax (0, viewport.getHeight()), pointerY - viewport.getY());
    const int oldScroll = viewport.getViewPositionY();
    const int oldExtent = contentHeight();

    rowHeight = newHeight;
    geometry.setRowHeight (rowHeight);
    applyLeadIn();
    if (sourceEditor != nullptr)
        sourceEditor->setGeometry (geometry);

    rebuildContentSize();
    const int maxY = juce::jmax (0, canvas.getHeight() - viewport.getMaximumVisibleHeight());
    viewport.setViewPosition (viewport.getViewPositionX(),
                              wheelresize::anchoredScroll (oldScroll, pointerInViewport, oldExtent, contentHeight(), maxY));
    gutter.repaint();
    refreshPlayhead();
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
    gridLength = GridLength::Off;
    rawGridTicks = ticks;
    canvas.repaint();
}

void PianoRollComponent::setGrid (GridLength length, GridDivision division)
{
    gridLength = length;
    gridDivision = division;
    rawGridTicks = 0;
    canvas.repaint();
}

void PianoRollComponent::setSwing (int percent)
{
    swingPercent = clampSwingPercent (percent);
    canvas.repaint();
}

EditorGrid PianoRollComponent::getEditorGrid() const
{
    if (gridLength == GridLength::Off)
        return EditorGrid::straight (rawGridTicks);
    return makeEditorGrid (gridLength, gridDivision, swingPercent, ticksPerQuarter,
                           SongDocument::meterChangesOf (meterMap));
}

void PianoRollComponent::setSnapToGrid (bool on) noexcept
{
    snapEnabled = on;
    if (sourceEditor != nullptr)
        sourceEditor->setSnapToGrid (on);
}

void PianoRollComponent::setPitchFilter (std::function<bool (int pitch)> allowed)
{
    if (sourceEditor != nullptr)
        sourceEditor->setPitchFilter (std::move (allowed));
}

void PianoRollComponent::setNoteLength (GridLength length, GridDivision division)
{
    if (sourceEditor != nullptr)
        sourceEditor->setNoteLength (length, division);
}

GridLength PianoRollComponent::getNoteLength() const noexcept
{
    return sourceEditor != nullptr ? sourceEditor->getNoteLength() : GridLength::Off;
}

GridDivision PianoRollComponent::getNoteDivision() const noexcept
{
    return sourceEditor != nullptr ? sourceEditor->getNoteDivision() : GridDivision::Straight;
}

void PianoRollComponent::setNoteChangeListener (NoteChangeListener listener)
{
    if (sourceEditor != nullptr)
        sourceEditor->setNoteChangeListener (std::move (listener));
}

int PianoRollComponent::getGridTicks() const
{
    return sourceEditor != nullptr ? getEditorGrid().spacingTicks : 0;
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
    markerClickCandidate = playback != nullptr && ! isDoubleClick && ! mods.isPopupMenu()
                           && (sourceEditor == nullptr || ! sourceEditor->isOverNote (pos));
    markerPressPos = pos;
    if (role == Role::Source && noteSource != nullptr)
        setActivePitch (geometry.pitchForY (pos.y));
    if (sourceEditor == nullptr)
        return false;
    // The gutter is mouse-transparent and pinned, so a press on a key lands
    // on the canvas within gutter width of the view's left edge.
    const int viewX = pos.x - viewport.getViewPositionX();
    if (mods.isPopupMenu() && ! isDoubleClick && viewX >= 0 && viewX < geometry.getKeyboardGutterWidth())
    {
        sourceEditor->selectPitch (geometry.pitchForY (pos.y),
                                   mods.isShiftDown() || mods.isCtrlDown() || mods.isCommandDown());
        afterEditorGesture (true);
        return true;
    }
    const bool changed = sourceEditor->mouseDown (pos, mods, isDoubleClick);
    afterEditorGesture (changed);
    return changed;
}

bool PianoRollComponent::handleEditorMouseDrag (juce::Point<int> pos, juce::ModifierKeys mods)
{
    if (sourceEditor == nullptr)
        return false;
    const bool changed = sourceEditor->mouseDrag (pos, mods);
    // A moved note carries the active row with it.
    if (const int pitch = sourceEditor->movingNotePitch(); pitch >= 0)
        setActivePitch (pitch);
    afterEditorGesture (changed);
    return changed;
}

bool PianoRollComponent::handleEditorMouseUp (juce::Point<int> pos, juce::ModifierKeys mods)
{
    if (markerClickCandidate && playback != nullptr && pos.getDistanceFrom (markerPressPos) < 4)
    {
        // Alt on the release places the marker freely, as Alt does for a note drag.
        const double tick = geometry.tickForX (pos.x);
        playback->setMarkerTick (snapEnabled && ! mods.isAltDown() ? snapToEditorGrid (getEditorGrid(), tick) : tick);
    }
    markerClickCandidate = false;
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
    owner.setHoveredPitch (owner.geometry.pitchForY (e.getPosition().y));
    owner.handleEditorMouseDrag (e.getPosition(), e.mods);
}

void PianoRollComponent::Canvas::mouseMove (const juce::MouseEvent& e)
{
    owner.setHoveredPitch (owner.geometry.pitchForY (e.getPosition().y));
}

void PianoRollComponent::Canvas::mouseExit (const juce::MouseEvent&)
{
    owner.setHoveredPitch (-1);
}

void PianoRollComponent::setHoveredPitch (int pitch)
{
    if (pitch < 0 || pitch > 127 || noteSource == nullptr)
        pitch = -1;
    if (pitch == hoveredPitch)
        return;
    hoveredPitch = pitch;
    gutter.repaint();
    if (onHoveredPitchChanged)
        onHoveredPitchChanged();
}

void PianoRollComponent::setActivePitch (int pitch)
{
    pitch = juce::jlimit (0, 127, pitch);
    if (pitch == activePitch)
        return;
    activePitch = pitch;
    canvas.repaint();
    gutter.repaint();
}

bool PianoRollComponent::stepActivePitch (int semitones)
{
    if (activePitch < 0)
        return false;
    setActivePitch (activePitch + semitones);
    const int top = geometry.yForPitch (activePitch);
    scrollIntoView ({ viewport.getViewPositionX() + geometry.getKeyboardGutterWidth(), top, 1, geometry.getRowHeight() });
    return true;
}

bool PianoRollComponent::hasNotes() const
{
    return sourceEditor != nullptr && noteSource != nullptr && noteSource->getNumNotes() > 0;
}

bool PianoRollComponent::selectNoteNearest (double tick)
{
    return pickNoteNearest (tick, false);
}

bool PianoRollComponent::hasUnselectedNotes() const
{
    if (! hasNotes())
        return false;
    const auto notes = SongDocument::getNotesNode (sourceEditor->getTrackNode());
    for (int i = 0; i < notes.getNumChildren(); ++i)
        if (! sourceEditor->isSelected (notes.getChild (i)))
            return true;
    return false;
}

bool PianoRollComponent::addNoteNearestToSelection (double tick)
{
    return pickNoteNearest (tick, true);
}

bool PianoRollComponent::pickNoteNearest (double tick, bool add)
{
    if (! hasNotes())
        return false;
    // Note i of the source is child i of the editor's track (see setEditableTrack).
    const auto notes = SongDocument::getNotesNode (sourceEditor->getTrackNode());
    std::vector<NoteStart> starts;
    std::vector<bool> selected;
    for (int i = 0; i < noteSource->getNumNotes(); ++i)
    {
        const auto note = noteSource->getNote (i);
        starts.push_back ({ note.startTick, note.pitch });
        selected.push_back (add && sourceEditor->isSelected (notes.getChild (i)));
    }
    const int index = nearestNoteIndex (starts, tick, activePitch, selected);
    if (index < 0)
        return false;
    if (add)
        sourceEditor->addToSelection (notes.getChild (index));
    else
        sourceEditor->selectOnly (notes.getChild (index));
    scrollNoteIntoView (index);
    canvas.repaint();
    return true;
}

void PianoRollComponent::scrollNoteIntoView (int index)
{
    // The footprint, so a diamond's left half is not left under the keyboard.
    const auto bounds = geometry.noteBounds (noteSource->getNote (index));
    const auto area = noteFootprint (bounds, role == Role::Source ? noteShape : NoteShape::rectangle).hit;
    scrollIntoView ({ area.getX(), area.getY(), juce::jmax (1, area.getWidth()), juce::jmax (1, area.getHeight()) });
}

void PianoRollComponent::noteStartsAndSelection (std::vector<NoteStart>& starts, std::vector<bool>& selected) const
{
    starts.clear();
    selected.clear();
    if (! hasNotes())
        return;
    // Note i of the source is child i of the editor's track (see setEditableTrack).
    const auto notes = SongDocument::getNotesNode (sourceEditor->getTrackNode());
    for (int i = 0; i < noteSource->getNumNotes(); ++i)
    {
        const auto note = noteSource->getNote (i);
        starts.push_back ({ note.startTick, note.pitch });
        selected.push_back (sourceEditor->isSelected (notes.getChild (i)));
    }
}

int PianoRollComponent::steppedNote (double tick, int direction) const
{
    std::vector<NoteStart> starts;
    std::vector<bool> selected;
    noteStartsAndSelection (starts, selected);
    return steppedNoteIndex (starts, tick, selected, direction);
}

int PianoRollComponent::samePitchNote (int direction) const
{
    if (! hasNotes() || sourceEditor->getNumSelected() != 1)
        return -1;
    std::vector<NoteStart> starts;
    std::vector<bool> selected;
    noteStartsAndSelection (starts, selected);
    return samePitchNoteIndex (starts, selected, direction);
}

bool PianoRollComponent::hasSamePitchNote (int direction) const
{
    return samePitchNote (direction) >= 0;
}

std::optional<int> PianoRollComponent::selectSamePitchNote (int direction)
{
    const int index = samePitchNote (direction);
    if (index < 0)
        return std::nullopt;
    const auto note = noteSource->getNote (index);
    sourceEditor->selectOnly (SongDocument::getNotesNode (sourceEditor->getTrackNode()).getChild (index));
    setActivePitch (note.pitch);
    scrollNoteIntoView (index);
    canvas.repaint();
    return note.startTick;
}

bool PianoRollComponent::canSelectAllOfSamePitch() const
{
    return hasSamePitchNote (-1) || hasSamePitchNote (+1);
}

bool PianoRollComponent::selectAllOfSamePitch()
{
    if (! canSelectAllOfSamePitch())
        return false;
    std::vector<NoteStart> starts;
    std::vector<bool> selected;
    noteStartsAndSelection (starts, selected);
    sourceEditor->selectPitch (starts[(std::size_t) soleSelectedIndex (selected)].pitch, false);
    canvas.repaint();
    return true;
}

bool PianoRollComponent::selectNotesWithPitch (const std::function<bool (int pitch)>& wanted)
{
    if (sourceEditor == nullptr)
        return false;
    sourceEditor->selectPitches (wanted);
    canvas.repaint();
    return true;
}

bool PianoRollComponent::deleteSelectedNotes()
{
    return handleEditorKeyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
}

bool PianoRollComponent::hasSteppedNote (double tick, int direction) const
{
    return steppedNote (tick, direction) >= 0;
}

std::optional<int> PianoRollComponent::selectSteppedNote (double tick, int direction)
{
    const int index = steppedNote (tick, direction);
    if (index < 0)
        return std::nullopt;
    const auto note = noteSource->getNote (index);
    sourceEditor->selectOnly (SongDocument::getNotesNode (sourceEditor->getTrackNode()).getChild (index));
    setActivePitch (note.pitch);
    scrollNoteIntoView (index);
    canvas.repaint();
    return note.startTick;
}

int PianoRollComponent::extendedNote (int direction) const
{
    std::vector<NoteStart> starts;
    std::vector<bool> selected;
    noteStartsAndSelection (starts, selected);
    return extendedNoteIndex (starts, selected, direction);
}

bool PianoRollComponent::hasExtendableNote (int direction) const
{
    return extendedNote (direction) >= 0;
}

std::optional<int> PianoRollComponent::addSteppedNoteToSelection (int direction)
{
    const int index = extendedNote (direction);
    if (index < 0)
        return std::nullopt;
    const auto note = noteSource->getNote (index);
    sourceEditor->addToSelection (SongDocument::getNotesNode (sourceEditor->getTrackNode()).getChild (index));
    setActivePitch (note.pitch);
    scrollNoteIntoView (index);
    canvas.repaint();
    return note.startTick;
}

std::optional<int> PianoRollComponent::selectionStart() const
{
    std::vector<NoteStart> starts;
    std::vector<bool> selected;
    noteStartsAndSelection (starts, selected);
    const int start = selectionStartTick (starts, selected);
    if (start < 0)
        return std::nullopt;
    return start;
}

void PianoRollComponent::scrollTickIntoView (double tick)
{
    const int x = geometry.xForTick ((int) tick);
    scrollIntoView ({ x, viewport.getViewPositionY(), 1, 1 });
}

void PianoRollComponent::scrollIntoView (juce::Rectangle<int> area)
{
    const int gutterWidth = geometry.getKeyboardGutterWidth();
    const int viewWidth = viewport.getMaximumVisibleWidth();
    const int viewHeight = viewport.getMaximumVisibleHeight();
    int x = viewport.getViewPositionX();
    int y = viewport.getViewPositionY();

    if (area.getRight() > x + viewWidth)
        x = area.getRight() - viewWidth;
    if (area.getX() < x + gutterWidth)
        x = area.getX() - gutterWidth;
    if (area.getBottom() > y + viewHeight)
        y = area.getBottom() - viewHeight;
    if (area.getY() < y)
        y = area.getY();

    if (x != viewport.getViewPositionX())
        timelineFitted = false; // do not let a resize refit undo the scroll
    viewport.setViewPosition (juce::jmax (0, x), juce::jmax (0, y));
}

void PianoRollComponent::Canvas::mouseUp (const juce::MouseEvent& e)
{
    owner.handleEditorMouseUp (e.getPosition(), e.mods);
}

bool PianoRollComponent::Canvas::keyPressed (const juce::KeyPress& key)
{
    if (owner.handleEditorKeyPressed (key))
        return true;
    return owner.onUnhandledKey && owner.onUnhandledKey (key);
}

void PianoRollComponent::Canvas::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    owner.wheelScroll (e, wheel);
}

void PianoRollComponent::paintCanvas (juce::Graphics& g, juce::Rectangle<int> clip) const
{
    g.fillAll (juce::Colour (SongsmithColours::background));

    drawRowBands (g, clip);
    drawRangeBand (g, clip);
    drawGridlines (g, clip);
    drawGhostTracks (g, clip);
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
        // The active row: the copper tint over the light band, composited once
        // so sharps and naturals get the same opaque colour.
        g.setColour (pitch == activePitch ? juce::Colour (rowBandLight).overlaidWith (juce::Colour (activeRowTint))
                     : juce::Colour (PianoRollGeometry::isBlackKey (pitch) ? rowBandDark : rowBandLight));
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
    const EditorGridLines editorGrid { gridLength == GridLength::Off ? 0 : getGridTicks(),
                                       gridLevelForLength (gridLength),
                                       gridDivision == GridDivision::Swing, getSwing() / 100.0 };
    paintGridLines (g, clip, geometry.getPixelsPerQuarterNote() / (double) ticksPerQuarter, ticksPerQuarter,
                    SongDocument::meterChangesOf (meterMap), editorGrid,
                    [this] (int tick) { return geometry.xForTick (tick); },
                    [this] (int x) { return geometry.tickForX (x); });
}

void PianoRollComponent::setKeyboardLabels (KeyboardLabels labels)
{
    keyboardLabels = labels;
    gutter.repaint();
}

void PianoRollComponent::setNoteColourMode (NoteColourMode mode)
{
    noteColourMode = mode;
    canvas.repaint();
}

void PianoRollComponent::setNoteShape (NoteShape shape)
{
    noteShape = shape;
    if (sourceEditor != nullptr)
        sourceEditor->setNoteShape (shape);

    if (geometry.getLeadIn() != leadInWidth())
    {
        applyLeadIn();
        if (sourceEditor != nullptr)
            sourceEditor->setGeometry (geometry);
        rebuildContentSize();
        refreshPlayhead();
    }
    canvas.repaint();
}

void PianoRollComponent::setNoteLabels (NoteLabels labels)
{
    noteLabels = labels;
    canvas.repaint();
}

void PianoRollComponent::paintGutter (juce::Graphics& g) const
{
    if (keyboardLabels == KeyboardLabels::midiNumbers)
    {
        paintNumberGutter (g);
        return;
    }

    const int gutterWidth = geometry.getKeyboardGutterWidth();
    const int gutterHeight = gutter.getHeight();

    // White keys shade slightly toward their right edge, like a lit keyboard.
    g.setGradientFill (juce::ColourGradient (juce::Colour (whiteKey), 0.0f, 0.0f,
                                             juce::Colour (whiteKeyShade), (float) gutterWidth, 0.0f, false));
    g.fillRect (0, 0, gutterWidth, gutterHeight);

    const int rowHeight = geometry.getRowHeight();
    if (rowHeight > 0)
    {
        // The gutter isn't inside the Viewport's scrollable content, so its rows
        // have to be placed manually at the canvas's current vertical scroll
        // offset — this is what keeps its keys in sync with the note rows
        // scrolling underneath, per the class comment on `visibleAreaChanged`.
        const int scrollY = viewport.getViewPositionY();
        const int topPitch = geometry.getTopPitch();
        const int firstRow = scrollY / rowHeight;
        const int lastRow = (scrollY + gutterHeight) / rowHeight;
        const int blackKeyWidth = juce::roundToInt ((float) gutterWidth * blackKeyWidthRatio);

        g.setFont (juce::Font (juce::FontOptions (9.0f)));

        for (int row = firstRow; row <= lastRow; ++row)
        {
            const int pitch = topPitch - row;
            const int pitchClass = ((pitch % 12) + 12) % 12;
            const int y = geometry.yForPitch (pitch) - scrollY;

            // The active row's key looks hovered too, so it stays named.
            const bool hovered = pitch == hoveredPitch || pitch == activePitch;

            if (PianoRollGeometry::isBlackKey (pitch))
            {
                // Glossy black key: lighter toward the bottom, a highlight on top, a shadow to the right.
                const juce::Rectangle<float> key (0.0f, (float) y, (float) blackKeyWidth, (float) rowHeight);
                if (hovered)
                    g.setColour (juce::Colour (blackKeyHover));
                else
                    g.setGradientFill (juce::ColourGradient (juce::Colour (blackKeyTop), 0.0f, key.getY(),
                                                             juce::Colour (blackKeyBottom), 0.0f, key.getBottom(), false));
                g.fillRect (key);
                g.setColour (juce::Colour (blackKeyHighlight));
                g.fillRect (0, y, blackKeyWidth - 1, 1);
                g.setColour (juce::Colours::black.withAlpha (0.35f));
                g.fillRect (blackKeyWidth, y, 2, rowHeight);

                // The two white keys either side meet behind the black key's middle.
                g.setColour (juce::Colour (whiteKeyDivider));
                g.drawHorizontalLine (y + rowHeight / 2, (float) blackKeyWidth, (float) gutterWidth);

                if (hovered) // named in the white-key area to its right
                {
                    g.setColour (juce::Colour (hoverLabel));
                    g.drawFittedText (PianoRollGeometry::noteName (pitch), blackKeyWidth, y,
                                       gutterWidth - blackKeyWidth - 4, rowHeight,
                                       juce::Justification::centredRight, 1);
                }
                continue;
            }

            if (hovered)
            {
                // B and E carry the B/C, E/F divider on their top pixel,
                // drawn by the row above -- tint beneath it, not over it.
                const int tintTop = (pitchClass == 11 || pitchClass == 4) ? y + 1 : y;
                g.setColour (juce::Colour (whiteKeyHover));
                g.fillRect (0, tintTop, gutterWidth, y + rowHeight - tintTop);
            }

            // C and F have a white key directly beneath them (B, E) with no
            // black key in between -- a full-width edge, like a real keyboard.
            if (pitchClass == 0 || pitchClass == 5)
            {
                g.setColour (juce::Colour (whiteKeyDivider));
                g.drawHorizontalLine (y + rowHeight, 0.0f, (float) gutterWidth);
            }

            if (hovered)
            {
                g.setColour (juce::Colour (hoverLabel));
                g.drawFittedText (PianoRollGeometry::noteName (pitch), 0, y, gutterWidth - 4, rowHeight,
                                   juce::Justification::centredRight, 1);
            }
            else if (pitchClass == 0) // only label C notes, per the mockup
            {
                const int octave = pitch / 12 - 1; // MIDI convention: pitch 60 == C4
                g.setColour (juce::Colour (whiteKeyLabel));
                g.drawFittedText ("C" + juce::String (octave), 0, y, gutterWidth - 4, rowHeight,
                                   juce::Justification::centredRight, 1);
            }
        }
    }

    g.setColour (juce::Colour (SongsmithColours::border));
    g.drawVerticalLine (gutterWidth - 1, 0.0f, (float) gutterHeight);
}

// MIDI Numbers: the canvas's row bands carried into the gutter, each row named by its
// note number. Hover and the active row tint the row as they do on the canvas.
void PianoRollComponent::paintNumberGutter (juce::Graphics& g) const
{
    const int gutterWidth = geometry.getKeyboardGutterWidth();
    const int gutterHeight = gutter.getHeight();

    g.setColour (juce::Colour (rowBandLight));
    g.fillRect (0, 0, gutterWidth, gutterHeight);

    const int rowHeight = geometry.getRowHeight();
    if (rowHeight > 0)
    {
        // Placed at the canvas's scroll offset, as paintGutter does.
        const int scrollY = viewport.getViewPositionY();
        const int topPitch = geometry.getTopPitch();
        const int firstRow = scrollY / rowHeight;
        const int lastRow = (scrollY + gutterHeight) / rowHeight;

        g.setFont (juce::Font (juce::FontOptions (9.0f)));

        for (int row = firstRow; row <= lastRow; ++row)
        {
            const int pitch = topPitch - row;
            if (pitch < 0 || pitch > 127)
                continue;
            const int pitchClass = ((pitch % 12) + 12) % 12;
            const int y = geometry.yForPitch (pitch) - scrollY;
            const bool hovered = pitch == hoveredPitch;

            g.setColour (hovered                ? juce::Colour (numberHover)
                         : pitch == activePitch ? juce::Colour (rowBandLight).overlaidWith (juce::Colour (activeRowTint))
                         : juce::Colour (PianoRollGeometry::isBlackKey (pitch) ? rowBandDark : rowBandLight));
            g.fillRect (0, y, gutterWidth, rowHeight);

            // B and E meet C and F with no sharp between: a line on their top pixel.
            if (pitchClass == 11 || pitchClass == 4)
            {
                g.setColour (juce::Colour (numberDivider));
                g.drawHorizontalLine (y, 0.0f, (float) gutterWidth);
            }

            g.setColour (juce::Colour (hovered || pitch == activePitch ? numberHoverLabel : numberLabel));
            g.drawFittedText (juce::String (pitch), 0, y, gutterWidth - 4, rowHeight,
                              juce::Justification::centredRight, 1);
        }
    }

    g.setColour (juce::Colour (SongsmithColours::border));
    g.drawVerticalLine (gutterWidth - 1, 0.0f, (float) gutterHeight);
}

void PianoRollComponent::setGhostTracks (std::vector<juce::ValueTree> tracks)
{
    ghostTracks = std::move (tracks);
    canvas.repaint();
}

void PianoRollComponent::drawGhostTracks (juce::Graphics& g, juce::Rectangle<int> clip) const
{
    if (role != Role::Source || ghostTracks.empty())
        return;

    g.setColour (juce::Colour (SongsmithColours::accentCopper).withAlpha (0.25f));

    for (auto& ghostTrack : ghostTracks)
    {
        auto notes = SongDocument::getNotesNode (ghostTrack);
        for (int i = 0; i < notes.getNumChildren(); ++i)
        {
            auto note = notes.getChild (i);
            const int pitch = (int) note.getProperty (SongIDs::pitch);
            const int startTick = (int) note.getProperty (SongIDs::startTick);
            const int durationTicks = (int) note.getProperty (SongIDs::durationTicks);

            const auto footprint = noteFootprint (geometry.noteBounds ({ pitch, startTick, durationTicks }), noteShape);
            if (! footprint.hit.intersects (clip))
                continue;

            if (noteShape == NoteShape::rectangle)
            {
                g.fillRect (footprint.body);
                continue;
            }

            // A flat diamond (and Diamond + Length's line) in the ghost colour; the line
            // starts at the diamond's right point so the translucent fills never overlap.
            g.fillRect (footprint.lengthLine.withLeft (juce::jmax (footprint.lengthLine.getX(), footprint.body.getRight())));
            g.fillPath (ThemePaint::diamondPath (footprint.body.toFloat()));
        }
    }
}

void PianoRollComponent::drawNotes (juce::Graphics& g, juce::Rectangle<int> clip) const
{
    if (noteSource == nullptr)
        return;

    const bool showLabels = role == Role::Source && (noteLabels.names || noteLabels.velocities)
                         && noteLabelsShownAt (geometry.getPixelsPerQuarterNote());
    const juce::Font labelFont (juce::FontOptions (juce::jlimit (8.0f, 12.0f, (float) geometry.getRowHeight() - 4.0f)));
    if (showLabels)
        g.setFont (labelFont);

    const int numNotes = noteSource->getNumNotes();
    for (int i = 0; i < numNotes; ++i)
    {
        const auto note = noteSource->getNote (i);
        const auto bounds = geometry.noteBounds (note);
        const juce::Rectangle<int> rect (bounds.x, bounds.y, bounds.width, bounds.height);
        const auto shape = role == Role::Source ? noteShape : NoteShape::rectangle;
        const auto footprint = noteFootprint (bounds, shape);

        const bool hasGhost = role == Role::Preview && note.state == NoteState::WillFold && note.postPitch.has_value();
        const juce::Rectangle<int> ghostRect = hasGhost
            ? juce::Rectangle<int> (bounds.x, geometry.yForPitch (*note.postPitch), bounds.width, geometry.getRowHeight())
            : juce::Rectangle<int>();

        // A WillFold note's ghost can sit many rows away from its solid
        // rect — cull against the union of both, so a partial repaint (e.g.
        // a Viewport scroll exposing only a newly-visible strip) that
        // contains the ghost's row but not the solid rect's row still draws
        // the note.
        // Glow and selection edge reach noteOverhang px beyond the note.
        // A diamond reaches half a row left of the note's start.
        const auto paintedRect = footprint.hit.expanded (ThemePaint::noteOverhang);
        // A label is never shortened, so it can reach well past its note, but only
        // rightward along its own row: measure it only when that could meet the clip.
        const int labelX = noteLabelX (footprint);
        const bool labelMayShow = showLabels && labelX < clip.getRight()
            && bounds.y < clip.getBottom() && bounds.y + bounds.height > clip.getY();
        const auto label = labelMayShow ? noteLabelText (noteLabels, note.pitch, note.velocity) : juce::String();
        const juce::Rectangle<int> labelRect = labelMayShow
            ? juce::Rectangle<int> (labelX, bounds.y, juce::GlyphArrangement::getStringWidthInt (labelFont, label) + 2,
                                    bounds.height)
            : juce::Rectangle<int>();
        if (! (hasGhost ? paintedRect.getUnion (ghostRect) : paintedRect.getUnion (labelRect)).intersects (clip))
            continue;

        const auto fill = role == Role::Source
            ? juce::Colour (noteColourFor (noteColourMode, note.pitch, note.velocity, note.channel, note.colourArgb))
            : juce::Colour (note.colourArgb);
        ThemePaint::NoteStyle style;
        if (role == Role::Preview)
            style.border = juce::Colour (note.state == NoteState::Normal ? SongsmithColours::previewNoteBorder
                                                                         : SongsmithColours::outOfRangeBorder);
        style.selected = role == Role::Source && sourceEditor != nullptr
            && sourceEditor->isSelected (SongDocument::getNotesNode (sourceEditor->getTrackNode()).getChild (i));
        if (shape == NoteShape::rectangle)
            ThemePaint::paintNoteBody (g, rect, fill, style);
        else
            ThemePaint::paintDiamondNote (g, footprint.body, footprint.lengthLine, fill, style);

        if (labelMayShow)
        {
            // Over a rectangle's fill in a contrasting colour; past it, or beside a
            // diamond, in the roll's text colour.
            if (shape == NoteShape::rectangle)
            {
                juce::Graphics::ScopedSaveState inside (g);
                g.reduceClipRegion (rect);
                g.setColour (noteLabelColour (fill));
                g.drawText (label, labelRect, juce::Justification::centredLeft, false);
            }
            juce::Graphics::ScopedSaveState outside (g);
            if (shape == NoteShape::rectangle)
                g.excludeClipRegion (rect);
            g.setColour (juce::Colour (SongsmithColours::text));
            g.drawText (label, labelRect, juce::Justification::centredLeft, false);
        }

        if (role != Role::Preview)
            continue;

        if (hasGhost)
        {
            g.setColour (juce::Colour (SongsmithColours::accentCopper).withAlpha (0.7f));
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

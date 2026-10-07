#include "TrackListComponent.h"
#include "SongsmithColours.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lotro
{

namespace
{
    // The earliest start among the sections `refs` name and their member notes:
    // the same bound moveSections clamps a move by (a note lying before every
    // section belongs to the first, so it can start before its section).
    int earliestStart (const SongDocument& doc, const std::vector<SectionRef>& refs)
    {
        int lowest = std::numeric_limits<int>::max();
        for (const auto& r : refs)
        {
            const auto track = doc.findTrackById (r.trackId);
            const auto sections = sectionsOf (track);
            for (const auto& s : sections)
                if (s.id == r.sectionId)
                    lowest = std::min (lowest, s.startTick);

            const auto notes = SongDocument::getNotesNode (track);
            for (int i = 0; i < notes.getNumChildren(); ++i)
                if (sectionIdOfNote (notes.getChild (i), sections) == r.sectionId)
                    lowest = std::min (lowest, (int) notes.getChild (i).getProperty (SongIDs::startTick));
        }
        return lowest == std::numeric_limits<int>::max() ? 0 : std::max (0, lowest);
    }
}

void TrackListComponent::ListContent::resized()
{
    int y = 0;
    for (auto* row : rows)
    {
        row->setBounds (0, y, getWidth(), TrackRowComponent::rowHeight);
        y += TrackRowComponent::rowHeight;
    }
}

TrackListComponent::TrackListComponent (SongDocument& document)
    : doc (document), sourceMidiNode (document.getSourceMidiNode())
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    horizontalBar.setAutoHide (true);
    horizontalBar.addListener (this);
    addAndMakeVisible (horizontalBar); // auto-hide still decides whether it shows

    sourceMidiNode.addListener (this);
    rebuild();
}

TrackListComponent::~TrackListComponent()
{
    // Cancel before removing the listener so a rebuild can't fire against a
    // component that is mid-destruction.
    cancelPendingUpdate();
    sourceMidiNode.removeListener (this);
    if (playback != nullptr)
        playback->removeListener (this);
    horizontalBar.removeListener (this);
}

void TrackListComponent::setPlayback (PlaybackController* controller)
{
    if (playback != nullptr)
        playback->removeListener (this);
    playback = controller;
    if (playback != nullptr)
        playback->addListener (this);

    if (playback == nullptr)
    {
        ruler.reset();
        overlay.reset();
        markerOverlay.reset();
    }
    else if (ruler == nullptr)
    {
        ruler = std::make_unique<TimelineRuler> ([this] (int x) { return (double) timelineView.tickForX (x - notePreviewOriginX()); });
        ruler->onSeek = [this] (double tick) { if (playback != nullptr) playback->seekToTick (tick); };
        ruler->setMarks ([this] (double tick) { return timelineView.xForTick ((int) tick) + notePreviewOriginX(); },
                         [this] { return rulerGridFromDocument (doc); });
        ruler->setContentLeft (notePreviewOriginX());
        ruler->setMarker ([this] { return playback != nullptr ? playback->getMarkerTick() : std::nullopt; });
        ruler->onSetMarker = [this] (double tick) { if (playback != nullptr) playback->setMarkerTick (tick); };
        ruler->onClearMarker = [this] { if (playback != nullptr) playback->clearMarker(); };
        addAndMakeVisible (*ruler);
        overlay = std::make_unique<PlayheadOverlay> (*playback, [this] (double tick) { return timelineView.xForTick ((int) tick); });
        addAndMakeVisible (*overlay);   // added after the viewport, so it draws on top
        markerOverlay = std::make_unique<MarkerOverlay> (*playback, [this] (double tick) { return timelineView.xForTick ((int) tick); });
        addAndMakeVisible (*markerOverlay);
    }
    rebuild();
    resized();
}

void TrackListComponent::refreshOverlay()
{
    if (overlay == nullptr)
        return;
    ++overlayRepaintCount;
    overlay->repaint();
    if (ruler != nullptr)
        ruler->repaint();
    if (markerOverlay != nullptr)
        markerOverlay->repaint();
}

void TrackListComponent::followPlayhead (bool playing)
{
    if (! playing || playback == nullptr)
        return;
    const double tick = playback->getPositionTicks();
    const int x = timelineView.xForTick ((int) tick);
    if (x >= 0 && x < previewWidth())
        return;

    // The same clamp syncHorizontalBar applies. If it leaves the offset where
    // it is (e.g. the playhead is at the very end of a fitted song), there is
    // nothing to flip: leave fitted mode and the rows alone.
    const double target = juce::jlimit (0.0, maxScrollOffset(), tick);
    if (std::abs (target - timelineView.getScrollOffsetTicks()) < 1.0e-9)
        return;
    timelineView.setScrollOffsetTicks (target); // page-flip: the playhead becomes the left edge
    timelineFitted = false;                      // do not let a resize refit fight the follow
    syncHorizontalBar();
    content.repaint();
}

void TrackListComponent::muteSoloChanged()
{
    if (playback == nullptr)
        return;
    for (auto* row : content.rows)
    {
        const auto id = row->getTrackId();
        row->setMuteSolo (playback->isMuted (id), playback->isSoloed (id), playback->isSilencedBySolo (id));
    }
}

void TrackListComponent::rebuild()
{
    // The strip holding the mouse is about to be destroyed, so no release will
    // arrive: drop any section drag uncommitted.
    gesture.reset();
    sectionView.drag.reset();
    content.rows.clear();

    for (int i = 0; i < doc.getNumTracks(); ++i)
    {
        auto* row = content.rows.add (new TrackRowComponent (doc.getTrack (i), i, timelineView));
        row->setGhostVisible (isTrackGhosted != nullptr && isTrackGhosted (row->getTrackId()));
        row->onTimelineClicked = [this] (int tick) { if (playback != nullptr) playback->setMarkerTick ((double) tick); };
        row->onTrackSelected = [this] (juce::int64 trackId, const juce::ModifierKeys& m) { selectTrack (trackId, m, false); };
        row->onStripSelected = [this] (juce::int64 trackId, const juce::ModifierKeys& m) { selectTrack (trackId, m, true); };
        row->onTrackDoubleClicked = [this] (juce::int64 trackId) { if (onTrackDoubleClicked) onTrackDoubleClicked (trackId); };
        row->setSectionView (&sectionView);
        row->onSectionPressed = [this] (juce::int64 trackId, const SectionHit& hit, int tick) { sectionPressed (trackId, hit, tick); };
        row->onSectionDragged = [this] (int tick) { sectionDragged (tick); };
        row->onSectionReleased = [this] (int tick) { sectionReleased (tick); };
        row->onGhostToggled = [this] (juce::int64 trackId, bool visible) { if (onGhostToggled) onGhostToggled (trackId, visible); };
        row->onMuteToggled = [this] (juce::int64 id, bool s) { if (playback != nullptr) playback->setMuted (id, s); };
        row->onSoloToggled = [this] (juce::int64 id, bool s) { if (playback != nullptr) playback->setSoloed (id, s); };
        if (playback != nullptr)
        {
            const auto id = row->getTrackId();
            row->setMuteSolo (playback->isMuted (id), playback->isSoloed (id), playback->isSilencedBySolo (id));
        }
        content.addAndMakeVisible (row);
    }

    // M5: the previously-selected track may have just disappeared (e.g. all
    // tracks removed, or SongDocument::removeTrack on this one specifically)
    // — clear the stale selection so it isn't reported as still live below.
    for (auto it = selectedTrackIds.begin(); it != selectedTrackIds.end();)
        it = doc.findTrackById (*it).isValid() ? std::next (it) : selectedTrackIds.erase (it);
    if (selectedTrackId != -1 && ! doc.findTrackById (selectedTrackId).isValid())
        selectedTrackId = -1;
    applySelectionToRows();

    // Sections that no longer exist drop out of the selection. A virtual default
    // section (id 0) that an edit materialised becomes the track's first section,
    // as the mutations resolve it, so it stays selected.
    std::set<SectionRef> stillSelected;
    for (const auto& r : sectionView.selected)
    {
        const auto sections = sectionsOf (doc.findTrackById (r.trackId));
        const auto id = r.sectionId;
        if (std::any_of (sections.begin(), sections.end(), [id] (const SectionRange& s) { return s.id == id; }))
            stillSelected.insert (r);
        else if (id == 0 && ! sections.empty())
            stillSelected.insert ({ r.trackId, sections.front().id });
    }
    sectionView.selected = std::move (stillSelected);
    content.repaint();

    content.setSize (contentWidth(), doc.getNumTracks() * TrackRowComponent::rowHeight);
    content.resized();
    syncHorizontalBar(); // the song may have got longer or shorter.
    repaint(); // M4: empty-state message visibility may have changed.
}

int TrackListComponent::documentEndTick() const
{
    int lastTick = 0;
    for (int t = 0; t < doc.getNumTracks(); ++t)
    {
        auto track = doc.getTrack (t);
        auto notes = SongDocument::getNotesNode (track);
        for (int n = 0; n < notes.getNumChildren(); ++n)
        {
            auto note = notes.getChild (n);
            const int endTick = (int) note.getProperty (SongIDs::startTick)
                               + (int) note.getProperty (SongIDs::durationTicks);
            lastTick = juce::jmax (lastTick, endTick);
        }
    }
    return lastTick;
}

int TrackListComponent::previewWidth() const
{
    return juce::jmax (0, contentWidth() - notePreviewOriginX());
}

void TrackListComponent::fitTimelineToDocument()
{
    timelineView.fitToWidth ((double) documentEndTick(), previewWidth());
    timelineFitted = true;
    syncHorizontalBar();
    content.repaint();
}

double TrackListComponent::maxScrollOffset() const
{
    const double visibleTicks = (double) previewWidth() / timelineView.getPixelsPerTick();
    return juce::jmax (0.0, (double) documentEndTick() - visibleTicks);
}

void TrackListComponent::syncHorizontalBar()
{
    const double endTick = (double) documentEndTick();
    const double visibleTicks = (double) previewWidth() / timelineView.getPixelsPerTick();
    const double maxOffset = maxScrollOffset();

    if (timelineView.getScrollOffsetTicks() > maxOffset)
        timelineView.setScrollOffsetTicks (maxOffset);

    // Never let the total range be shorter than the visible span, so a song
    // that fits (or an empty document) reads as "fully visible" and the bar
    // auto-hides instead of showing a thumb that can't move.
    horizontalBar.setRangeLimits (0.0, juce::jmax (endTick, visibleTicks), juce::dontSendNotification);
    horizontalBar.setCurrentRange (timelineView.getScrollOffsetTicks(), visibleTicks, juce::dontSendNotification);
    horizontalBar.setSingleStepSize (visibleTicks / 20.0);
    refreshOverlay();
}

void TrackListComponent::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    timelineView.setScrollOffsetTicks (newRangeStart);
    content.repaint();
    refreshOverlay();
}

void TrackListComponent::clearSelection()
{
    selectTrack (-1, {}, false);
}

void TrackListComponent::selectTrack (juce::int64 trackId, const juce::ModifierKeys& mods, bool fromStrip)
{
    const auto isConductorId = [this] (juce::int64 id)
    {
        const auto t = doc.findTrackById (id);
        return ! t.isValid() || (bool) t.getProperty (SongIDs::isConductor, false);
    };

    if (trackId == -1)
    {
        selectedTrackIds.clear();
        selectedTrackId = -1;
    }
    else if (mods.isShiftDown() && selectedTrackId != -1)
    {
        int from = -1, to = -1;
        for (int i = 0; i < content.rows.size(); ++i)
        {
            if (content.rows[i]->getTrackId() == selectedTrackId) from = i;
            if (content.rows[i]->getTrackId() == trackId) to = i;
        }
        // A plain Shift-click replaces the selection with the range; Ctrl+Shift extends it.
        if (! (mods.isCtrlDown() || mods.isCommandDown()))
            selectedTrackIds.clear();
        if (from >= 0 && to >= 0)
            for (int i = std::min (from, to); i <= std::max (from, to); ++i)
                if (! isConductorId (content.rows[i]->getTrackId()))
                    selectedTrackIds.insert (content.rows[i]->getTrackId());
    }
    else if (mods.isCtrlDown() || mods.isCommandDown())
    {
        if (selectedTrackIds.erase (trackId) == 0 && ! isConductorId (trackId))
            selectedTrackIds.insert (trackId);
        selectedTrackId = trackId;
    }
    else if (fromStrip && selectedTrackIds.size() > 1 && selectedTrackIds.count (trackId) > 0)
    {
        selectedTrackId = trackId; // keep the multi-selection
    }
    else
    {
        selectedTrackIds.clear();
        if (! isConductorId (trackId))
            selectedTrackIds.insert (trackId);
        selectedTrackId = trackId;
    }
    applySelectionToRows();
}

void TrackListComponent::applySelectionToRows()
{
    // A conductor is never in selectedTrackIds but still highlights when it is the
    // clicked row; any other row highlights only while it is in the set (so a
    // Ctrl-toggled-off anchor is not left looking selected).
    for (auto* row : content.rows)
    {
        const auto id = row->getTrackId();
        const auto track = doc.findTrackById (id);
        const bool isConductorAnchor = id == selectedTrackId && track.isValid() && (bool) track.getProperty (SongIDs::isConductor, false);
        row->setSelected (selectedTrackIds.count (id) > 0 || isConductorAnchor);
    }
}

void TrackListComponent::selectAllTracks()
{
    selectedTrackIds.clear();
    for (int i = 0; i < doc.getNumTracks(); ++i)
    {
        const auto t = doc.getTrack (i);
        if (! (bool) t.getProperty (SongIDs::isConductor, false))
            selectedTrackIds.insert ((juce::int64) t.getProperty (SongIDs::trackId));
    }
    applySelectionToRows();
}

bool TrackListComponent::splitSections (std::optional<int> pointerTick, juce::int64 pointerTrackId)
{
    std::optional<int> tick = pointerTick;
    if (! tick && playback != nullptr)
        if (const auto marker = playback->getMarkerTick())
            tick = (int) std::llround (*marker);
    if (! tick)
        return false;

    std::vector<juce::int64> trackIds (selectedTrackIds.begin(), selectedTrackIds.end());
    if (trackIds.empty() && pointerTrackId != -1)
        trackIds.push_back (pointerTrackId);

    // splitAt ignores tracks it cannot split; ask first so "did anything" is exact.
    const auto splittable = std::any_of (trackIds.begin(), trackIds.end(), [&] (juce::int64 id)
    {
        const auto track = doc.findTrackById (id);
        if (! track.isValid() || (bool) track.getProperty (SongIDs::isConductor, false))
            return false;
        const auto sections = sectionsOf (track);
        return std::any_of (sections.begin(), sections.end(),
                            [&] (const auto& s) { return s.startTick < *tick && *tick < s.endTick; });
    });
    if (! splittable)
        return false;

    splitAt (doc, trackIds, *tick);
    return true;
}

bool TrackListComponent::splitAtPointer()
{
    // The tick under the pointer, but only when it is over a track's note strip.
    std::optional<int> tick;
    juce::int64 trackId = -1;
    if (isMouseOver (true))
    {
        const auto inContent = content.getLocalPoint (this, getMouseXYRelative());
        for (auto* row : content.rows)
        {
            if (! row->getBounds().contains (inContent))
                continue;
            const auto inRow = inContent - row->getPosition();
            const auto strip = row->notePreviewForTesting().getBounds();
            if (strip.contains (inRow) && row->canDrag())
            {
                tick = timelineView.tickForX (inRow.x - strip.getX());
                trackId = row->getTrackId();
            }
        }
    }
    return splitSections (tick, trackId);
}

bool TrackListComponent::deleteSelectedSections()
{
    if (sectionView.selected.empty())
        return false;
    deleteSections (doc, std::vector<SectionRef> (sectionView.selected.begin(), sectionView.selected.end()));
    sectionView.selected.clear();
    content.repaint();
    return true;
}

void TrackListComponent::sectionPressed (juce::int64 trackId, const SectionHit& hit, int tick)
{
    gesture.reset();
    sectionView.drag.reset();
    if (hit.zone == SectionZone::None)
    {
        sectionView.selected.clear();
        content.repaint();
        return;
    }

    // Track toggling/range selection already ran in selectTrack (the preview calls
    // onNonToggleClick first), so selectedTrackIds is up to date here.
    const auto refs = withCompanions (doc, selectedTrackIds, { trackId, hit.sectionId });
    sectionView.selected = std::set<SectionRef> (refs.begin(), refs.end());

    int edgeTick = tick;
    for (const auto& s : sectionsOf (doc.findTrackById (trackId)))
        if (s.id == hit.sectionId)
            edgeTick = hit.zone == SectionZone::LeftEdge ? s.startTick : s.endTick;
    gesture = SectionGesture { hit.zone, refs, tick, earliestStart (doc, refs), edgeTick };
    content.repaint();
}

void TrackListComponent::sectionDragged (int tick)
{
    if (! gesture)
        return;
    sectionView.drag = previewFor (*gesture, tick);
    content.repaint();
}

void TrackListComponent::sectionReleased (int tick)
{
    if (! gesture)
        return;
    const auto g = *gesture;
    gesture.reset();
    sectionView.drag.reset();
    content.repaint();

    // A press released where it started is a click: it only selects.
    if (tick == g.pressTick)
        return;

    // Every edge moves by the same delta (never to one shared tick), so a
    // companion with a different end grows or shrinks by what the user dragged.
    const auto d = previewFor (g, tick);
    if (d.kind == SectionDragPreview::Kind::Move)
        moveSections (doc, g.refs, d.deltaTicks);
    else
        resizeSectionsBy (doc, g.refs, d.kind == SectionDragPreview::Kind::ResizeLeft ? SectionEdge::Left : SectionEdge::Right,
                          d.deltaTicks);
}

SectionDragPreview TrackListComponent::previewFor (const SectionGesture& g, int tick)
{
    // The move is clamped here so the preview shows what moveSections will do; an
    // edge follows the pointer's movement from where it was grabbed, so grabbing
    // it a few pixels off does not make it jump (resizeSectionsBy clamps each
    // section to at least one tick, and paintSections mirrors that).
    const int delta = tick - g.pressTick;
    if (g.zone == SectionZone::Body)
        return { SectionDragPreview::Kind::Move, std::max (delta, -g.minStart), 0 };
    return { g.zone == SectionZone::LeftEdge ? SectionDragPreview::Kind::ResizeLeft : SectionDragPreview::Kind::ResizeRight,
             delta, g.edgeTick + delta };
}

int TrackListComponent::contentWidth() const
{
    return viewport.getWidth() - viewport.getScrollBarThickness();
}

int TrackListComponent::notePreviewOriginX() const
{
    // Mirrors TrackRowComponent::resized()'s own jmin, so the two agree when
    // the list is narrower than the fixed info column.
    return juce::jmin (TrackRowComponent::trackInfoWidth, contentWidth());
}

void TrackListComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    constexpr float pixelsPerNotch = 50.0f;

    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        // Ctrl/Cmd+wheel: the track rows' vertical scroll. Wheel up = towards the first row.
        viewport.setViewPosition (viewport.getViewPositionX(),
                                  viewport.getViewPositionY() - juce::roundToInt (wheel.deltaY * pixelsPerNotch));
        return;
    }

    if (e.mods.isShiftDown())
        timelineView.scrollByPixels (juce::roundToInt ((-wheel.deltaX - wheel.deltaY) * pixelsPerNotch));
    else
        zoomAboutMarker (wheel.deltaY > 0.0f ? 1.1 : 1.0 / 1.1);

    syncHorizontalBar();
    content.repaint();
}

void TrackListComponent::zoomAboutMarker (double factor)
{
    // The start marker (when set) is first brought to the middle of the
    // preview strip, so zooming closes in on the spot the user chose; with no
    // marker the zoom is about whatever is already at the middle.
    const int centreX = previewWidth() / 2;
    if (playback != nullptr)
        if (const auto marker = playback->getMarkerTick())
            timelineView.setScrollOffsetTicks (*marker - (double) centreX / timelineView.getPixelsPerTick());

    timelineView.zoomBy (factor, centreX);
    timelineFitted = false;
}

void TrackListComponent::resized()
{
    // The horizontal bar's strip is always reserved (even while it
    // auto-hides) so rows don't jump when it appears.
    auto area = getLocalBounds();
    if (ruler != nullptr)
    {
        ruler->setBounds (area.removeFromTop (TimelineRuler::height));
        ruler->setContentLeft (notePreviewOriginX());
    }
    auto barStrip = area.removeFromBottom (viewport.getScrollBarThickness());
    viewport.setBounds (area);
    if (overlay != nullptr)
        overlay->setBounds (viewport.getBounds().withTrimmedLeft (notePreviewOriginX()).withWidth (previewWidth()));
    if (markerOverlay != nullptr)
        markerOverlay->setBounds (overlay->getBounds());

    horizontalBar.setBounds (barStrip.withLeft (notePreviewOriginX())
                                     .withWidth (previewWidth()));

    content.setSize (contentWidth(), doc.getNumTracks() * TrackRowComponent::rowHeight);
    content.resized();

    if (timelineFitted)
        timelineView.fitToWidth ((double) documentEndTick(), previewWidth());

    syncHorizontalBar();
}

void TrackListComponent::paint (juce::Graphics& g)
{
    // Slightly darker than the shared background, matching the mockup's
    // MIDI-track-list panel (#262626 there vs. #2b2b2b for the background).
    g.fillAll (juce::Colour (SongsmithColours::background).darker (0.15f));

    // M4: first-launch/empty-document affordance.
    if (doc.getNumTracks() <= 1) // only the conductor: nothing imported yet
    {
        g.setColour (juce::Colour (SongsmithColours::textMuted));
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawFittedText (juce::String::fromUTF8 (
                               "No MIDI loaded \xe2\x80\x94 File \xe2\x86\x92 Import \xe2\x96\xb8 MIDI\xe2\x80\xa6 or drop a .mid here"),
                           getLocalBounds().reduced (12), juce::Justification::centred, 4);
    }
}

} // namespace lotro

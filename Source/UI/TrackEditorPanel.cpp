#include "TrackEditorPanel.h"
#include "EditorMenuModel.h"
#include "MenuPopup.h"
#include "Playback/PreviewRequest.h"
#include "SongsmithColours.h"

namespace lotro
{
    namespace
    {
        // The editor Edit item a Ctrl/Cmd shortcut stands for; 0 for any other key.
        int editCommandForKey (const juce::KeyPress& key)
        {
            const auto cmd = juce::ModifierKeys::commandModifier;
            const auto cmdShift = juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier;
            const std::pair<juce::KeyPress, int> shortcuts[] {
                { juce::KeyPress ('z', cmd, 0), EditorEditUndo },
                { juce::KeyPress ('y', cmd, 0), EditorEditRedo },
                { juce::KeyPress ('z', cmdShift, 0), EditorEditRedo },
                { juce::KeyPress ('x', cmd, 0), EditorEditCut },
                { juce::KeyPress ('c', cmd, 0), EditorEditCopy },
                { juce::KeyPress ('v', cmd, 0), EditorEditPaste },
                { juce::KeyPress ('v', cmdShift, 0), EditorEditPasteAtOrigin },
                { juce::KeyPress ('a', cmd, 0), EditorEditSelectAll },
                // Plain letters only; Ctrl/Cmd+S stays the main window's Save.
                { juce::KeyPress ('i'), EditorEditInsertNote },
                { juce::KeyPress ('s'), EditorEditSplitNotes },
                { juce::KeyPress ('j'), EditorEditJoinNotes },
                { juce::KeyPress ('l'), EditorEditLegato },
                { juce::KeyPress ('l', juce::ModifierKeys::shiftModifier, 0), EditorEditLegatoToNextNote } };
            for (const auto& [shortcut, command] : shortcuts)
                if (key == shortcut)
                    return command;
            return 0;
        }
    }

    TrackEditorPanel::TrackEditorPanel (SongDocument& document)
        : doc (document),
          roll (PianoRollComponent::Role::Source, &doc)
    {
        setWantsKeyboardFocus (true);
        roll.setSnapToGrid (snapToGrid);
        roll.setPitchFilter ([this] (int pitch)
        {
            const auto key = currentSnapKey();
            return ! (snapToKey && key.has_value()) || isPitchAllowed (*key, pitch);
        });
        roll.setNoteChangeListener ([this] (NoteChange change, const std::vector<juce::ValueTree>& notes)
        {
            if (playback == nullptr || currentTrackId == -1 || ! previewWanted (notePreview, change))
                return;
            if (const auto request = makePreviewRequest (currentTrackId, notes))
                playback->previewNotes (*request);
        });
        roll.onUnhandledKey = [this] (const juce::KeyPress& key) { return navigateKeyPressed (key); };
        addAndMakeVisible (menuBar);
        addAndMakeVisible (roll);
        addAndMakeVisible (bottomBar);
        bottomBar.onSwingChanged = [this] (int percent) { roll.setSwing (percent); };
        refreshSwingControl();
    }

    TrackEditorPanel::~TrackEditorPanel()
    {
        menuBar.setModel (nullptr);
    }

    void TrackEditorPanel::resized()
    {
        auto area = getLocalBounds();
        menuBar.setBounds (area.removeFromTop (menuBarHeight));
        bottomBar.setBounds (area.removeFromBottom (EditorBottomBar::height));
        if (content != nullptr)
            content->setBounds (area);
        else
            roll.setBounds (area);
    }

    juce::StringArray TrackEditorPanel::MenuModel::getMenuBarNames()
    {
        juce::StringArray names;
        for (const auto& m : editorMenus ({}))
            names.add (m.name);
        return names;
    }

    juce::PopupMenu TrackEditorPanel::MenuModel::getMenuForIndex (int topLevelMenuIndex, const juce::String&)
    {
        EditorMenuState s;
        s.hasTrack = owner.currentTrackId != -1;
        s.canUndo = owner.doc.canUndo();
        s.canRedo = owner.doc.canRedo();
        s.hasSelection = s.hasTrack && ! owner.roll.selectedNotes().empty();
        s.hasNotes = s.hasTrack && owner.roll.hasNotes();
        s.canPaste = owner.canPaste();
        s.canPasteAtOrigin = s.hasTrack && ! owner.clipboard.empty();
        s.canInsertNote = owner.canInsertNote();
        s.canSplitNotes = owner.canSplitNotes();
        s.canJoinNotes = s.hasTrack && owner.roll.canJoinNotes();
        s.canLegato = s.hasTrack && owner.roll.canLegatoNotes (LegatoTarget::samePitch);
        s.canLegatoToNextNote = s.hasTrack && owner.roll.canLegatoNotes (LegatoTarget::anyPitch);
        s.snapToGrid = owner.snapToGrid;
        s.canStepMarker = owner.canStepMarker();
        s.canStepPitch = owner.currentTrackId != -1 && owner.roll.getActivePitch() >= 0;
        s.canSelectNearestNote = owner.canSelectNearestNote();
        s.canAddNearestNote = s.canSelectNearestNote && owner.roll.hasUnselectedNotes();
        s.canSelectPreviousNote = s.canSelectNearestNote && owner.roll.hasSteppedNote (owner.cursorTick(), -1);
        s.canSelectNextNote = s.canSelectNearestNote && owner.roll.hasSteppedNote (owner.cursorTick(), +1);
        s.canSelectPreviousSamePitch = s.canSelectNearestNote && owner.roll.hasSamePitchNote (-1);
        s.canSelectNextSamePitch = s.canSelectNearestNote && owner.roll.hasSamePitchNote (+1);
        s.canSelectAllSamePitch = owner.currentTrackId != -1 && owner.roll.canSelectAllOfSamePitch();
        s.canAddPreviousNote = s.canSelectNearestNote && owner.roll.hasExtendableNote (-1);
        s.canAddNextNote = s.canSelectNearestNote && owner.roll.hasExtendableNote (+1);
        s.canMoveCursorToSelectionStart = s.canSelectNearestNote && owner.roll.selectionStart().has_value();
        s.canSelectByKey = owner.currentTrackId != -1 && owner.currentSnapKey().has_value();
        s.snapToKey = owner.snapToKey;
        s.notePreview = owner.notePreview;
        s.gridLength = owner.getGridLength();
        s.gridDivision = owner.getGridDivision();
        s.noteLength = owner.getNoteLength();
        s.noteDivision = owner.getNoteDivision();
        s.keyboardLabels = owner.getKeyboardLabels();
        s.noteShape = owner.getNoteShape();
        s.noteColourMode = owner.getNoteColourMode();
        s.noteLabels = owner.getNoteLabels();
        if (const auto key = owner.currentSnapKey())
        {
            s.snapRootIndex = key->rootIndex;
            s.snapScaleIndex = (int) key->scale;
        }
        const auto menus = editorMenus (s);
        juce::PopupMenu m;
        if (topLevelMenuIndex >= 0 && topLevelMenuIndex < (int) menus.size())
            addMenuItems (m, menus[(std::size_t) topLevelMenuIndex].items);
        return m;
    }

    void TrackEditorPanel::MenuModel::menuItemSelected (int menuItemID, int)
    {
        // A command from the editor's own bar puts the keyboard in the editor, so a
        // following Delete acts on its notes rather than the main page's sections.
        if (owner.isShowing())
            owner.grabKeyboardFocus();
        owner.invokeMenuCommand (menuItemID);
    }

    void TrackEditorPanel::invokeMenuCommand (int commandId)
    {
        switch (commandId)
        {
            case EditorFileExportTrackMidi:
                if (currentTrackId != -1 && onExportTrackMidi)
                    onExportTrackMidi (currentTrackId);
                return;
            case EditorFileClose:
                if (onCloseRequested)
                    onCloseRequested();
                return;
            case EditorEditUndo:
                roll.undo();
                return;
            case EditorEditRedo:
                roll.redo();
                return;
            case EditorEditCut:
                if (copySelection())
                    roll.deleteSelectedNotes();
                return;
            case EditorEditCopy:
                copySelection();
                return;
            case EditorEditPaste:
                if (canPaste())
                    roll.pasteNotes (clipboard, cursorTickRounded());
                return;
            case EditorEditPasteAtOrigin:
                if (currentTrackId != -1)
                    roll.pasteNotes (clipboard, clipboard.originTick());
                return;
            case EditorEditSelectAll:
                if (currentTrackId != -1)
                    roll.selectAllNotes();
                return;
            case EditorEditDelete:
                roll.deleteSelectedNotes();
                return;
            case EditorEditInsertNote:
                if (canInsertNote())
                    roll.insertNote (cursorTickRounded());
                return;
            case EditorEditSplitNotes:
                if (canSplitNotes())
                    roll.splitNotes (cursorTickRounded());
                return;
            case EditorEditJoinNotes:
                if (currentTrackId != -1)
                    roll.joinNotes();
                return;
            case EditorEditLegato:
                if (currentTrackId != -1)
                    roll.legatoNotes (LegatoTarget::samePitch);
                return;
            case EditorEditLegatoToNextNote:
                if (currentTrackId != -1)
                    roll.legatoNotes (LegatoTarget::anyPitch);
                return;
            case EditorNavigateMarkerLeft:
                stepMarker (-1);
                return;
            case EditorNavigateMarkerRight:
                stepMarker (+1);
                return;
            case EditorNavigatePitchUp:
                roll.stepActivePitch (+1);
                return;
            case EditorNavigatePitchDown:
                roll.stepActivePitch (-1);
                return;
            case EditorNavigateSelectNearestNote:
                if (canSelectNearestNote())
                    roll.selectNoteNearest (cursorTick());
                return;
            case EditorNavigateAddNearestNote:
                if (canSelectNearestNote())
                    roll.addNoteNearestToSelection (cursorTick());
                return;
            case EditorNavigateSelectPreviousNote:
                selectSteppedNote (-1);
                return;
            case EditorNavigateSelectNextNote:
                selectSteppedNote (+1);
                return;
            case EditorNavigateSelectPreviousSamePitch:
                selectSamePitchNote (-1);
                return;
            case EditorNavigateSelectNextSamePitch:
                selectSamePitchNote (+1);
                return;
            case EditorNavigateSelectAllSamePitch:
                if (currentTrackId != -1)
                    roll.selectAllOfSamePitch();
                return;
            case EditorViewNotesInKey:
            case EditorViewNotesOutOfKey:
                if (const auto key = currentSnapKey(); key.has_value() && currentTrackId != -1)
                {
                    const bool inKey = commandId == EditorViewNotesInKey;
                    roll.selectNotesWithPitch ([&] (int pitch) { return isPitchAllowed (*key, pitch) == inKey; });
                }
                return;
            case EditorNavigateAddPreviousNote:
                addSteppedNote (-1);
                return;
            case EditorNavigateAddNextNote:
                addSteppedNote (+1);
                return;
            case EditorNavigateCursorToSelectionStart:
                moveCursorToSelectionStart();
                return;
            case EditorOptionsSnapToGrid:
                setSnapToGrid (! snapToGrid);
                if (onSnapToGridChanged)
                    onSnapToGridChanged (snapToGrid);
                return;
            case EditorOptionsPreviewOnInsert:
            case EditorOptionsPreviewOnEdit:
            case EditorOptionsPreviewOnVelocity:
            {
                auto& flag = commandId == EditorOptionsPreviewOnInsert ? notePreview.onInsert
                           : commandId == EditorOptionsPreviewOnEdit   ? notePreview.onEdit
                                                                       : notePreview.onVelocity;
                flag = ! flag;
                if (onNotePreviewChanged)
                    onNotePreviewChanged (notePreview);
                return;
            }
            case EditorViewPianoNotes:
            case EditorViewMidiNumbers:
            {
                const auto labels = commandId == EditorViewMidiNumbers ? KeyboardLabels::midiNumbers
                                                                       : KeyboardLabels::pianoNotes;
                if (labels == getKeyboardLabels())
                    return;
                setKeyboardLabels (labels);
                if (onKeyboardLabelsChanged)
                    onKeyboardLabelsChanged (labels);
                return;
            }
            case EditorViewNoteRectangle:
            case EditorViewNoteDiamond:
            case EditorViewNoteDiamondLength:
            {
                const auto shape = commandId == EditorViewNoteDiamond       ? NoteShape::diamond
                                 : commandId == EditorViewNoteDiamondLength ? NoteShape::diamondLength
                                                                            : NoteShape::rectangle;
                if (shape == getNoteShape())
                    return;
                setNoteShape (shape);
                if (onNoteShapeChanged)
                    onNoteShapeChanged (shape);
                return;
            }
            case EditorViewColourTrack:
            case EditorViewColourVelocity:
            case EditorViewColourChannel:
            case EditorViewColourPitch:
            {
                const auto mode = commandId == EditorViewColourVelocity ? NoteColourMode::velocity
                                : commandId == EditorViewColourChannel  ? NoteColourMode::channel
                                : commandId == EditorViewColourPitch    ? NoteColourMode::pitch
                                                                        : NoteColourMode::track;
                if (mode == getNoteColourMode())
                    return;
                setNoteColourMode (mode);
                if (onNoteColourModeChanged)
                    onNoteColourModeChanged (mode);
                return;
            }
            case EditorViewNoteNames:
            case EditorViewNoteVelocities:
            {
                auto labels = getNoteLabels();
                if (commandId == EditorViewNoteNames)
                    labels.names = ! labels.names;
                else
                    labels.velocities = ! labels.velocities;
                setNoteLabels (labels);
                if (onNoteLabelsChanged)
                    onNoteLabelsChanged (labels);
                return;
            }
            case EditorOptionsSnapToKeyEnabled:
                if (currentSnapKey().has_value())
                    snapToKey = ! snapToKey;
                return;
            default:
                if (isEditorSnapRootCommand (commandId))
                {
                    const auto current = currentSnapKey();
                    pickedSnapKey = SnapKey { commandId - EditorSnapRootBase, current ? current->scale : SnapScale::major };
                }
                else if (isEditorSnapScaleCommand (commandId))
                {
                    if (const auto current = currentSnapKey())
                        pickedSnapKey = SnapKey { current->rootIndex, (SnapScale) (commandId - EditorSnapScaleBase) };
                }
                else if (isEditorGridLengthCommand (commandId))
                    setGridLength ((GridLength) (commandId - EditorGridLengthBase));
                else if (isEditorGridDivisionCommand (commandId))
                    setGridDivision ((GridDivision) (commandId - EditorGridDivisionBase));
                else if (isEditorNoteLengthCommand (commandId))
                    setNoteLength ((GridLength) (commandId - EditorNoteLengthBase));
                else if (isEditorNoteDivisionCommand (commandId))
                    setNoteDivision ((GridDivision) (commandId - EditorNoteDivisionBase));
                return;
        }
    }

    std::optional<SnapKey> TrackEditorPanel::currentSnapKey() const
    {
        if (pickedSnapKey.has_value())
            return pickedSnapKey;
        if (const auto songKey = doc.getSongKey())
            return snapKeyForSongKey (*songKey);
        return std::nullopt;
    }

    void TrackEditorPanel::paintOverChildren (juce::Graphics& g)
    {
        if (currentTrackId != -1)
            return;
        g.setColour (juce::Colour (SongsmithColours::text).withAlpha (0.6f));
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("Double-click a track to edit it",
                    getLocalArea (&roll, roll.getLocalBounds()), juce::Justification::centred);
    }

    TrackEditorPanel::Content::Content (PianoRollComponent& rollIn, PlaybackController& controller, SongDocument& document)
        : strip (controller, document),
          ruler ([&rollIn] (int x) { return rollIn.tickForXInComponent (x); }),
          roll (rollIn)
    {
        // The ruler spans the roll's width, so its local x is the roll's local x.
        // Clicks over the keyboard gutter map to ticks before the visible start
        // (and clamp at 0 in the ruler).
        ruler.onSeek = [&controller] (double tick) { controller.seekToTick (tick); };
        ruler.setMarks ([&rollIn] (double tick) { return rollIn.xForTickInComponent (tick); },
                        [&controller] { return rulerGridFromDocument (controller.document()); });
        ruler.setContentLeft (rollIn.getGutterWidth());
        ruler.setMarker ([&controller] { return controller.getMarkerTick(); });
        // The roll holds the panel's Snap to Grid state, so a ruler click snaps as a roll click does
        // (and Alt frees it the same way).
        ruler.onSetMarker = [&controller, &rollIn] (double tick, bool unsnapped)
        {
            controller.setMarkerTick (rollIn.getSnapToGrid() && ! unsnapped ? snapToEditorGrid (rollIn.getEditorGrid(), tick) : tick);
        };
        ruler.onClearMarker = [&controller] { controller.clearMarker(); };
        ruler.setLoopRange ([&controller] { return controller.getLoopRange(); });
        ruler.setPlayhead ([&controller] { return std::optional<double> (controller.getPositionTicks()); });
        // The press records the OTHER edge; every drag step is anchored on it, so an edge
        // dragged past the other one flips the pair instead of jumping.
        ruler.onLoopEdgePressed = [this, &controller] (bool isStart)
        {
            loopEdgeAnchor.reset();
            if (const auto range = controller.getLoopRange())
                loopEdgeAnchor = isStart ? range->endTick : range->startTick;
        };
        ruler.onLoopEdgeDragged = [this, &controller, &rollIn] (bool, double tick)
        {
            // Nothing to move once the range is gone (cleared mid-drag) or if the press found none.
            if (! loopEdgeAnchor.has_value() || ! controller.getLoopRange().has_value())
                return;
            // The ruler reports no modifiers; Alt is read from the live keyboard state.
            const bool unsnapped = juce::ModifierKeys::getCurrentModifiers().isAltDown();
            controller.setLoopRange (*loopEdgeAnchor, snapLoopTick (tick, rollIn.getEditorGrid(), unsnapped));
        };
        ruler.onClearLoop = [this, &controller]
        {
            loopEdgeAnchor.reset();
            controller.clearLoopRange();
        };
        rollIn.onViewChanged = [this] { ruler.repaint(); };
        rollIn.onPlayheadMoved = [this] { ruler.playheadMoved(); };
        // The blank corner over the keyboard gutter names the hovered row, in either label mode.
        ruler.setCornerLabel ([&rollIn]
        {
            const int pitch = rollIn.getHoveredPitch();
            return pitch < 0 ? juce::String() : juce::String (pitch) + " " + PianoRollGeometry::noteName (pitch);
        });
        rollIn.onHoveredPitchChanged = [this] { ruler.cornerLabelChanged(); };
        addAndMakeVisible (strip);
        addAndMakeVisible (ruler);
        addAndMakeVisible (roll);
    }

    TrackEditorPanel::Content::~Content()
    {
        roll.onViewChanged = nullptr;
        roll.onPlayheadMoved = nullptr;
        roll.onHoveredPitchChanged = nullptr;
    }

    void TrackEditorPanel::Content::resized()
    {
        auto area = getLocalBounds();
        strip.setBounds (area.removeFromTop (TransportStrip::height));
        ruler.setBounds (area.removeFromTop (TimelineRuler::height));
        roll.setBounds (area);
    }

    void TrackEditorPanel::setStripButtonsHost (StripButtonsHost* host)
    {
        stripButtonsHost = host;
        if (content != nullptr)
            content->strip.setButtonsHost (host);
    }

    void TrackEditorPanel::setPlayback (PlaybackController* controller)
    {
        if (controller == nullptr || content != nullptr)
            return;

        playback = controller;
        roll.setPlayback (controller);
        content = std::make_unique<Content> (roll, *controller, doc);   // re-parents the roll
        content->strip.setButtonsHost (stripButtonsHost);
        addAndMakeVisible (*content);
        resized();
    }

    bool TrackEditorPanel::canStepMarker() const
    {
        return currentTrackId != -1 && playback != nullptr && roll.getGridTicks() > 0;
    }

    double TrackEditorPanel::cursorTick() const
    {
        return playback->getMarkerTick().value_or (playback->getPositionTicks());
    }

    int TrackEditorPanel::cursorTickRounded() const
    {
        return std::max (0, juce::roundToInt (cursorTick()));
    }

    bool TrackEditorPanel::canInsertNote() const
    {
        return currentTrackId != -1 && playback != nullptr && roll.canInsertNote (cursorTickRounded());
    }

    bool TrackEditorPanel::canSplitNotes() const
    {
        return currentTrackId != -1 && playback != nullptr && roll.canSplitNotes (cursorTickRounded());
    }

    bool TrackEditorPanel::canSelectNearestNote() const
    {
        return currentTrackId != -1 && playback != nullptr && roll.hasNotes();
    }

    bool TrackEditorPanel::selectSteppedNote (int direction)
    {
        if (! canSelectNearestNote())
            return false;
        const auto start = roll.selectSteppedNote (cursorTick(), direction);
        if (! start.has_value())
            return false;
        // The note's own start, not snapped: the point is to land on the note.
        playback->setMarkerTick ((double) *start);
        return true;
    }

    bool TrackEditorPanel::selectSamePitchNote (int direction)
    {
        if (! canSelectNearestNote())
            return false;
        const auto start = roll.selectSamePitchNote (direction);
        if (! start.has_value())
            return false;
        // As selectSteppedNote: the note's own start, not snapped.
        playback->setMarkerTick ((double) *start);
        return true;
    }

    bool TrackEditorPanel::addSteppedNote (int direction)
    {
        if (! canSelectNearestNote())
            return false;
        const auto start = roll.addSteppedNoteToSelection (direction);
        if (! start.has_value())
            return false;
        // As selectSteppedNote: the note's own start, not snapped.
        playback->setMarkerTick ((double) *start);
        return true;
    }

    bool TrackEditorPanel::moveCursorToSelectionStart()
    {
        if (! canSelectNearestNote())
            return false;
        const auto start = roll.selectionStart();
        if (! start.has_value())
            return false;
        playback->setMarkerTick ((double) *start);
        roll.scrollTickIntoView ((double) *start);
        return true;
    }

    bool TrackEditorPanel::stepMarker (int direction)
    {
        if (! canStepMarker())
            return false;
        const double from = cursorTick();
        playback->setMarkerTick (stepEditorGrid (roll.getEditorGrid(), from, direction));
        return true;
    }

    bool TrackEditorPanel::navigateKeyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress (juce::KeyPress::leftKey))
            return stepMarker (-1);
        if (key == juce::KeyPress (juce::KeyPress::rightKey))
            return stepMarker (+1);
        if (key == juce::KeyPress (juce::KeyPress::upKey))
            return roll.stepActivePitch (+1);
        if (key == juce::KeyPress (juce::KeyPress::downKey))
            return roll.stepActivePitch (-1);
        return false;
    }

    bool TrackEditorPanel::copySelection()
    {
        const auto selected = roll.selectedNotes();
        if (currentTrackId == -1 || selected.empty())
            return false;
        clipboard = copyNotes (selected);
        return true;
    }

    bool TrackEditorPanel::canPaste() const
    {
        return currentTrackId != -1 && playback != nullptr && ! clipboard.empty();
    }

    bool TrackEditorPanel::keyPressed (const juce::KeyPress& key)
    {
        if (navigateKeyPressed (key))
            return true;
        // The Edit menu's shortcuts, claimed even when they do nothing so the main
        // page never selects or undoes behind the editor (a focused canvas has
        // already taken Ctrl+Z / Ctrl+Y itself).
        if (const int command = editCommandForKey (key); command != 0)
        {
            invokeMenuCommand (command);
            return true;
        }
        // The roll's SourceRollEditor only claims Delete/Backspace, so Space
        // reaches the panel.
        if (key == juce::KeyPress::spaceKey && playback != nullptr)
        {
            playback->togglePlayPause();
            return true;
        }
        // Focus on the panel rather than the canvas (as opening the editor or using
        // its menu bar leaves it): Delete still means the editor's notes, and is
        // claimed even with none selected so the main page never deletes sections.
        if (key == juce::KeyPress (juce::KeyPress::deleteKey) || key == juce::KeyPress (juce::KeyPress::backspaceKey))
        {
            roll.deleteSelectedNotes();
            return true;
        }
        return false;
    }

    void TrackEditorPanel::setTrack (juce::ValueTree trackNode)
    {
        if (! trackNode.isValid())
            return;

        currentTrackId = (juce::int64) trackNode.getProperty (SongIDs::trackId);
        currentNoteSource = std::make_unique<SourceTrackNoteSource> (trackNode);

        const int ticksPerQuarter = (int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter, 480);
        roll.setNoteSource (currentNoteSource.get(), ticksPerQuarter, doc.getMeterMapNode());
        roll.setEditableTrack (trackNode);

        refreshSwingControl();
        titleChanged();
        if (isShowing())
            grabKeyboardFocus();
    }

    void TrackEditorPanel::clearTrack()
    {
        roll.setEditableTrack ({});
        roll.setNoteSource (nullptr, 480, {});
        roll.setGhostTracks ({});
        currentNoteSource.reset();
        currentTrackId = -1;
        pickedSnapKey.reset();
        clipboard = {};
        refreshSwingControl();
        titleChanged();
        repaint();
    }

    juce::String TrackEditorPanel::title() const
    {
        if (currentTrackId == -1)
            return "MIDI Editor";
        const auto node = doc.findTrackById (currentTrackId);
        return "Edit Track: " + node.getProperty (SongIDs::name).toString();
    }

    void TrackEditorPanel::titleChanged()
    {
        if (onTitleChanged)
            onTitleChanged (title());
    }

    void TrackEditorPanel::setGridLength (GridLength length)
    {
        roll.setGrid (length, roll.getGridDivision());
        refreshSwingControl();
    }

    void TrackEditorPanel::setGridDivision (GridDivision division)
    {
        roll.setGrid (roll.getGridLength(), division);
        refreshSwingControl();
    }

    void TrackEditorPanel::setNoteLength (GridLength length)
    {
        roll.setNoteLength (length, roll.getNoteDivision());
    }

    void TrackEditorPanel::setNoteDivision (GridDivision division)
    {
        roll.setNoteLength (roll.getNoteLength(), division);
    }

    int TrackEditorPanel::getGridTicks() const
    {
        return roll.getGridTicks();
    }

    EditorGrid TrackEditorPanel::getEditorGrid() const
    {
        return roll.getEditorGrid();
    }

    void TrackEditorPanel::setSwing (int percent)
    {
        roll.setSwing (percent);
        bottomBar.setSwing (percent);
    }

    void TrackEditorPanel::refreshSwingControl()
    {
        bottomBar.setSwingEnabled (currentTrackId != -1 && roll.getGridDivision() == GridDivision::Swing
                                   && roll.getGridLength() != GridLength::Off);
    }

    int TrackEditorPanel::getSwing() const noexcept
    {
        return roll.getSwing();
    }

    void TrackEditorPanel::quantizeSelection()
    {
        roll.quantizeSelection();
    }

    void TrackEditorPanel::setGhostTracks (std::vector<juce::ValueTree> tracks)
    {
        roll.setGhostTracks (std::move (tracks));
    }

    void TrackEditorPanel::trackAppearanceChanged (juce::int64 trackId)
    {
        if (trackId == currentTrackId)
            titleChanged();

        roll.repaint();
        ++appearanceRepaintCount;
    }
}

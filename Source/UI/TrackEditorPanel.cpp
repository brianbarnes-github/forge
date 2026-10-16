#include "TrackEditorPanel.h"
#include "EditorMenuModel.h"
#include "GridSnap.h"
#include "MenuPopup.h"
#include "SongsmithColours.h"

namespace lotro
{
    TrackEditorPanel::TrackEditorPanel (SongDocument& document)
        : doc (document),
          roll (PianoRollComponent::Role::Source, &doc)
    {
        setWantsKeyboardFocus (true);
        roll.setSnapToGrid (snapToGrid);
        roll.onUnhandledKey = [this] (const juce::KeyPress& key) { return navigateKeyPressed (key); };
        addAndMakeVisible (menuBar);
        addAndMakeVisible (roll);
    }

    TrackEditorPanel::~TrackEditorPanel()
    {
        menuBar.setModel (nullptr);
    }

    void TrackEditorPanel::resized()
    {
        auto area = getLocalBounds();
        menuBar.setBounds (area.removeFromTop (menuBarHeight));
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
        s.snapToGrid = owner.snapToGrid;
        s.canStepMarker = owner.canStepMarker();
        s.canStepPitch = owner.currentTrackId != -1 && owner.roll.getActivePitch() >= 0;
        s.canSelectNearestNote = owner.canSelectNearestNote();
        const auto menus = editorMenus (s);
        juce::PopupMenu m;
        if (topLevelMenuIndex >= 0 && topLevelMenuIndex < (int) menus.size())
            addMenuItems (m, menus[(std::size_t) topLevelMenuIndex].items);
        return m;
    }

    void TrackEditorPanel::MenuModel::menuItemSelected (int menuItemID, int)
    {
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
            case EditorOptionsSnapToGrid:
                setSnapToGrid (! snapToGrid);
                if (onSnapToGridChanged)
                    onSnapToGridChanged (snapToGrid);
                return;
            default:
                return;
        }
    }

    void TrackEditorPanel::MenuModel::menuBarActivated (bool isActive)
    {
        if (isActive)
            ++activations;
    }

    int TrackEditorPanel::MenuBar::titleAt (const juce::MouseEvent& e)
    {
        // The bar's only children are its title components, in menu order.
        const auto p = e.getEventRelativeTo (this).getPosition();
        for (int i = 0; i < getNumChildComponents(); ++i)
            if (getChildComponent (i)->getBounds().contains (p))
                return i;
        return -1;
    }

    bool TrackEditorPanel::MenuBar::hasItems (int index)
    {
        auto* model = getModel();
        if (model == nullptr)
            return false;
        const auto names = model->getMenuBarNames();
        return model->getMenuForIndex (index, names[index]).getNumItems() > 0;
    }

    bool TrackEditorPanel::MenuBar::letThrough (const juce::MouseEvent& e)
    {
        const int title = titleAt (e);
        if (title >= 0 && ! hasItems (title))
            return false;
        if (title >= 0)
            openIndex = title;
        return true;
    }

    void TrackEditorPanel::MenuBar::mouseDown (const juce::MouseEvent& e)
    {
        if (letThrough (e))
            juce::MenuBarComponent::mouseDown (e);
    }

    void TrackEditorPanel::MenuBar::mouseDrag (const juce::MouseEvent& e)
    {
        if (letThrough (e))
            juce::MenuBarComponent::mouseDrag (e);
    }

    void TrackEditorPanel::MenuBar::mouseMove (const juce::MouseEvent& e)
    {
        if (letThrough (e))
            juce::MenuBarComponent::mouseMove (e);
    }

    bool TrackEditorPanel::MenuBar::keyPressed (const juce::KeyPress& key)
    {
        const bool left = key.isKeyCode (juce::KeyPress::leftKey);
        if (! left && ! key.isKeyCode (juce::KeyPress::rightKey))
            return juce::MenuBarComponent::keyPressed (key);

        std::vector<bool> flags;
        for (int i = 0; i < getNumChildComponents(); ++i)
            flags.push_back (hasItems (i));
        const int target = nextOpenableMenu (flags, juce::jmax (0, openIndex), left ? -1 : +1);
        if (target >= 0)
        {
            openIndex = target;
            showMenu (target);
        }
        return true;
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
        // The roll holds the panel's Snap to Grid state, so a ruler click snaps as a roll click does.
        ruler.onSetMarker = [&controller, &rollIn] (double tick)
        {
            controller.setMarkerTick (rollIn.getSnapToGrid() ? snapTickToGrid (tick, rollIn.getGridTicks()) : tick);
        };
        ruler.onClearMarker = [&controller] { controller.clearMarker(); };
        ruler.setLoopRange ([&controller] { return controller.getLoopRange(); });
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
            controller.setLoopRange (*loopEdgeAnchor, snapLoopTick (tick, rollIn.getGridTicks(), unsnapped));
        };
        ruler.onClearLoop = [this, &controller]
        {
            loopEdgeAnchor.reset();
            controller.clearLoopRange();
        };
        rollIn.onViewChanged = [this] { ruler.repaint(); };
        addAndMakeVisible (strip);
        addAndMakeVisible (ruler);
        addAndMakeVisible (roll);
    }

    TrackEditorPanel::Content::~Content()
    {
        roll.onViewChanged = nullptr;
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

    bool TrackEditorPanel::canSelectNearestNote() const
    {
        return currentTrackId != -1 && playback != nullptr && roll.hasNotes();
    }

    bool TrackEditorPanel::stepMarker (int direction)
    {
        if (! canStepMarker())
            return false;
        const double from = cursorTick();
        playback->setMarkerTick (stepTickByGrid (from, roll.getGridTicks(), direction));
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

    bool TrackEditorPanel::keyPressed (const juce::KeyPress& key)
    {
        if (navigateKeyPressed (key))
            return true;
        // The roll's SourceRollEditor only claims Delete/Backspace, so Space
        // reaches the panel.
        if (key == juce::KeyPress::spaceKey && playback != nullptr)
        {
            playback->togglePlayPause();
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

    void TrackEditorPanel::setGridTicks (int ticks)
    {
        roll.setGridTicks (ticks);
    }

    int TrackEditorPanel::getGridTicks() const
    {
        return roll.getGridTicks();
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

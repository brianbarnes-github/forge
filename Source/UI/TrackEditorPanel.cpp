#include "TrackEditorPanel.h"
#include "EditorMenuModel.h"
#include "SongsmithColours.h"

namespace lotro
{
    TrackEditorPanel::TrackEditorPanel (SongDocument& document)
        : doc (document),
          roll (PianoRollComponent::Role::Source, &doc)
    {
        setWantsKeyboardFocus (true);
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
        for (const auto& m : editorMenus())
            names.add (m.name);
        return names;
    }

    juce::PopupMenu TrackEditorPanel::MenuModel::getMenuForIndex (int, const juce::String&)
    {
        return {};
    }

    void TrackEditorPanel::MenuModel::menuItemSelected (int, int) {}

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
        ruler.onSetMarker = [&controller] (double tick) { controller.setMarkerTick (tick); };
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

    bool TrackEditorPanel::keyPressed (const juce::KeyPress& key)
    {
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

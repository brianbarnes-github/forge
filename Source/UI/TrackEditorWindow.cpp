#include "TrackEditorWindow.h"
#include "SongsmithColours.h"

namespace lotro
{
    TrackEditorWindow::TrackEditorWindow (SongDocument& document, juce::Component* centreAround)
        : juce::DocumentWindow ("Edit Track",
                                 juce::Colour (SongsmithColours::background),
                                 juce::DocumentWindow::allButtons),
          doc (document),
          roll (PianoRollComponent::Role::Source, &doc)
    {
        setUsingNativeTitleBar (true);
        setResizable (true, false);
        setContentNonOwned (&roll, true);
        centreAroundComponent (centreAround, 900, 500);
        setVisible (true);
    }

    TrackEditorWindow::~TrackEditorWindow()
    {
        // Detach whichever component is hosted (the roll, or `content`, which
        // holds the roll as a child) before `content` is destroyed.
        setContentNonOwned (nullptr, false);
    }

    TrackEditorWindow::Content::Content (PianoRollComponent& rollIn, PlaybackController& controller)
        : strip (controller),
          ruler ([&rollIn] (int x) { return rollIn.tickForXInComponent (x); }),
          roll (rollIn)
    {
        // The ruler spans the roll's width, so its local x is the roll's local x.
        // Clicks over the keyboard gutter map to ticks before the visible start
        // (and clamp at 0 in the ruler).
        ruler.onSeek = [&controller] (double tick) { controller.seekToTick (tick); };
        addAndMakeVisible (strip);
        addAndMakeVisible (ruler);
        addAndMakeVisible (roll);
    }

    void TrackEditorWindow::Content::resized()
    {
        auto area = getLocalBounds();
        strip.setBounds (area.removeFromTop (TransportStrip::height));
        ruler.setBounds (area.removeFromTop (TimelineRuler::height));
        roll.setBounds (area);
    }

    void TrackEditorWindow::setPlayback (PlaybackController* controller)
    {
        if (controller == nullptr || content != nullptr)
            return;

        playback = controller;
        roll.setPlayback (controller);
        content = std::make_unique<Content> (roll, *controller);
        setContentNonOwned (content.get(), true);
    }

    bool TrackEditorWindow::keyPressed (const juce::KeyPress& key)
    {
        // The roll's SourceRollEditor only claims Delete/Backspace, so Space
        // reaches the window.
        if (key == juce::KeyPress::spaceKey && playback != nullptr)
        {
            playback->togglePlayPause();
            return true;
        }
        return juce::DocumentWindow::keyPressed (key);
    }

    void TrackEditorWindow::setTrack (juce::ValueTree trackNode)
    {
        if (! trackNode.isValid())
            return;

        currentTrackId = (juce::int64) trackNode.getProperty (SongIDs::trackId);
        currentNoteSource = std::make_unique<SourceTrackNoteSource> (trackNode);

        const int ticksPerQuarter = (int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter, 480);
        roll.setNoteSource (currentNoteSource.get(), ticksPerQuarter, doc.getMeterMapNode());
        roll.setEditableTrack (trackNode);

        setName ("Edit Track: " + trackNode.getProperty (SongIDs::name).toString());
    }

    void TrackEditorWindow::setGridTicks (int ticks)
    {
        roll.setGridTicks (ticks);
    }

    int TrackEditorWindow::getGridTicks() const
    {
        return roll.getGridTicks();
    }

    void TrackEditorWindow::quantizeSelection()
    {
        roll.quantizeSelection();
    }

    void TrackEditorWindow::setGhostTracks (std::vector<juce::ValueTree> tracks)
    {
        roll.setGhostTracks (std::move (tracks));
    }

    void TrackEditorWindow::closeButtonPressed()
    {
        setVisible (false);
        if (onClosed)
            onClosed();
    }
}

#include "TrackEditorWindow.h"
#include "SongsmithColours.h"

namespace lotro
{
    TrackEditorWindow::TrackEditorWindow (SongDocument& document)
        : juce::DocumentWindow ("Edit Track",
                                 juce::Colour (SongsmithColours::background),
                                 juce::DocumentWindow::closeButton),
          doc (document),
          roll (PianoRollComponent::Role::Source, &doc)
    {
        setUsingNativeTitleBar (true);
        setResizable (true, false);
        setContentNonOwned (&roll, true);
        centreWithSize (900, 500);
        setVisible (true);
    }

    TrackEditorWindow::~TrackEditorWindow()
    {
        setContentNonOwned (nullptr, false);
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

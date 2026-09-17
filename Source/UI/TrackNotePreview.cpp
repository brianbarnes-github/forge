#include "TrackNotePreview.h"
#include "SongsmithColours.h"

namespace lotro
{
    TrackNotePreview::TrackNotePreview (juce::ValueTree trackNodeIn, const TimelineViewState& viewStateIn)
        : track (trackNodeIn), viewState (viewStateIn)
    {
    }

    void TrackNotePreview::paint (juce::Graphics& g)
    {
        using namespace SongsmithColours;

        auto bounds = getLocalBounds();
        g.setColour (juce::Colour (background));
        g.fillRect (bounds);

        int minPitch = 127;
        int maxPitch = 0;
        bool anyNotes = false;

        for (int i = 0; i < track.getNumChildren(); ++i)
        {
            auto note = track.getChild (i);
            if (! note.hasType (SongIDs::NOTE))
                continue;

            anyNotes = true;
            const int pitch = (int) note.getProperty (SongIDs::pitch);
            minPitch = juce::jmin (minPitch, pitch);
            maxPitch = juce::jmax (maxPitch, pitch);
        }

        if (! anyNotes)
            return;

        const int pitchSpan = juce::jmax (1, maxPitch - minPitch);

        g.setColour (juce::Colour (accentAmber));
        for (int i = 0; i < track.getNumChildren(); ++i)
        {
            auto note = track.getChild (i);
            if (! note.hasType (SongIDs::NOTE))
                continue;

            const int pitch = (int) note.getProperty (SongIDs::pitch);
            const int startTick = (int) note.getProperty (SongIDs::startTick);
            const int durationTicks = (int) note.getProperty (SongIDs::durationTicks);

            const int x = viewState.xForTick (startTick);
            const int width = juce::jmax (1, viewState.xForTick (startTick + durationTicks) - x);
            const float normalisedPitch = (float) (pitch - minPitch) / (float) pitchSpan;
            const int y = juce::roundToInt ((1.0f - normalisedPitch) * (float) juce::jmax (0, bounds.getHeight() - 2));

            g.fillRect (x, y, width, 2);
        }
    }

    juce::Rectangle<int> TrackNotePreview::ghostToggleBounds() const
    {
        return getLocalBounds().removeFromRight (16).removeFromTop (16).reduced (3);
    }

    bool TrackNotePreview::toggleGhostIfHit (juce::Point<int> pos)
    {
        if (! ghostToggleBounds().contains (pos))
            return false;

        ghostVisible = ! ghostVisible;
        repaint();
        if (onGhostToggled)
            onGhostToggled (ghostVisible);
        return true;
    }
}

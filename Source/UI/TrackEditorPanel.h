#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PianoRollComponent.h"
#include "Playback/PlaybackController.h"
#include "Playback/TimelineRuler.h"
#include "Playback/TransportStrip.h"
#include "SongDocument.h"
#include "SourceTrackNoteSource.h"

#include <functional>

namespace lotro
{
    // The single-instance editor for one MIDI_TRACK, opened by double-clicking a
    // TrackListComponent row. A plain component: the dock host puts it in a panel
    // that is either docked below the track list or floating in its own window.
    // Re-hosts the existing PianoRollComponent(Role::Source)/SourceRollEditor
    // pairing completely unchanged -- only the parent changes. Built once, empty
    // ("Double-click a track to edit it"), and re-pointed with setTrack.
    class TrackEditorPanel : public juce::Component
    {
    public:
        explicit TrackEditorPanel (SongDocument& document);
        ~TrackEditorPanel() override;

        void setTrack (juce::ValueTree trackNode);
        // Back to the empty state (the Song was replaced; the old track handle is stale).
        void clearTrack();
        juce::int64 getTrackId() const noexcept { return currentTrackId; }

        // "Edit Track: <name>", or "MIDI Editor" when no track is loaded.
        juce::String title() const;
        std::function<void (const juce::String&)> onTitleChanged;

        void setGridTicks (int ticks);
        int getGridTicks() const;
        void setFollowPlayhead (bool on) { roll.setFollowPlayhead (on); }
        bool getFollowPlayhead() const { return roll.getFollowPlayhead(); }
        void quantizeSelection();

        void setGhostTracks (std::vector<juce::ValueTree> tracks);

        // A track's name or colour changed: retitles the panel if it is this
        // panel's track, and repaints the roll (its notes or a ghost may use
        // that colour).
        void trackAppearanceChanged (juce::int64 trackId);
        int appearanceRepaintsForTesting() const noexcept { return appearanceRepaintCount; }

        // Hosts the shared transport strip and seek ruler above the roll and
        // shows the playhead over its notes; Space toggles play/pause. Not
        // owned: the controller must outlive this panel. Call once, with a
        // non-null controller.
        void setPlayback (PlaybackController* controller);
        bool keyPressed (const juce::KeyPress& key) override;
        bool hasTransportStripForTesting() const noexcept
        {
            return content != nullptr && content->strip.getParentComponent() == content.get() && content->strip.isVisible();
        }
        bool hasRulerForTesting() const noexcept
        {
            return content != nullptr && content->ruler.getParentComponent() == content.get() && content->ruler.isVisible();
        }

        void resized() override;
        void paintOverChildren (juce::Graphics&) override;

    private:
        friend struct TrackEditorPanelTestAccess;

        // Strip on top, ruler under it (same width as the roll, so a ruler x
        // is a roll x), then the roll filling the rest.
        class Content : public juce::Component
        {
        public:
            Content (PianoRollComponent& rollIn, PlaybackController& controller);
            ~Content() override;
            void resized() override;

            TransportStrip strip;
            TimelineRuler ruler;
            PianoRollComponent& roll;
        };

        void titleChanged();

        SongDocument& doc;
        PianoRollComponent roll;
        // After `roll` (it holds a reference) and destroyed before it. Its strip and
        // the roll's playhead are destroyed before the controller, which the owner
        // keeps alive for longer than this panel.
        std::unique_ptr<Content> content;
        PlaybackController* playback = nullptr;
        std::unique_ptr<SourceTrackNoteSource> currentNoteSource;
        juce::int64 currentTrackId = -1;
        int appearanceRepaintCount = 0;   // test observability
    };
}

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PianoRollComponent.h"
#include "Playback/PlaybackController.h"
#include "Playback/TimelineRuler.h"
#include "Playback/TransportStrip.h"
#include "SongDocument.h"
#include "SourceTrackNoteSource.h"

namespace lotro
{
    // Floating, single-instance editor window for one MIDI_TRACK, opened by
    // double-clicking a TrackListComponent row. Re-hosts the existing
    // PianoRollComponent(Role::Source)/SourceRollEditor pairing completely
    // unchanged -- only the parent changes, not the pairing's construction
    // or behaviour (2026-09-15 upper-region-track-timeline design).
    class TrackEditorWindow : public juce::DocumentWindow
    {
    public:
        // Pops up centred over `centreAround` (the application window),
        // kept on that window's monitor; nullptr centres on the active
        // top-level window instead.
        TrackEditorWindow (SongDocument& document, juce::Component* centreAround);
        ~TrackEditorWindow() override;

        void setTrack (juce::ValueTree trackNode);
        juce::int64 getTrackId() const noexcept { return currentTrackId; }

        void setGridTicks (int ticks);
        int getGridTicks() const;
        void quantizeSelection();

        void setGhostTracks (std::vector<juce::ValueTree> tracks);

        void closeButtonPressed() override;

        // Hosts the shared transport strip and seek ruler above the roll and
        // shows the playhead over its notes; Space toggles play/pause. Not
        // owned: the controller must outlive this window. Call once, with a
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

        // Fired when the window is closed by the user, so the owner can
        // reset its unique_ptr rather than hold a dangling window.
        std::function<void()> onClosed;

    private:
        friend struct TrackEditorWindowTestAccess;

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

        SongDocument& doc;
        PianoRollComponent roll;
        // After `roll` (it holds a reference) and destroyed before it; the
        // destructor detaches it from the window first. Its strip and the
        // roll's playhead are destroyed before the controller, which the
        // owner keeps alive for longer than this window.
        std::unique_ptr<Content> content;
        PlaybackController* playback = nullptr;
        std::unique_ptr<SourceTrackNoteSource> currentNoteSource;
        juce::int64 currentTrackId = -1;
    };
}

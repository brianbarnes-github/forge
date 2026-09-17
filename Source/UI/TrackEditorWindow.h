#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PianoRollComponent.h"
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
        explicit TrackEditorWindow (SongDocument& document);
        ~TrackEditorWindow() override;

        void setTrack (juce::ValueTree trackNode);
        juce::int64 getTrackId() const noexcept { return currentTrackId; }

        void setGridTicks (int ticks);
        int getGridTicks() const;
        void quantizeSelection();

        void setGhostTracks (std::vector<juce::ValueTree> tracks);

        void closeButtonPressed() override;

        // Fired when the window is closed by the user, so the owner can
        // reset its unique_ptr rather than hold a dangling window.
        std::function<void()> onClosed;

    private:
        SongDocument& doc;
        PianoRollComponent roll;
        std::unique_ptr<SourceTrackNoteSource> currentNoteSource;
        juce::int64 currentTrackId = -1;
    };
}

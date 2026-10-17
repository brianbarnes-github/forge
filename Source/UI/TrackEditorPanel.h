#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "EditorBottomBar.h"
#include "PianoRollComponent.h"
#include "Playback/PlaybackController.h"
#include "Playback/TimelineRuler.h"
#include "Playback/TransportStrip.h"
#include "ScaleSnap.h"
#include "SongDocument.h"
#include "SourceTrackNoteSource.h"

#include <functional>
#include <optional>

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
        // The panel's own menu bar (File, Edit, Navigate, Options, View) across the
        // top, docked or floating; the same height as the main window's.
        static constexpr int menuBarHeight = 24;

        explicit TrackEditorPanel (SongDocument& document);
        ~TrackEditorPanel() override;

        void setTrack (juce::ValueTree trackNode);
        // Back to the empty state (the Song was replaced; the old track handle is stale).
        void clearTrack();
        juce::int64 getTrackId() const noexcept { return currentTrackId; }

        // "Edit Track: <name>", or "MIDI Editor" when no track is loaded.
        juce::String title() const;
        std::function<void (const juce::String&)> onTitleChanged;

        // The editor menu bar's commands (EditorMenuCommandId). File > Export to New
        // MIDI File fires onExportTrackMidi with the loaded track (nothing when none
        // is loaded); File > Close Editor fires onCloseRequested; Options > Snap to
        // Grid flips the snap state and fires onSnapToGridChanged with the new value;
        // Options > Snap to Key > Enabled toggles it (only with a key) and its Root /
        // Type items set the session pick (see currentSnapKey); Options > Note
        // Preview's items flip their checkbox (see setNotePreview); View > Piano Notes /
        // MIDI Numbers set the keyboard labels and, on a change, fire onKeyboardLabelsChanged;
        // View > Piano Roll Notes / Color Note by set the roll's note shape / colour mode
        // and, on a change, fire onNoteShapeChanged / onNoteColourModeChanged;
        // Navigate > Move Marker Left / Right by Grid step the start marker (stepMarker);
        // Navigate > Move Up / Down a Semitone step the roll's active row;
        // Navigate > Select Note Nearest Cursor selects the note starting nearest
        // the cursor's time (selectNoteNearest); Add Note Nearest Cursor to Selection
        // adds the nearest one not yet selected (addNoteNearestToSelection);
        // Select Previous / Next Note select the note before / after the cursor on
        // any pitch and move the start marker to its start (selectSteppedNote);
        // with exactly one note selected, Select Previous / Next Note of the Same
        // Pitch do the same along its pitch (selectSamePitchNote) and Select All
        // Notes of the Same Pitch selects that pitch, leaving the marker alone;
        // Add Previous / Next Note to Selection add the note before / after the
        // selection and move the marker to its start (addSteppedNote); Move Cursor
        // to Start of Selection moves it to the earliest selected start.
        void invokeMenuCommand (int commandId);
        std::function<void (juce::int64 trackId)> onExportTrackMidi;
        std::function<void()> onCloseRequested;
        std::function<void (bool on)> onSnapToGridChanged;
        // Options > Note Preview's three items each flip their checkbox and fire
        // this with the new settings.
        std::function<void (const NotePreviewSettings&)> onNotePreviewChanged;

        // Options > Note Preview: which edits in the roll sound the inserted or
        // moved notes through the playback synth (a single note, or a chord of
        // up to six starting together; anything else stays silent). Nothing
        // sounds without playback or a track. Setting it does not fire
        // onNotePreviewChanged.
        void setNotePreview (const NotePreviewSettings& settings) noexcept { notePreview = settings; }
        NotePreviewSettings getNotePreview() const noexcept { return notePreview; }

        // View > Piano Notes / MIDI Numbers: what the roll's keyboard gutter shows.
        // Saved; setting it does not fire onKeyboardLabelsChanged.
        void setKeyboardLabels (KeyboardLabels labels) { roll.setKeyboardLabels (labels); }
        KeyboardLabels getKeyboardLabels() const noexcept { return roll.getKeyboardLabels(); }
        std::function<void (KeyboardLabels)> onKeyboardLabelsChanged;
        // Saved; setting them does not fire the callbacks.
        void setNoteShape (NoteShape shape) { roll.setNoteShape (shape); }
        NoteShape getNoteShape() const noexcept { return roll.getNoteShape(); }
        std::function<void (NoteShape)> onNoteShapeChanged;
        void setNoteColourMode (NoteColourMode mode) { roll.setNoteColourMode (mode); }
        NoteColourMode getNoteColourMode() const noexcept { return roll.getNoteColourMode(); }
        std::function<void (NoteColourMode)> onNoteColourModeChanged;

        // Options > Snap to Grid's tick: a click in the roll or the ruler that drops the
        // start marker lands on the nearest grid line. Setting it does not fire
        // onSnapToGridChanged.
        void setSnapToGrid (bool on) noexcept { snapToGrid = on; roll.setSnapToGrid (on); }
        bool getSnapToGrid() const noexcept { return snapToGrid; }

        // Options > Snap to Key: while Enabled and there is a key, a double-click
        // only adds a note on a pitch of that scale or chord (nothing else is
        // filtered). Session-only. The key is the user's Root/Type pick, else the
        // song key (read live, so Set Key and undo move it); nullopt when the
        // song has none and nothing was picked. clearTrack drops the pick.
        std::optional<SnapKey> currentSnapKey() const;
        bool getSnapToKey() const noexcept { return snapToKey; }

        // Options > Grid Length / Grid Division (also set by the main window's
        // Edit > Grid Size, which keeps the division): the editor's grid, drawn in
        // the roll and used by snapping and Move Marker by Grid. Session-only;
        // setting either reports nothing. getGridTicks is the spacing (0 = Off).
        void setGridLength (GridLength length);
        void setGridDivision (GridDivision division);
        GridLength getGridLength() const noexcept { return roll.getGridLength(); }
        GridDivision getGridDivision() const noexcept { return roll.getGridDivision(); }
        int getGridTicks() const;
        // Options > Note Length / Note Division: a double-clicked note's length
        // (Off follows the grid). Session-only; setting either reports nothing.
        void setNoteLength (GridLength length);
        void setNoteDivision (GridDivision division);
        GridLength getNoteLength() const noexcept { return roll.getNoteLength(); }
        GridDivision getNoteDivision() const noexcept { return roll.getNoteDivision(); }
        // The Swing division's strength (percent, -100..100; kept but inert off Swing) and the
        // grid as a value, swung lines included. Session-only; setting reports nothing.
        void setSwing (int percent);
        int getSwing() const noexcept;
        EditorGrid getEditorGrid() const;
        void setFollowPlayhead (bool on) { roll.setFollowPlayhead (on); }
        bool getFollowPlayhead() const { return roll.getFollowPlayhead(); }
        void quantizeSelection();
        // A note move/resize/rubber-band drag is in progress in the roll.
        bool isNoteGestureActive() const noexcept { return roll.isEditorGestureActive(); }

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
        // The user's command buttons for the panel's transport strip; applied now and when
        // setPlayback creates the strip. Not owned.
        void setStripButtonsHost (StripButtonsHost* host);
        StripButtonsBar* stripButtonsBarForTesting() { return content != nullptr ? content->strip.buttonsBarForTesting() : nullptr; }
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
        friend struct SongsmithMainComponentTestAccess;

        // Strip on top, ruler under it (same width as the roll, so a ruler x
        // is a roll x), then the roll filling the rest.
        class Content : public juce::Component
        {
        public:
            Content (PianoRollComponent& rollIn, PlaybackController& controller, SongDocument& document);
            ~Content() override;
            void resized() override;

            TransportStrip strip;
            TimelineRuler ruler;
            PianoRollComponent& roll;
            std::optional<double> loopEdgeAnchor;   // the ruler bracket drag's fixed (other) edge
        };

        // Feeds editorMenus() to the menu bar, built from the live state each time a
        // menu opens, and hands chosen ids to the panel.
        class MenuModel : public juce::MenuBarModel
        {
        public:
            explicit MenuModel (TrackEditorPanel& ownerIn) : owner (ownerIn) {}
            juce::StringArray getMenuBarNames() override;
            juce::PopupMenu getMenuForIndex (int topLevelMenuIndex, const juce::String& menuName) override;
            void menuItemSelected (int menuItemID, int topLevelMenuIndex) override;
            void menuBarActivated (bool isActive) override;
            int activationsForTesting() const noexcept { return activations; }

        private:
            TrackEditorPanel& owner;
            int activations = 0;   // test observability: times the bar was armed
        };

        // JUCE opens no popup for an empty menu and never reports it closed, which
        // leaves the bar armed (title lit, hover switching titles) and its open-menu
        // count off for the session. So presses, drags and hovers over a title with
        // no items, and Left/Right onto one, never reach the base class.
        class MenuBar : public juce::MenuBarComponent
        {
        public:
            using juce::MenuBarComponent::MenuBarComponent;
            void mouseDown (const juce::MouseEvent&) override;
            void mouseDrag (const juce::MouseEvent&) override;
            void mouseMove (const juce::MouseEvent&) override;
            bool keyPressed (const juce::KeyPress&) override;

        private:
            int titleAt (const juce::MouseEvent&);   // -1 off every title
            bool hasItems (int index);
            bool letThrough (const juce::MouseEvent&);    // false over an empty title
            int openIndex = -1;                            // the last title let through
        };

        void titleChanged();
        // A track, playback and a grid that is not Off.
        bool canStepMarker() const;
        // The cursor's time: the start marker, else the playhead. Needs playback.
        double cursorTick() const;
        // A track with notes, and playback.
        bool canSelectNearestNote() const;
        // Moves the start marker (or, with none, the playhead's tick) to the previous
        // (-1) or next (+1) grid line. Returns whether it could.
        bool stepMarker (int direction);
        // Selects only the previous (-1) or next (+1) note from the cursor (see
        // PianoRollComponent::selectSteppedNote) and moves the start marker to its
        // exact start. Returns whether there was one.
        bool selectSteppedNote (int direction);
        // Selects only the previous (-1) or next (+1) note of the one selected note's
        // pitch (see PianoRollComponent::selectSamePitchNote) and moves the start
        // marker to its exact start. Returns whether there was one.
        bool selectSamePitchNote (int direction);
        // Adds the note before (-1) or after (+1) the selection (see
        // PianoRollComponent::addSteppedNoteToSelection) and moves the start marker
        // to its exact start. Returns whether there was one.
        bool addSteppedNote (int direction);
        // Moves the start marker to the earliest selected note's exact start and
        // scrolls it into view. Returns whether anything was selected.
        bool moveCursorToSelectionStart();
        // Plain Left/Right (marker) and Up/Down (active row), from the panel or ahead
        // of the roll's Viewport scrolling; unclaimed when the step can't happen.
        bool navigateKeyPressed (const juce::KeyPress& key);

        // The bar's Swing slider is live only with a track, the Swing division and a length.
        void refreshSwingControl();

        SongDocument& doc;
        MenuModel menuModel { *this };
        MenuBar menuBar { &menuModel };
        PianoRollComponent roll;
        EditorBottomBar bottomBar;
        // After `roll` (it holds a reference) and destroyed before it. Its strip and
        // the roll's playhead are destroyed before the controller, which the owner
        // keeps alive for longer than this panel.
        std::unique_ptr<Content> content;
        PlaybackController* playback = nullptr;
        StripButtonsHost* stripButtonsHost = nullptr;
        std::unique_ptr<SourceTrackNoteSource> currentNoteSource;
        juce::int64 currentTrackId = -1;
        bool snapToGrid = true;
        bool snapToKey = false;
        NotePreviewSettings notePreview;
        std::optional<SnapKey> pickedSnapKey;   // nullopt: follow the song key
        int appearanceRepaintCount = 0;   // test observability
    };
}

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PianoRollComponent.h"
#include "Playback/PlaybackController.h"
#include "Playback/TimelineRuler.h"
#include "Playback/TransportStrip.h"
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

        void setGridTicks (int ticks);
        int getGridTicks() const;
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

        // Feeds editorMenus() to the menu bar. No items yet, so nothing to dispatch.
        class MenuModel : public juce::MenuBarModel
        {
        public:
            juce::StringArray getMenuBarNames() override;
            juce::PopupMenu getMenuForIndex (int topLevelMenuIndex, const juce::String& menuName) override;
            void menuItemSelected (int menuItemID, int topLevelMenuIndex) override;
            void menuBarActivated (bool isActive) override;
            int activationsForTesting() const noexcept { return activations; }

        private:
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

        SongDocument& doc;
        MenuModel menuModel;
        MenuBar menuBar { &menuModel };
        PianoRollComponent roll;
        // After `roll` (it holds a reference) and destroyed before it. Its strip and
        // the roll's playhead are destroyed before the controller, which the owner
        // keeps alive for longer than this panel.
        std::unique_ptr<Content> content;
        PlaybackController* playback = nullptr;
        StripButtonsHost* stripButtonsHost = nullptr;
        std::unique_ptr<SourceTrackNoteSource> currentNoteSource;
        juce::int64 currentTrackId = -1;
        int appearanceRepaintCount = 0;   // test observability
    };
}

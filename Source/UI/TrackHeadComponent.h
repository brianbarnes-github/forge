#pragma once

#include "SongDocument.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <memory>

// The two-row head at the left of a MIDI-source track row: index, colour
// swatch and name on top; drawn mute / solo icon buttons and a playback
// volume slider below. The conductor's head shows only its name. The head
// itself takes no clicks, so the row keeps selection / drag / double-click;
// only the child controls do.
namespace lotro
{

struct TrackHeadLayout
{
    juce::Rectangle<int> index, swatch, name, mute, solo, volume;
};

class TrackHeadComponent : public juce::Component
{
public:
    // trackNode must be a valid MIDI_TRACK node; displayIndex is the number
    // shown (0 = the conductor, drawn unnumbered).
    TrackHeadComponent (juce::ValueTree trackNode, int displayIndex);

    static constexpr int sidePadding = 8, rightPadding = 6, indexWidth = 16, swatchSize = 14, iconSize = 22, gap = 4;

    // Pure geometry: bounds = the head's own local bounds. The conductor gets
    // only index and name; its swatch / button / slider rectangles are empty.
    static TrackHeadLayout layoutFor (juce::Rectangle<int> bounds, bool conductor);

    void paint (juce::Graphics& g) override;
    void resized() override;

    // Reflects the playback controller's state without firing callbacks.
    void setMuteSolo (bool muted, bool soloed);

    // Re-reads name / colour / volume and repaints; the slider is set without notification.
    void refreshFromTrack();

    // Head-local; empty for the conductor.
    juce::Rectangle<int> swatchBounds() const;

    // Fired when the M / S button is clicked, with the new state.
    std::function<void (bool)> onMuteToggled;
    std::function<void (bool)> onSoloToggled;

    // Fired on a user change of the slider. startsGesture is true for the
    // first change of a drag and for every change made outside a drag
    // (keyboard, double-click reset), so each opens its own undo step.
    std::function<void (int percent, bool startsGesture)> onVolumeChanged;

    // Right-click menu. buildContextMenu() is the single place menu items are
    // added; item 1 is "Rename...", disabled on the conductor.
    juce::PopupMenu buildContextMenu() const;
    void showContextMenu();
    void contextMenuChosen (int itemId);

    // Inline rename. commitRename trims, and fires onRenamed only for a
    // non-empty name that differs from the current one; either way the editor
    // closes. Losing focus or pressing Escape cancels.
    void beginRename();
    void commitRename (const juce::String& newName);
    std::function<void (const juce::String&)> onRenamed;

    // Colour picker, opened by a click on the swatch (a no-op on the
    // conductor or while already open). colourPicked reports each change with
    // startsGesture true for the first of a picker session; colourPickerClosed
    // ends the session.
    void openColourPicker();
    void colourPicked (juce::Colour c);
    void colourPickerClosed();
    std::function<void (juce::uint32 argb, bool startsGesture)> onColourChanged;
    bool colourPickerOpenForTesting() const { return pickerOpen; }

    // The launched callout's selector (null when closed), and a dismissal that
    // takes the real close path once the message loop has run.
    juce::ColourSelector* colourSelectorForTesting() const;
    void dismissColourPickerForTesting();

    ~TrackHeadComponent() override;

    // Null when not editing.
    juce::TextEditor* renameEditorForTesting() { return renameEditor.get(); }

private:
    class IconButton : public juce::Button
    {
    public:
        enum class Kind { Mute, Solo };
        IconButton (Kind k, juce::Colour onColour) : juce::Button ({}), kind (k), onColourValue (onColour)
        {
            setClickingTogglesState (true);
            setWantsKeyboardFocus (false);
        }
        void paintButton (juce::Graphics& g, bool isOver, bool isDown) override;
    private:
        Kind kind;
        juce::Colour onColourValue;
    };

public:
    juce::Button& muteButtonForTesting() { return muteButton; }
    juce::Button& soloButtonForTesting() { return soloButton; }
    juce::Slider& volumeSliderForTesting() { return volumeSlider; }

private:
    // Detaches the editor now and deletes it on the next message-loop turn, so
    // it may be called from the editor's own callbacks.
    void endRename();

    bool isConductorTrack() const { return (bool) track.getProperty (SongIDs::isConductor, false); }

    juce::ValueTree track;
    int             index;
    IconButton      muteButton { IconButton::Kind::Mute, juce::Colours::orangered };
    IconButton      soloButton { IconButton::Kind::Solo, juce::Colours::gold };
    juce::Slider    volumeSlider;
    bool            dragging = false;        // between the slider's onDragStart and onDragEnd
    bool            dragHasChanged = false;  // the current drag has already reported a change
    std::unique_ptr<juce::TextEditor> renameEditor;
    juce::Component::SafePointer<juce::CallOutBox> pickerBox;
    bool            pickerOpen = false;
    bool            pickerFirstChange = true;  // the next colourPicked starts a gesture
};

} // namespace lotro

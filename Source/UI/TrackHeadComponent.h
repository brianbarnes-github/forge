#pragma once

#include "SongDocument.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

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
    // (wheel, keyboard, double-click reset), so each opens its own undo step.
    std::function<void (int percent, bool startsGesture)> onVolumeChanged;

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
    bool isConductorTrack() const { return (bool) track.getProperty (SongIDs::isConductor, false); }

    juce::ValueTree track;
    int             index;
    IconButton      muteButton { IconButton::Kind::Mute, juce::Colours::orangered };
    IconButton      soloButton { IconButton::Kind::Solo, juce::Colours::gold };
    juce::Slider    volumeSlider;
    bool            dragging = false;        // between the slider's onDragStart and onDragEnd
    bool            dragHasChanged = false;  // the current drag has already reported a change
};

} // namespace lotro

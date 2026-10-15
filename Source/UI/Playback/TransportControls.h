#pragma once

#include "UI/Playback/PlaybackController.h"
#include "UI/SongDocument.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace lotro
{

// The right area of the transport strip: the Selection group (loop start / end
// / length), the time signature and tempo at the playhead, and the playback-rate
// knob. Spec: docs/superpowers/specs/2026-10-14-songsmith-transport-controls-design.md.
class TransportControls : public juce::Component, private PlaybackController::Listener
{
public:
    static constexpr int preferredWidth = 520;

    TransportControls (PlaybackController& controllerIn, SongDocument& documentIn);
    ~TransportControls() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

    juce::TextEditor& startBoxForTesting()  { return startBox.editor; }
    juce::TextEditor& endBoxForTesting()    { return endBox.editor; }
    juce::TextEditor& lengthBoxForTesting() { return lengthBox.editor; }
    juce::TextEditor& meterBoxForTesting()  { return meterBox.editor; }
    juce::TextEditor& tempoBoxForTesting()  { return tempoBox.editor; }
    juce::Slider& rateSliderForTesting()    { return rateSlider; }
    juce::Label& rateReadoutForTesting()    { return rateReadout; }

private:
    // A text box plus the text last written into it, so a refresh can tell "the
    // user is typing here" (focused and different from `shown`) from "idle".
    struct Box
    {
        juce::TextEditor editor;
        juce::String shown;
        bool isEditing() const { return editor.hasKeyboardFocus (true) && editor.getText() != shown; }
        void show (const juce::String& text);   // writes text and records it
    };

    enum class Edited { start, end, length };

    void playbackLoopChanged() override { refreshSelection(); }
    void playbackRateChanged() override { refreshRate(); }

    void setupBox (Box& box, const juce::String& tooltip, std::function<void()> onCommit);
    void commitSelection (Edited which);
    void refreshSelection();
    void reject (Box& box, const juce::String& reason);
    void refreshRate();
    void applyReadoutText();

    PlaybackController& controller;
    SongDocument& doc;

    juce::Label selectionLabel;
    Box startBox, endBox, lengthBox, meterBox, tempoBox;
    juce::Slider rateSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Label rateReadout;
};

} // namespace lotro

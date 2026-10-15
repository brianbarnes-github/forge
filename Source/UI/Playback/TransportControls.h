#pragma once

#include "UI/Playback/PlaybackController.h"
#include "UI/SongDocument.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>

namespace lotro
{

// Which tempo the rate knob's menu bakes (times the rate) into the song's first tempo.
enum class RateBake { firstTempo, playheadTempo };

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

    // The rate knob's right-click menu actions.
    void resetRate();
    // Sets the first tempo to (chosen tempo x rate) as one undo step, then resets the rate to 1.000.
    // A tempo the document cannot store is rejected with a bubble and nothing changes.
    void bakeRate (RateBake source);

    juce::TextEditor& startBoxForTesting()  { return startBox.editor; }
    juce::TextEditor& endBoxForTesting()    { return endBox.editor; }
    juce::TextEditor& lengthBoxForTesting() { return lengthBox.editor; }
    juce::TextEditor& meterBoxForTesting()  { return meterBox.editor; }
    juce::TextEditor& tempoBoxForTesting()  { return tempoBox.editor; }
    // Real keyboard focus needs a window manager; this makes every box count as focused.
    void pretendFocusedForTesting (bool focused) { for (auto* b : { &startBox, &endBox, &lengthBox, &meterBox, &tempoBox }) b->pretendFocused = focused; }
    // The rejection reason while its bubble is showing, else empty.
    juce::String reasonShownForTesting() const { return bubble != nullptr && bubble->isVisible() ? lastReason : juce::String(); }
    juce::Slider& rateSliderForTesting()    { return rateSlider; }
    juce::Label& rateReadoutForTesting()    { return rateReadout; }

private:
    // A text box plus the text last written into it, so a refresh can tell "the
    // user is typing here" (focused and different from `shown`) from "idle".
    struct Box
    {
        juce::TextEditor editor;
        juce::String shown;
        bool pretendFocused = false;
        bool isEditing() const { return (pretendFocused || editor.hasKeyboardFocus (true)) && editor.getText() != shown; }
        void show (const juce::String& text);   // writes text and records it
    };

    enum class Edited { start, end, length };

    void mouseDown (const juce::MouseEvent&) override;   // right-click on the knob: the rate menu
    void showRateMenu();

    void playbackLoopChanged() override { refreshSelection(); }
    void playbackRateChanged() override { refreshRate(); }
    void playbackPositionChanged() override { refreshTiming(); }
    void playbackSnapshotChanged() override { refreshSelection(); refreshTiming(); }

    void setupBox (Box& box, const juce::String& tooltip, std::function<void (bool enter)> onCommit);
    void commitSelection (Edited which, bool enter);   // enter: an explicit commit, not a focus loss
    void refreshSelection (bool force = false);   // force: overwrite a box even while it is being edited
    void reject (Box& box, const juce::String& reason);   // red outline plus a bubble with the reason
    void showReason (Box& box, const juce::String& reason);
    bool hasSong() const;   // a snapshot with something to play
    void refreshTiming (bool force = false);   // meter and tempo at the playhead; force as for refreshSelection
    void commitMeter();
    void commitTempo();
    void refreshRate();
    void applyReadoutText();

    PlaybackController& controller;
    SongDocument& doc;

    juce::Label selectionLabel;
    Box startBox, endBox, lengthBox, meterBox, tempoBox;
    // LookAndFeel_V4 insets a rotary by 10 px a side, which leaves almost nothing of a 24 px knob.
    struct KnobLook : juce::LookAndFeel_V4
    {
        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                               float startAngle, float endAngle, juce::Slider&) override;
    };
    KnobLook knobLook;
    juce::Slider rateSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Label rateReadout;
    juce::String lastReason;
    std::unique_ptr<juce::BubbleMessageComponent> bubble;   // the rejection reason, pointing at the box
};

} // namespace lotro

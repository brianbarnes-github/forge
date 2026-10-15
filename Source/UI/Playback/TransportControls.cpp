#include "UI/Playback/TransportControls.h"

#include "UI/MeterSegments.h"
#include "UI/Playback/RateMapping.h"
#include "UI/Playback/TransportText.h"
#include "UI/SongsmithColours.h"
#include "UI/TempoEdit.h"

#include <algorithm>
#include <cmath>

namespace lotro
{

namespace
{
    constexpr int labelWidth = 62, positionWidth = 64, lengthWidth = 56, meterWidth = 46, tempoWidth = 78,
                  knobSize = 24, readoutWidth = 52, gap = 6, boxHeight = 20;

    // Start of the bar containing `tick` (not the nearest bar line); 0 with no segments.
    double barStartContaining (const std::vector<MeterSegment>& segments, double tick)
    {
        if (segments.empty())
            return 0.0;
        const auto& s = segmentAt (segments, tick);
        return s.startTick + std::floor (std::max (0.0, tick - s.startTick) / s.ticksPerBar) * s.ticksPerBar;
    }

    juce::String describe (TempoEditError e)
    {
        switch (e)
        {
            case TempoEditError::NoTimeBase:        return "Open a MIDI file first";
            case TempoEditError::NegativeTick:      return "That position is before the start";
            case TempoEditError::BpmOutOfRange:     return "Tempo must be between " + juce::String (minBpm(), 2) + " and " + juce::String (maxBpm);
            case TempoEditError::MeterOutOfRange:   return "Numerator 1-32; denominator 1, 2, 4, 8, 16 or 32";
            case TempoEditError::TickOccupied:      return "There is already an event at that position";
            case TempoEditError::NoSuchEvent:       return "No such event";
            case TempoEditError::CannotRemoveFirst: return "The first event cannot be removed";
        }
        return {};
    }
}

void TransportControls::Box::show (const juce::String& text)
{
    editor.setText (text, juce::dontSendNotification);
    shown = text;
}

TransportControls::TransportControls (PlaybackController& controllerIn, SongDocument& documentIn)
    : controller (controllerIn), doc (documentIn)
{
    selectionLabel.setText ("Selection", juce::dontSendNotification);
    selectionLabel.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::textMuted));
    addAndMakeVisible (selectionLabel);

    setupBox (startBox,  "Loop start (bar.beat.tick)", [this] (bool enter) { commitSelection (Edited::start, enter); });
    setupBox (endBox,    "Loop end (bar.beat.tick)",   [this] (bool enter) { commitSelection (Edited::end, enter); });
    setupBox (lengthBox, "Loop length in beats",       [this] (bool enter) { commitSelection (Edited::length, enter); });
    setupBox (meterBox,  "Time signature at the playhead", [this] (bool) { commitMeter(); });
    setupBox (tempoBox,  "Tempo at the playhead",          [this] (bool) { commitTempo(); });

    rateSlider.setLookAndFeel (&knobLook);
    rateSlider.setRange (-2.0, 2.0, 0.001);
    rateSlider.setValue (rateToSlider (controller.rate()), juce::dontSendNotification);
    rateSlider.setDoubleClickReturnValue (true, 0.0);
    // Slider value 0 (rate 1.000) is the midpoint of the sweep, which is 2*pi: straight up.
    rateSlider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    rateSlider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (SongsmithColours::accentAmber));
    rateSlider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (SongsmithColours::columnDivider));
    rateSlider.setColour (juce::Slider::thumbColourId, juce::Colour (SongsmithColours::text));
    rateSlider.setWantsKeyboardFocus (false);
    rateSlider.setTooltip ("Playback rate (double-click to reset to 1.000)");
    rateSlider.onValueChange = [this] { controller.setRate (sliderToRate (rateSlider.getValue())); };
    addAndMakeVisible (rateSlider);

    rateReadout.setEditable (false, true);   // double-click to type
    rateReadout.setJustificationType (juce::Justification::centredLeft);
    rateReadout.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::text));
    rateReadout.onTextChange = [this] { applyReadoutText(); };
    addAndMakeVisible (rateReadout);

    controller.addListener (this);
    refreshSelection();
    refreshTiming();
    refreshRate();
}

TransportControls::~TransportControls()
{
    rateSlider.setLookAndFeel (nullptr);
    controller.removeListener (this);
}

void TransportControls::KnobLook::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                                    float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    const auto radius = std::min (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const auto angle = startAngle + sliderPos * (endAngle - startAngle);
    const auto arcRadius = radius - 1.5f;

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (slider.findColour (juce::Slider::rotarySliderOutlineColourId));
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path fill;
    fill.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);
    g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId));
    g.strokePath (fill, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Pointer from the centre toward the current angle.
    juce::Path pointer;
    pointer.addLineSegment (juce::Line<float> (0.0f, -2.0f, 0.0f, -(arcRadius - 3.0f)), 0.0f);
    g.setColour (slider.findColour (juce::Slider::thumbColourId));
    g.strokePath (pointer, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                  juce::AffineTransform::rotation (angle).translated (centre));
}

void TransportControls::setupBox (Box& box, const juce::String& tooltip, std::function<void (bool enter)> onCommit)
{
    box.editor.setMultiLine (false);
    box.editor.setTooltip (tooltip);
    box.editor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (SongsmithColours::panelHeader));
    box.editor.setColour (juce::TextEditor::textColourId, juce::Colour (SongsmithColours::text));
    box.editor.setColour (juce::TextEditor::outlineColourId, juce::Colour (SongsmithColours::border));
    // Enter commits and, once the box holds the true value, hands focus back so the
    // window's shortcuts (Space, Esc, S, Delete, undo) work again. A held half-entry stays focused.
    box.editor.onReturnKey = [&box, onCommit]
    {
        onCommit (true);
        if (box.editor.getText() == box.shown)
            juce::Component::unfocusAllComponents();
    };
    box.editor.onFocusLost = [onCommit] { onCommit (false); };
    box.editor.onEscapeKey = [this]
    {
        refreshSelection (true);
        refreshTiming (true);
        juce::Component::unfocusAllComponents();
    };
    addAndMakeVisible (box.editor);
}

void TransportControls::paint (juce::Graphics& g)
{
    // A fixed mark over the top of the knob's rim: the neutral 1.000 position at 12 o'clock.
    const auto knob = rateSlider.getBounds();
    g.setColour (juce::Colour (SongsmithColours::accentAmber));
    g.fillRect (juce::Rectangle<float> ((float) knob.getCentreX() - 1.0f, (float) knob.getY() - 1.0f, 2.0f, 5.0f));

    // The Selection group's frame: label + three boxes.
    g.setColour (juce::Colour (SongsmithColours::trackDivider));
    g.drawRoundedRectangle (selectionLabel.getBounds().getX() - 3.0f, 1.0f,
                            (float) (lengthBox.editor.getRight() - selectionLabel.getX() + 6), (float) getHeight() - 2.0f, 3.0f, 1.0f);
}

void TransportControls::resized()
{
    auto area = getLocalBounds().reduced (0, (getHeight() - boxHeight) / 2);
    auto place = [&] (juce::Component& c, int width) { c.setBounds (area.removeFromLeft (width)); area.removeFromLeft (gap); };
    area.removeFromLeft (4);
    rateSlider.setBounds (area.removeFromLeft (knobSize).withSizeKeepingCentre (knobSize, knobSize));
    area.removeFromLeft (gap);
    rateReadout.setBounds (area.removeFromLeft (readoutWidth));
    area.removeFromLeft (gap);
    place (selectionLabel, labelWidth);
    place (startBox.editor, positionWidth);
    place (endBox.editor, positionWidth);
    place (lengthBox.editor, lengthWidth);
    area.removeFromLeft (gap);
    place (meterBox.editor, meterWidth);
    place (tempoBox.editor, tempoWidth);
}

void TransportControls::reject (Box& box, const juce::String& reason)
{
    box.editor.setColour (juce::TextEditor::outlineColourId, juce::Colour (SongsmithColours::outOfRangeBorder));
    box.editor.repaint();
    showReason (box, reason);
    juce::Component::SafePointer<juce::TextEditor> safe (&box.editor);
    juce::Timer::callAfterDelay (1500, [safe]
    {
        if (safe != nullptr)
        {
            safe->setColour (juce::TextEditor::outlineColourId, juce::Colour (SongsmithColours::border));
            safe->repaint();
        }
    });
}

void TransportControls::showReason (Box& box, const juce::String& reason)
{
    // The app has no TooltipWindow, so the reason goes in a bubble on the window.
    lastReason = reason;
    bubble = std::make_unique<juce::BubbleMessageComponent> (150);
    bubble->setColour (juce::BubbleComponent::backgroundColourId, juce::Colour (SongsmithColours::panelHeader));
    bubble->setColour (juce::BubbleComponent::outlineColourId, juce::Colour (SongsmithColours::outOfRangeBorder));
    if (auto* top = box.editor.getTopLevelComponent())
        top->addChildComponent (*bubble);
    juce::AttributedString text;
    text.append (reason, juce::Font (juce::FontOptions (13.0f)), juce::Colour (SongsmithColours::text));
    bubble->showAt (&box.editor, text, 1800, true, false);
}

void TransportControls::refreshSelection (bool force)
{
    const auto snapshot = controller.currentSnapshot();
    const auto range = controller.getLoopRange();
    const bool enabled = hasSong();
    for (auto* box : { &startBox, &endBox, &lengthBox })
        box->editor.setEnabled (enabled);
    if (! enabled)
    {
        for (auto* box : { &startBox, &endBox, &lengthBox })
            if (force || ! box->isEditing())
                box->show ({});
        return;
    }
    const auto segments = meterSegments (doc.getMeterChanges(), snapshot->tempo().getTicksPerQuarter());
    if (! range.has_value())
    {
        // No loop: position zero, so a typed end or length has a start to work from.
        if (force || ! startBox.isEditing())  startBox.show (formatPosition (segments, 0.0));
        if (force || ! endBox.isEditing())    endBox.show (formatPosition (segments, 0.0));
        if (force || ! lengthBox.isEditing()) lengthBox.show (formatBeats (0.0));
        return;
    }
    if (force || ! startBox.isEditing())  startBox.show (formatPosition (segments, range->startTick));
    if (force || ! endBox.isEditing())    endBox.show (formatPosition (segments, range->endTick));
    if (force || ! lengthBox.isEditing()) lengthBox.show (formatBeats (ticksToBeats (segments, range->startTick, range->endTick)));
}

bool TransportControls::hasSong() const
{
    const auto snapshot = controller.currentSnapshot();
    return snapshot != nullptr && snapshot->endSeconds() > 0.0;
}

void TransportControls::refreshTiming (bool force)
{
    const bool enabled = hasSong();
    meterBox.editor.setEnabled (enabled);
    tempoBox.editor.setEnabled (enabled);
    if (! enabled)
    {
        for (auto* box : { &meterBox, &tempoBox })
            if (force || ! box->isEditing())
                box->show ({});
        return;
    }
    const double tick = controller.getPositionTicks();
    if (force || ! meterBox.isEditing())
    {
        const auto segments = meterSegments (doc.getMeterChanges(), controller.currentSnapshot()->tempo().getTicksPerQuarter());
        const auto numerator = segments.empty() ? 4 : segmentAt (segments, tick).numerator;
        const auto denominator = segments.empty() ? 4 : segmentAt (segments, tick).denominator;
        meterBox.show (juce::String (formatMeterText (numerator, denominator)));
    }
    if (force || ! tempoBox.isEditing())
        tempoBox.show (juce::String::fromUTF8 (formatExactTempoText (bpmAt (listTempoMeterEvents (doc), (int) std::llround (tick))).c_str()));
}

void TransportControls::commitMeter()
{
    if (! hasSong() || meterBox.editor.getText() == meterBox.shown)
        return;
    const auto parsed = parseMeterText (meterBox.editor.getText().toStdString());
    if (! parsed)
    {
        reject (meterBox, "Use n/d, such as 6/8");
    }
    else
    {
        // The edit goes on the bar containing the playhead, which is the bar the box shows.
        const auto segments = meterSegments (doc.getMeterChanges(), controller.currentSnapshot()->tempo().getTicksPerQuarter());
        const double barTick = barStartContaining (segments, controller.getPositionTicks());
        const bool alreadyInForce = ! segments.empty() && segmentAt (segments, barTick).numerator == parsed->first
                                    && segmentAt (segments, barTick).denominator == parsed->second;
        if (! alreadyInForce)
            if (const auto error = setMeter (doc, (int) std::llround (barTick), parsed->first, parsed->second))
                reject (meterBox, describe (*error));
    }
    refreshTiming (true);
}

void TransportControls::commitTempo()
{
    if (! hasSong() || tempoBox.editor.getText() == tempoBox.shown)
        return;
    const auto bpm = parseTempoText (tempoBox.editor.getText().toStdString());
    if (! bpm)
    {
        reject (tempoBox, "Enter a tempo, such as 120");
    }
    else
    {
        const int tick = (int) std::llround (controller.getPositionTicks());
        // The same rounding the document stores: 120.00001 is the tempo already in force.
        const auto typed = roundedBpm (*bpm);
        const bool alreadyInForce = typed.has_value() && typed == roundedBpm (bpmAt (listTempoMeterEvents (doc), tick));
        if (! alreadyInForce)
            if (const auto error = setTempo (doc, tick, *bpm))
                reject (tempoBox, describe (*error));
    }
    refreshTiming (true);
}

void TransportControls::commitSelection (Edited which, bool enter)
{
    const auto snapshot = controller.currentSnapshot();
    if (snapshot == nullptr)
        return;
    const auto segments = meterSegments (doc.getMeterChanges(), snapshot->tempo().getTicksPerQuarter());
    auto text = [] (Box& b) { return b.editor.getText().toStdString(); };

    // Nothing typed anywhere: clear the loop.
    if (startBox.editor.getText().isEmpty() && endBox.editor.getText().isEmpty() && lengthBox.editor.getText().isEmpty())
    {
        controller.clearLoopRange();
        refreshSelection (true);
        return;
    }

    // Not edited yet (focus passing through a box): nothing to do.
    Box& edited = which == Edited::start ? startBox : which == Edited::end ? endBox : lengthBox;
    if (edited.editor.getText() == edited.shown)
        return;

    // Some but not all boxes emptied while a loop exists. Enter shows the true range again;
    // a focus loss holds the empty box so the user can go on emptying the others.
    if (controller.getLoopRange().has_value()
        && (startBox.editor.getText().isEmpty() || endBox.editor.getText().isEmpty() || lengthBox.editor.getText().isEmpty()))
    {
        if (enter)
            refreshSelection (true);
        return;
    }

    // No loop yet and the end still shows its untouched default: a typed start would only be
    // reordered against position zero, so hold it until an end is typed (no bubble, no revert).
    if (which == Edited::start && ! controller.getLoopRange().has_value() && endBox.editor.getText() == endBox.shown)
        return;

    std::optional<double> start, end;
    if (startBox.editor.getText().isNotEmpty())
    {
        const auto p = parsePosition (text (startBox), segments);
        if (p.error) { reject (startBox, describe (*p.error)); refreshSelection (true); return; }
        start = p.tick;
    }
    if (which == Edited::length)
    {
        const auto beats = parseBeats (text (lengthBox));
        if (! beats || ! start) { reject (lengthBox, "Enter a length in beats, with a start"); refreshSelection (true); return; }
        end = beatsToTicks (segments, *start, *beats);
    }
    else if (endBox.editor.getText().isNotEmpty())
    {
        const auto p = parsePosition (text (endBox), segments);
        if (p.error) { reject (endBox, describe (*p.error)); refreshSelection (true); return; }
        end = p.tick;
    }

    if (! start || ! end)
        return;   // an edge is still missing: hold what was typed

    if (! controller.setLoopRange (*start, *end))
        reject (edited, "That range is too short or outside the song");
    refreshSelection (true);
}

void TransportControls::refreshRate()
{
    rateSlider.setValue (rateToSlider (controller.rate()), juce::dontSendNotification);
    rateReadout.setText (formatRate (controller.rate()), juce::dontSendNotification);
}

void TransportControls::applyReadoutText()
{
    if (const auto r = parseRate (rateReadout.getText().toStdString()))
        controller.setRate (*r);
    refreshRate();   // also reverts junk to the current rate
}

} // namespace lotro

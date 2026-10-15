#include "UI/Playback/TransportControls.h"

#include "UI/MeterSegments.h"
#include "UI/Playback/RateMapping.h"
#include "UI/Playback/TransportText.h"
#include "UI/SongsmithColours.h"
#include "UI/TempoEdit.h"

#include <cmath>

namespace lotro
{

namespace
{
    constexpr int labelWidth = 62, positionWidth = 64, lengthWidth = 56, meterWidth = 46, tempoWidth = 78,
                  knobSize = 24, readoutWidth = 52, gap = 6, boxHeight = 20;

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

    setupBox (startBox,  "Loop start (bar.beat.tick)", [this] { commitSelection (Edited::start); });
    setupBox (endBox,    "Loop end (bar.beat.tick)",   [this] { commitSelection (Edited::end); });
    setupBox (lengthBox, "Loop length in beats",       [this] { commitSelection (Edited::length); });
    setupBox (meterBox,  "Time signature at the playhead", [this] { commitMeter(); });
    setupBox (tempoBox,  "Tempo at the playhead",          [this] { commitTempo(); });

    rateSlider.setRange (-2.0, 2.0, 0.001);
    rateSlider.setValue (rateToSlider (controller.rate()), juce::dontSendNotification);
    rateSlider.setDoubleClickReturnValue (true, 0.0);
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

TransportControls::~TransportControls() { controller.removeListener (this); }

void TransportControls::setupBox (Box& box, const juce::String& tooltip, std::function<void()> onCommit)
{
    box.editor.setMultiLine (false);
    box.editor.setTooltip (tooltip);
    box.editor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (SongsmithColours::panelHeader));
    box.editor.setColour (juce::TextEditor::textColourId, juce::Colour (SongsmithColours::text));
    box.editor.setColour (juce::TextEditor::outlineColourId, juce::Colour (SongsmithColours::border));
    box.editor.onReturnKey = onCommit;
    box.editor.onFocusLost = onCommit;
    addAndMakeVisible (box.editor);
}

void TransportControls::paint (juce::Graphics& g)
{
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
    place (selectionLabel, labelWidth);
    place (startBox.editor, positionWidth);
    place (endBox.editor, positionWidth);
    place (lengthBox.editor, lengthWidth);
    area.removeFromLeft (gap);
    place (meterBox.editor, meterWidth);
    place (tempoBox.editor, tempoWidth);
    rateSlider.setBounds (area.removeFromLeft (knobSize).withSizeKeepingCentre (knobSize, getHeight() - 4));
    area.removeFromLeft (gap);
    rateReadout.setBounds (area.removeFromLeft (readoutWidth));
}

void TransportControls::reject (Box& box, const juce::String& reason)
{
    box.editor.setTooltip (reason);
    box.editor.setColour (juce::TextEditor::outlineColourId, juce::Colour (SongsmithColours::outOfRangeBorder));
    box.editor.repaint();
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

void TransportControls::refreshSelection (bool force)
{
    const auto snapshot = controller.currentSnapshot();
    const auto range = controller.getLoopRange();
    const bool enabled = hasSong();
    for (auto* box : { &startBox, &endBox, &lengthBox })
        box->editor.setEnabled (enabled);
    if (! range.has_value() || ! enabled)
    {
        for (auto* box : { &startBox, &endBox, &lengthBox })
            if (force || ! box->isEditing())
                box->show ({});
        return;
    }
    const auto segments = meterSegments (doc.getMeterChanges(), snapshot->tempo().getTicksPerQuarter());
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
        // A meter change belongs on a bar line: put it on the one nearest the playhead.
        const auto segments = meterSegments (doc.getMeterChanges(), controller.currentSnapshot()->tempo().getTicksPerQuarter());
        const double tick = controller.getPositionTicks();
        if (const auto error = setMeter (doc, (int) std::llround (nearestBarStart (segments, tick)), parsed->first, parsed->second))
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
        reject (tempoBox, "Enter a tempo, such as 120");
    else if (const auto error = setTempo (doc, (int) std::llround (controller.getPositionTicks()), *bpm))
        reject (tempoBox, describe (*error));
    refreshTiming (true);
}

void TransportControls::commitSelection (Edited which)
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

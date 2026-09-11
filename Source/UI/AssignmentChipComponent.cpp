#include "AssignmentChipComponent.h"
#include "SongsmithColours.h"

namespace lotro
{

namespace
{
    // U+2212 MINUS SIGN, used for negative transpose per the mockup ("-12"
    // renders with a full-width minus rather than a hyphen).
    juce::String transposeLabel (int semitones)
    {
        if (semitones >= 0)
            return "+" + juce::String (semitones);
        return juce::String::fromUTF8 ("\xe2\x88\x92") + juce::String (-semitones);
    }
}

AssignmentChipComponent::AssignmentChipComponent (SongDocument& document, juce::ValueTree partNode,
                                                    juce::ValueTree assignmentNode)
    : doc (document), part (std::move (partNode)), assignment (std::move (assignmentNode))
{
    jassert (part.hasType (SongIDs::PART));
    jassert (assignment.hasType (SongIDs::ASSIGNMENT));

    addAndMakeVisible (removeButton);
    removeButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    removeButton.setColour (juce::TextButton::textColourOffId, juce::Colour (SongsmithColours::textMuted));

    juce::ValueTree partRef = part;
    juce::ValueTree assignmentRef = assignment;
    SongDocument* docPtr = &doc;
    removeButton.onClick = [docPtr, partRef, assignmentRef]() mutable
    {
        docPtr->removeAssignment (partRef, assignmentRef);
    };
}

juce::String AssignmentChipComponent::buildLabel() const
{
    const auto trackId = (juce::int64) assignment.getProperty (SongIDs::trackId);
    const auto trackNode = doc.findTrackById (trackId);

    // M8: resolving trackId to a positional index here (rather than only in
    // SongModelBridge, the project's one sanctioned place for it) is
    // deliberately allowed — this is display-only label text, re-resolved
    // fresh on every paint, and the resulting int is never used as an index
    // into anything. It exists because B3 requires a "Tk<n>" chip label, and
    // that number IS the track's current SOURCE_MIDI position.
    juce::String trackLabel = "Tk?";
    if (trackNode.isValid())
    {
        const auto position = doc.getSourceMidiNode().indexOf (trackNode);
        if (position >= 0)
            trackLabel = "Tk" + juce::String (position + 1);
    }

    return trackLabel;
}

int AssignmentChipComponent::getPreferredWidth() const
{
    const auto label = buildLabel();
    const auto transpose = transposeLabel ((int) assignment.getProperty (SongIDs::transposeSemitones));

    juce::Font labelFont (juce::FontOptions (10.0f));
    juce::Font monoFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain));

    // swatch (10) + gap + label + gap + transpose + gap + remove button (14)
    return 10 + 4 + juce::GlyphArrangement::getStringWidthInt (labelFont, label) + 4
              + juce::GlyphArrangement::getStringWidthInt (monoFont, transpose) + 4
              + 14 + 10; // trailing padding
}

void AssignmentChipComponent::paint (juce::Graphics& g)
{
    const auto trackId = (juce::int64) assignment.getProperty (SongIDs::trackId);
    const auto trackNode = doc.findTrackById (trackId);
    const auto swatch = trackNode.isValid()
        ? (juce::uint32) (int) trackNode.getProperty (SongIDs::colorArgb)
        : SongsmithColours::textMuted;

    auto bounds = getLocalBounds().toFloat();

    // Dashed border in the track's own colour, per the mockup.
    juce::Path outline;
    outline.addRoundedRectangle (bounds.reduced (0.5f), 2.0f);
    juce::PathStrokeType stroke (1.0f);
    const float dashLengths[] = { 3.0f, 2.0f };
    juce::Path dashed;
    stroke.createDashedStroke (dashed, outline, dashLengths, 2);
    g.setColour (juce::Colour (swatch));
    g.fillPath (dashed);

    auto area = getLocalBounds().reduced (3, 2);
    auto swatchArea = area.removeFromLeft (6).withSizeKeepingCentre (6, 6);
    g.setColour (juce::Colour (swatch));
    g.fillRect (swatchArea);
    area.removeFromLeft (4);

    area.removeFromRight (removeButton.getWidth());

    auto transposeArea = area.removeFromRight (28);
    g.setColour (juce::Colour (SongsmithColours::textMuted));
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain)));
    g.drawText (transposeLabel ((int) assignment.getProperty (SongIDs::transposeSemitones)),
                transposeArea, juce::Justification::centredLeft);

    g.setColour (juce::Colour (SongsmithColours::text));
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawText (buildLabel(), area, juce::Justification::centredLeft);
}

void AssignmentChipComponent::resized()
{
    removeButton.setBounds (getLocalBounds().removeFromRight (14).reduced (1));
}

} // namespace lotro

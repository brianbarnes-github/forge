#include "PartStripComponent.h"
#include "SongsmithColours.h"

#include "Core/LotroInstrument.h"

namespace lotro
{

PartStripComponent::PartStripComponent (SongDocument& document) : doc (document)
{
    header.setText (juce::String::fromUTF8 ("PARTS \xc2\xb7 DROP TRACKS TO ASSIGN"),
                     juce::dontSendNotification);
    header.setColour (juce::Label::backgroundColourId, juce::Colour (SongsmithColours::panelHeader));
    header.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::textMuted));
    header.setFont (juce::Font (juce::FontOptions (11.0f)).withExtraKerningFactor (0.04f));
    addAndMakeVisible (header);

    addAndMakeVisible (addButton);
    addButton.onClick = [this] { addPartClicked(); };

    doc.getPartsNode().addListener (this);
    rebuild();
}

PartStripComponent::~PartStripComponent()
{
    // Cancel before removing the listener so a rebuild can't fire against a
    // component that is mid-destruction.
    cancelPendingUpdate();
    doc.getPartsNode().removeListener (this);
}

void PartStripComponent::rebuild()
{
    slots.clear();

    for (int i = 0; i < doc.getNumParts(); ++i)
    {
        auto partNode = doc.getPart (i);
        auto* slot = slots.add (new PartSlotComponent (doc, partNode));
        slot->setSelected ((juce::int64) partNode.getProperty (SongIDs::partId) == selectedPartId);
        slot->onPartSelected = [this] (juce::int64 partId) { selectPart (partId); };
        addAndMakeVisible (slot);
    }

    resized();
}

void PartStripComponent::selectPart (juce::int64 partId)
{
    selectedPartId = partId;
    for (auto* slot : slots)
        slot->setSelected (slot->getPartId() == partId);

    if (onPartSelected)
        onPartSelected (partId);
}

void PartStripComponent::addPartClicked()
{
    auto newPart = doc.addPart (juce::String (std::string (displayName (LotroInstrument::LuteOfAges))), "");
    selectPart ((juce::int64) newPart.getProperty (SongIDs::partId));
}

void PartStripComponent::resized()
{
    auto area = getLocalBounds();
    header.setBounds (area.removeFromTop (headerHeight));

    addButton.setColour (juce::TextButton::buttonColourId, juce::Colour (SongsmithColours::background));
    addButton.setColour (juce::TextButton::textColourOffId, juce::Colour (SongsmithColours::textMuted));
    addButton.setBounds (area.removeFromRight (addButtonWidth));

    const int n = slots.size();
    if (n == 0)
        return;

    const int slotWidth = area.getWidth() / n;
    for (int i = 0; i < n; ++i)
    {
        auto slotArea = (i == n - 1) ? area : area.removeFromLeft (slotWidth);
        slots[i]->setBounds (slotArea);
    }
}

} // namespace lotro

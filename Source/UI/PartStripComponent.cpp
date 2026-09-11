#include "PartStripComponent.h"
#include "SongsmithColours.h"

#include "Core/LotroInstrument.h"

namespace lotro
{

void PartStripComponent::Row::paint (juce::Graphics& g)
{
    // Border-coloured background: slots are laid out with a 1px gap between
    // adjacent slots (see resized() below), so this shows through as the
    // mockup's gutter (M3) rather than the slots reading as one flat panel.
    g.fillAll (juce::Colour (SongsmithColours::border));
}

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

    viewport.setViewedComponent (&row, false);
    viewport.setScrollBarsShown (false, true); // horizontal only (I3)
    addAndMakeVisible (viewport);

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
    row.slots.clear();

    for (int i = 0; i < doc.getNumParts(); ++i)
    {
        auto partNode = doc.getPart (i);
        auto* slot = row.slots.add (new PartSlotComponent (doc, partNode));
        slot->setSelected ((juce::int64) partNode.getProperty (SongIDs::partId) == selectedPartId);
        slot->onPartSelected = [this] (juce::int64 partId) { selectPart (partId); };
        row.addAndMakeVisible (slot);
    }

    resized();
    repaint(); // M4: empty-state message visibility may have changed.
}

void PartStripComponent::selectPart (juce::int64 partId)
{
    selectedPartId = partId;
    for (auto* slot : row.slots)
        slot->setSelected (slot->getPartId() == partId);

    if (onPartSelected)
        onPartSelected (partId);
}

void PartStripComponent::addPartClicked()
{
    auto newPart = doc.addPart (juce::String (std::string (displayName (LotroInstrument::LuteOfAges))), "");
    selectPart ((juce::int64) newPart.getProperty (SongIDs::partId));
}

void PartStripComponent::paint (juce::Graphics& g)
{
    // M4: first-launch/empty-document affordance — the strip would otherwise
    // just be a header bar over a blank row.
    if (doc.getNumParts() != 0)
        return;

    g.setColour (juce::Colour (SongsmithColours::textMuted));
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    auto area = getLocalBounds().withTrimmedTop (headerHeight).reduced (12);
    g.drawFittedText (juce::String::fromUTF8 (
                           "No parts \xe2\x80\x94 + Add, or Song \xe2\x86\x92 Default parts from tracks"),
                       area, juce::Justification::centred, 2);
}

void PartStripComponent::resized()
{
    auto area = getLocalBounds();
    header.setBounds (area.removeFromTop (headerHeight));

    addButton.setColour (juce::TextButton::buttonColourId, juce::Colour (SongsmithColours::background));
    addButton.setColour (juce::TextButton::textColourOffId, juce::Colour (SongsmithColours::textMuted));
    addButton.setBounds (area.removeFromRight (addButtonWidth));

    viewport.setBounds (area);

    const int n = row.slots.size();
    if (n == 0)
    {
        row.setSize (area.getWidth(), area.getHeight());
        return;
    }

    // I3: below minSlotWidth per slot, switch from equal division to a
    // fixed minSlotWidth per slot and let the viewport scroll horizontally
    // instead of squeezing every slot unreadably thin.
    const bool overflow = n * minSlotWidth > area.getWidth();
    const int  rowWidth = overflow ? n * minSlotWidth : area.getWidth();
    const int  slotWidth = overflow ? minSlotWidth : (area.getWidth() / n);

    row.setSize (rowWidth, area.getHeight());

    int x = 0;
    for (int i = 0; i < n; ++i)
    {
        const bool isLast = (i == n - 1);
        // M3: a 1px gutter after every slot but the last (exposes Row's
        // border-coloured background). In equal-division mode the last slot
        // also absorbs the width lost to integer division, same as before.
        const int w = isLast ? (rowWidth - x) : (slotWidth - 1);
        row.slots[i]->setBounds (x, 0, w, area.getHeight());
        x += w + (isLast ? 0 : 1);
    }
}

} // namespace lotro

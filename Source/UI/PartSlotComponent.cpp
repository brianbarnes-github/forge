#include "PartSlotComponent.h"
#include "SongsmithColours.h"

#include "Core/LotroInstrument.h"

#include <cmath>

namespace lotro
{

PartSlotComponent::PartSlotComponent (SongDocument& document, juce::ValueTree partNode)
    : doc (document), part (std::move (partNode))
{
    jassert (part.hasType (SongIDs::PART));

    for (int i = 0; i < SongDocument::getNumAssignments (part); ++i)
    {
        auto assignment = SongDocument::getAssignment (part, i);
        auto* chip = chips.add (new AssignmentChipComponent (doc, part, assignment));
        addAndMakeVisible (chip);
    }
}

juce::int64 PartSlotComponent::getPartId() const
{
    return (juce::int64) part.getProperty (SongIDs::partId);
}

void PartSlotComponent::setSelected (bool shouldBeSelected)
{
    if (selected == shouldBeSelected) return;
    selected = shouldBeSelected;
    repaint();
}

void PartSlotComponent::paint (juce::Graphics& g)
{
    using namespace SongsmithColours;

    auto bounds = getLocalBounds();
    g.setColour (juce::Colour (selected ? 0xFF3A3A3Au : 0xFF2E2E2Eu));
    g.fillRect (bounds);

    if (dragHighlight)
    {
        g.setColour (juce::Colour (accentAmber).withAlpha (0.15f));
        g.fillRect (bounds);
    }

    if (selected)
    {
        g.setColour (juce::Colour (accentAmber));
        g.fillRect (bounds.removeFromTop (2));
    }

    auto header = getLocalBounds().reduced (6, 4).removeFromTop (16);

    auto xArea = header.removeFromLeft (16);
    g.setColour (juce::Colour (textMuted));
    g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain));
    g.drawText (juce::String ((int) part.getProperty (SongIDs::x)), xArea, juce::Justification::centredLeft);

    const auto instrumentName = part.getProperty (SongIDs::instrumentName).toString();
    juce::Font badgeFont (10.0f);
    const int badgeWidth = (int) std::ceil (badgeFont.getStringWidthFloat (instrumentName)) + 12;
    auto badgeArea = header.removeFromLeft (badgeWidth);
    g.setColour (juce::Colour (accentAmber).withAlpha (0.25f));
    g.fillRoundedRectangle (badgeArea.reduced (0, 2).toFloat(), 2.0f);
    g.setColour (juce::Colour (accentAmber));
    g.setFont (badgeFont);
    g.drawText (instrumentName, badgeArea, juce::Justification::centred);

    header.removeFromLeft (6);
    g.setColour (juce::Colour (text));
    g.setFont (juce::Font (10.0f));
    g.drawText (part.getProperty (SongIDs::label).toString(), header, juce::Justification::centredLeft);

    if (chips.isEmpty())
    {
        auto placeholder = getLocalBounds().reduced (6).withTrimmedTop (20);

        juce::Path outline;
        outline.addRoundedRectangle (placeholder.toFloat(), 2.0f);
        juce::PathStrokeType stroke (1.0f);
        const float dashLengths[] = { 3.0f, 2.0f };
        juce::Path dashed;
        stroke.createDashedStroke (dashed, outline, dashLengths, 2);
        g.setColour (juce::Colour (0xFF555555u));
        g.fillPath (dashed);

        g.setColour (juce::Colour (textMuted));
        g.setFont (juce::Font (10.0f));
        g.drawText ("drop here", placeholder, juce::Justification::centred);
    }
}

void PartSlotComponent::resized()
{
    auto body = getLocalBounds().reduced (6, 4).withTrimmedTop (18);

    int x = body.getX();
    int y = body.getY();
    const int gap = 3;
    const int rowHeight = AssignmentChipComponent::chipHeight;

    for (auto* chip : chips)
    {
        const int w = chip->getPreferredWidth();
        if (x != body.getX() && x + w > body.getRight())
        {
            x = body.getX();
            y += rowHeight + gap;
        }
        chip->setBounds (x, y, w, rowHeight);
        x += w + gap;
    }
}

void PartSlotComponent::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showContextMenu();
        return;
    }

    if (onPartSelected)
        onPartSelected (getPartId());
}

bool PartSlotComponent::isInterestedInDragSource (const SourceDetails& details)
{
    if (! details.description.isInt64())
        return false;

    return doc.findTrackById ((juce::int64) details.description).isValid();
}

void PartSlotComponent::itemDragEnter (const SourceDetails&)
{
    dragHighlight = true;
    repaint();
}

void PartSlotComponent::itemDragExit (const SourceDetails&)
{
    dragHighlight = false;
    repaint();
}

void PartSlotComponent::itemDropped (const SourceDetails& details)
{
    dragHighlight = false;

    if (details.description.isInt64())
        doc.assignTrackToPart (getPartId(), (juce::int64) details.description);

    // Note: assignTrackToPart mutating the document fires PartStripComponent's
    // listener, which rebuilds the whole strip (destroying/recreating this
    // slot) — nothing below this line may touch `this` again.
}

void PartSlotComponent::showContextMenu()
{
    juce::PopupMenu menu;

    juce::PopupMenu instrumentMenu;
    const auto names = allInstrumentNames();
    const auto current = part.getProperty (SongIDs::instrumentName).toString();
    for (size_t i = 0; i < names.size(); ++i)
    {
        juce::String name (names[i].data(), names[i].size());
        instrumentMenu.addItem ((int) i + 1, name, true, name == current);
    }
    menu.addSubMenu ("Instrument", instrumentMenu);
    menu.addItem (1000, "Rename...");
    menu.addSeparator();
    menu.addItem (1001, "Remove part");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
        [this, names] (int result)
        {
            if (result == 0) return;

            if (result == 1000) { promptRename(); return; }
            if (result == 1001) { doc.removePart (getPartId()); return; }

            if (result >= 1 && result <= (int) names.size())
            {
                const auto& chosen = names[(size_t) result - 1];
                doc.setProperty (part, SongIDs::instrumentName,
                                  juce::String (chosen.data(), chosen.size()));
            }
            // Any branch above that mutates the document may have already
            // destroyed `this` via PartStripComponent::rebuild() — nothing
            // after this point may touch member state.
        });
}

void PartSlotComponent::promptRename()
{
    auto* aw = new juce::AlertWindow ("Rename Part", "Label:", juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("label", part.getProperty (SongIDs::label).toString());
    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::ValueTree partRef = part;
    SongDocument* docPtr = &doc;

    aw->enterModalState (true, juce::ModalCallbackFunction::create (
        [aw, partRef, docPtr] (int result) mutable
        {
            if (result == 1)
                docPtr->setProperty (partRef, SongIDs::label, aw->getTextEditorContents ("label"));
        }), true);
}

} // namespace lotro

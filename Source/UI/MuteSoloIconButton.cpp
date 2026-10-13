#include "MuteSoloIconButton.h"
#include "SongsmithColours.h"

namespace lotro
{

namespace
{
    // Off-state fill of the mute / solo icon buttons: a step lighter than the
    // row backgrounds so the button reads as a control on either.
    constexpr juce::uint32 iconButtonOff = 0xFF4A4A4A;

    // The glyphs are drawn in a unit square, then scaled into the button.
    juce::Path speakerBody()
    {
        juce::Path p;
        p.addRectangle (0.08f, 0.37f, 0.16f, 0.26f);
        p.startNewSubPath (0.24f, 0.37f);
        p.lineTo (0.48f, 0.16f);
        p.lineTo (0.48f, 0.84f);
        p.lineTo (0.24f, 0.63f);
        p.closeSubPath();
        return p;
    }

    // A slash across the speaker when muted, two sound waves when not.
    juce::Path speakerStrokes (bool muted)
    {
        juce::Path p;
        if (muted)
        {
            p.startNewSubPath (0.10f, 0.10f);
            p.lineTo (0.90f, 0.90f);
        }
        else
        {
            const float right = juce::MathConstants<float>::halfPi;
            p.addCentredArc (0.48f, 0.5f, 0.18f, 0.18f, 0.0f, right - 0.75f, right + 0.75f, true);
            p.addCentredArc (0.48f, 0.5f, 0.36f, 0.36f, 0.0f, right - 0.75f, right + 0.75f, true);
        }
        return p;
    }

    juce::Path headphoneCups()
    {
        juce::Path p;
        p.addRoundedRectangle (0.08f, 0.52f, 0.20f, 0.36f, 0.05f);
        p.addRoundedRectangle (0.72f, 0.52f, 0.20f, 0.36f, 0.05f);
        return p;
    }

    juce::Path headphoneBand()
    {
        juce::Path p;
        const float halfPi = juce::MathConstants<float>::halfPi;
        p.addCentredArc (0.5f, 0.58f, 0.40f, 0.44f, 0.0f, -halfPi, halfPi, true);
        return p;
    }
}

void MuteSoloIconButton::paintButton (juce::Graphics& g, bool isOver, bool isDown)
{
    const bool on = getToggleState();
    auto fill = on ? onColourValue : juce::Colour (iconButtonOff);
    if (isOver)
        fill = fill.brighter (0.15f);
    if (isDown)
        fill = fill.darker (0.2f);

    const auto bounds = getLocalBounds().toFloat();
    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 3.0f);

    // Glyph in a square centred in the button, inset 4 px.
    const auto inner = bounds.reduced (4.0f);
    const float side = juce::jmin (inner.getWidth(), inner.getHeight());
    if (side <= 0.0f)
        return;
    const auto square = inner.withSizeKeepingCentre (side, side);
    const auto toButton = juce::AffineTransform::scale (side).translated (square.getX(), square.getY());

    juce::Path filled, stroked;
    if (kind == Kind::Mute)
    {
        filled = speakerBody();
        stroked = speakerStrokes (on);
    }
    else
    {
        filled = headphoneCups();
        stroked = headphoneBand();
    }
    filled.applyTransform (toButton);
    stroked.applyTransform (toButton);

    g.setColour (juce::Colour (on ? SongsmithColours::text : SongsmithColours::textMuted));
    g.fillPath (filled);
    g.strokePath (stroked, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

}

#include "UI/Theme/ThemePaint.h"

#include "UI/SongsmithColours.h"

namespace lotro::ThemePaint
{

void paintPanelGradient (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    if (bounds.isEmpty())
        return;
    const auto b = bounds.toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colour (SongsmithColours::surfaceTop), 0.0f, b.getY(),
                                             juce::Colour (SongsmithColours::surfaceBottom), 0.0f, b.getBottom(), false));
    g.fillRect (bounds);
    g.setColour (juce::Colour (SongsmithColours::edgeHighlight));
    g.fillRect (bounds.getX(), bounds.getY(), bounds.getWidth(), 1);
    g.setColour (juce::Colour (SongsmithColours::border));
    g.fillRect (bounds.getX(), bounds.getBottom() - 1, bounds.getWidth(), 1);
}

void paintNoteBody (juce::Graphics& g, juce::Rectangle<int> rect, juce::Colour fill, const NoteStyle& style)
{
    if (rect.isEmpty())
        return;

    // Fast path: zoomed-out notes are too small for rounding, gradient or edge to show.
    if (rect.getHeight() < smallNoteSize || rect.getWidth() < smallNoteSize)
    {
        g.setColour (fill);
        g.fillRect (rect);
        if (style.selected)
        {
            g.setColour (juce::Colours::white);
            g.drawRect (rect, 1);
        }
        return;
    }

    const auto r = rect.toFloat();
    const float radius = juce::jmin (3.0f, r.getHeight() / 3.0f, r.getWidth() / 2.0f);

    if (rect.getHeight() >= glowMinHeight)
    {
        // Outermost ring first, fainter; the inner ring adds on top, so alpha falls with distance.
        for (int ring = noteOverhang; ring >= 1; --ring)
        {
            g.setColour (fill.withAlpha (0.25f / (float) ring));
            g.fillRoundedRectangle (r.expanded ((float) ring), radius + (float) ring);
        }
    }

    g.setGradientFill (juce::ColourGradient (fill.brighter (0.15f), 0.0f, r.getY(),
                                             fill.darker (0.15f), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);

    g.setColour (style.border.value_or (fill.brighter (0.4f)));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);

    if (style.selected)
    {
        g.setColour (juce::Colours::white);
        const float thickness = juce::jmin (2.0f, r.getHeight() / 2.0f);
        g.drawRoundedRectangle (r.reduced (thickness / 2.0f), radius, thickness);
    }
}

void paintDiamondNote (juce::Graphics& g, juce::Rectangle<int> square, juce::Rectangle<int> lengthLine,
                       juce::Colour fill, const NoteStyle& style)
{
    if (square.isEmpty())
        return;

    if (! lengthLine.isEmpty())
    {
        g.setColour (fill.withAlpha (0.7f));
        g.fillRect (lengthLine);
        if (style.selected)
        {
            g.setColour (juce::Colours::white);
            g.drawRect (lengthLine.expanded (1), 1);
        }
    }

    const auto r = square.toFloat();
    juce::Path diamond;
    diamond.startNewSubPath (r.getCentreX(), r.getY());
    diamond.lineTo (r.getRight(), r.getCentreY());
    diamond.lineTo (r.getCentreX(), r.getBottom());
    diamond.lineTo (r.getX(), r.getCentreY());
    diamond.closeSubPath();

    // Fast path: zoomed-out notes are too small for the gradient or edge to show.
    if (square.getHeight() < smallNoteSize)
    {
        g.setColour (fill);
        g.fillPath (diamond);
    }
    else
    {
        g.setGradientFill (juce::ColourGradient (fill.brighter (0.15f), 0.0f, r.getY(),
                                                 fill.darker (0.15f), 0.0f, r.getBottom(), false));
        g.fillPath (diamond);
        g.setColour (style.border.value_or (fill.brighter (0.4f)));
        g.strokePath (diamond, juce::PathStrokeType (1.0f));
    }

    if (style.selected)
    {
        g.setColour (juce::Colours::white);
        g.strokePath (diamond, juce::PathStrokeType (1.0f));
    }
}

juce::Path anvilPath()
{
    juce::Path p;
    p.startNewSubPath (0.0f, 0.18f);                 // horn tip
    p.quadraticTo (0.18f, 0.18f, 0.30f, 0.10f);      // horn's upper curve
    p.lineTo (1.0f, 0.10f);                          // face
    p.lineTo (1.0f, 0.30f);                          // heel
    p.lineTo (0.78f, 0.36f);
    p.quadraticTo (0.66f, 0.42f, 0.66f, 0.55f);      // waist, right
    p.lineTo (0.80f, 0.70f);                         // foot, right
    p.lineTo (0.30f, 0.70f);                         // foot, left
    p.lineTo (0.44f, 0.55f);
    p.quadraticTo (0.44f, 0.42f, 0.30f, 0.34f);      // waist, left
    p.quadraticTo (0.14f, 0.30f, 0.0f, 0.18f);       // horn's underside
    p.closeSubPath();
    return p;
}

void paintLogo (juce::Graphics& g, juce::Rectangle<float> area, bool withWordmark)
{
    const auto copper = juce::Colour (SongsmithColours::accentCopper);
    auto markArea = withWordmark ? area.removeFromLeft (area.getHeight() * 1.4f) : area;
    auto mark = anvilPath();
    mark.applyTransform (mark.getTransformToScaleToFit (markArea.reduced (2.0f), true));
    g.setColour (copper.withAlpha (0.35f));
    g.fillPath (mark);
    g.setColour (copper);
    g.strokePath (mark, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (! withWordmark)
        return;
    area.removeFromLeft (8.0f);
    auto title = area.removeFromTop (area.getHeight() * 0.62f);
    g.setColour (copper);
    g.setFont (juce::FontOptions (title.getHeight() * 0.8f));
    g.drawText ("Songsmith", title, juce::Justification::bottomLeft, false);
    g.setColour (juce::Colour (SongsmithColours::textMuted));
    juce::Font caption (juce::FontOptions (juce::jmax (8.0f, area.getHeight() * 0.6f)));
    caption.setExtraKerningFactor (0.25f);
    g.setFont (caption);
    g.drawText ("MIDI EDITOR", area, juce::Justification::topLeft, false);
}

juce::Image renderAppIcon (int size)
{
    juce::Image image (juce::Image::ARGB, size, size, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    const float inset = (float) size * 0.08f;
    auto mark = anvilPath();
    mark.applyTransform (mark.getTransformToScaleToFit (juce::Rectangle<float> (inset, inset, (float) size - 2 * inset,
                                                                                (float) size - 2 * inset), true));
    g.setColour (juce::Colour (SongsmithColours::accentCopper));
    g.fillPath (mark);
    return image;
}

}

#include "TrackHeadComponent.h"
#include "SongsmithColours.h"

#include <utility>

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

void TrackHeadComponent::IconButton::paintButton (juce::Graphics& g, bool isOver, bool isDown)
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

TrackHeadComponent::TrackHeadComponent (juce::ValueTree trackNode, int displayIndex)
    : track (trackNode), index (displayIndex)
{
    jassert (track.hasType (SongIDs::MIDI_TRACK));
    setInterceptsMouseClicks (false, true);
    setOpaque (false);

    volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    volumeSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    volumeSlider.setRange (0.0, 100.0, 1.0);
    volumeSlider.setDoubleClickReturnValue (true, 100.0);
    volumeSlider.setTooltip ("Playback volume");
    volumeSlider.setValue (trackPlaybackVolume (track), juce::dontSendNotification);
    volumeSlider.onDragStart = [this] { startsGesture = true; };
    volumeSlider.onDragEnd = [this] { startsGesture = true; };
    volumeSlider.onValueChange = [this]
    {
        const bool starts = std::exchange (startsGesture, false);
        if (onVolumeChanged)
            onVolumeChanged ((int) volumeSlider.getValue(), starts);
    };

    muteButton.setTooltip ("Mute");
    soloButton.setTooltip ("Solo");
    muteButton.onClick = [this] { if (onMuteToggled) onMuteToggled (muteButton.getToggleState()); };
    soloButton.onClick = [this] { if (onSoloToggled) onSoloToggled (soloButton.getToggleState()); };

    const bool conductor = isConductorTrack();
    for (auto* c : std::initializer_list<juce::Component*> { &muteButton, &soloButton, &volumeSlider })
    {
        addChildComponent (*c);
        c->setVisible (! conductor);
    }
}

TrackHeadLayout TrackHeadComponent::layoutFor (juce::Rectangle<int> bounds, bool conductor)
{
    TrackHeadLayout l;
    auto area = bounds;
    auto top = area.removeFromTop (area.getHeight() / 2);
    auto bottom = area;

    top.removeFromLeft (sidePadding);
    l.index = top.removeFromLeft (indexWidth);
    if (conductor)
    {
        l.name = top.withTrimmedRight (rightPadding);
        return l;
    }
    l.swatch = top.removeFromLeft (swatchSize + gap).withSizeKeepingCentre (swatchSize, swatchSize);
    l.name = top.withTrimmedRight (rightPadding);

    bottom.removeFromLeft (sidePadding);
    const int iconHeight = juce::jmin (iconSize, bottom.getHeight() - 2);
    l.mute = bottom.removeFromLeft (iconSize).withSizeKeepingCentre (iconSize, iconHeight);
    bottom.removeFromLeft (gap);
    l.solo = bottom.removeFromLeft (iconSize).withSizeKeepingCentre (iconSize, iconHeight);
    bottom.removeFromLeft (gap);
    l.volume = bottom.withTrimmedRight (rightPadding).reduced (0, 2);
    return l;
}

void TrackHeadComponent::resized()
{
    const auto l = layoutFor (getLocalBounds(), isConductorTrack());
    muteButton.setBounds (l.mute);
    soloButton.setBounds (l.solo);
    volumeSlider.setBounds (l.volume);
}

void TrackHeadComponent::paint (juce::Graphics& g)
{
    const bool conductor = isConductorTrack();
    const auto l = layoutFor (getLocalBounds(), conductor);

    if (index != 0)
    {
        g.setColour (juce::Colour (SongsmithColours::textMuted));
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain)));
        g.drawText (juce::String (index), l.index, juce::Justification::centredLeft);
    }

    if (! conductor)
    {
        g.setColour (juce::Colour ((juce::uint32) (int) track.getProperty (SongIDs::colorArgb)));
        g.fillRect (l.swatch);
    }

    g.setColour (juce::Colour (conductor ? SongsmithColours::textMuted : SongsmithColours::text));
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText (track.getProperty (SongIDs::name).toString(), l.name, juce::Justification::centredLeft, true);
}

void TrackHeadComponent::setMuteSolo (bool muted, bool soloed)
{
    muteButton.setToggleState (muted, juce::dontSendNotification);
    soloButton.setToggleState (soloed, juce::dontSendNotification);
}

void TrackHeadComponent::refreshFromTrack()
{
    volumeSlider.setValue (trackPlaybackVolume (track), juce::dontSendNotification);
    repaint();
}

juce::Rectangle<int> TrackHeadComponent::swatchBounds() const
{
    return layoutFor (getLocalBounds(), isConductorTrack()).swatch;
}

} // namespace lotro

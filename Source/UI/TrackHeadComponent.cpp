#include "TrackHeadComponent.h"
#include "SongsmithColours.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <utility>

namespace lotro
{

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
    // The wheel belongs to the track list (scroll / row resize over the heads).
    volumeSlider.setScrollWheelEnabled (false);
    // NN% in a bubble while dragging (a bubble needs no TooltipWindow).
    volumeSlider.setTextValueSuffix ("%");
    volumeSlider.setPopupDisplayEnabled (true, false, nullptr);
    volumeSlider.setTooltip ("Playback volume");
    volumeSlider.setValue (trackPlaybackVolume (track), juce::dontSendNotification);
    // JUCE brackets mouse drags, wheel moves and double-clicks with
    // onDragStart / onDragEnd but not keyboard steps, so any change outside a
    // bracket starts its own gesture.
    volumeSlider.onDragStart = [this] { dragging = true; dragHasChanged = false; };
    volumeSlider.onDragEnd = [this] { dragging = false; };
    volumeSlider.onValueChange = [this]
    {
        const bool starts = ! dragging || ! std::exchange (dragHasChanged, true);
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
        auto row = bottom.withTrimmedLeft (sidePadding + indexWidth).withTrimmedRight (rightPadding);
        l.tempo = row.removeFromLeft (tempoWidth);
        row.removeFromLeft (gap);
        l.key = row;
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

    if (conductor && tempoText.isNotEmpty())
        g.drawText (tempoText, l.tempo, juce::Justification::centredLeft, true);
    if (conductor && keyText.isNotEmpty())
        g.drawText (keyText, l.key, juce::Justification::centredLeft, true);
}

void TrackHeadComponent::setTempoText (const juce::String& text)
{
    if (tempoText == text)
        return;
    tempoText = text;
    repaint();
}

void TrackHeadComponent::setKeyText (const juce::String& text)
{
    if (keyText == text)
        return;
    keyText = text;
    repaint();
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

juce::PopupMenu TrackHeadComponent::buildContextMenu() const
{
    juce::PopupMenu menu;
    menu.addItem (1, juce::String::fromUTF8 ("Rename\xe2\x80\xa6"), ! isConductorTrack());
    if (isConductorTrack())
        menu.addItem (2, juce::String::fromUTF8 ("Tempo and Meter\xe2\x80\xa6"));
    return menu;
}

void TrackHeadComponent::showContextMenu()
{
    juce::Component::SafePointer<TrackHeadComponent> safe (this);
    buildContextMenu().showMenuAsync (juce::PopupMenu::Options(), [safe] (int id)
    {
        if (safe != nullptr)
            safe->contextMenuChosen (id);
    });
}

void TrackHeadComponent::contextMenuChosen (int itemId)
{
    if (itemId == 1)
        beginRename();
    else if (itemId == 2 && isConductorTrack() && onTempoMeterRequested)
        onTempoMeterRequested();
}

void TrackHeadComponent::beginRename()
{
    if (isConductorTrack() || renameEditor != nullptr)
        return;

    renameEditor = std::make_unique<juce::TextEditor>();
    renameEditor->setFont (juce::Font (juce::FontOptions (11.0f)));
    renameEditor->setText (track.getProperty (SongIDs::name).toString(), juce::dontSendNotification);
    renameEditor->onReturnKey = [this] { if (renameEditor != nullptr) commitRename (renameEditor->getText()); };
    renameEditor->onEscapeKey = [this] { endRename(); };
    renameEditor->onFocusLost = [this] { endRename(); };
    addAndMakeVisible (*renameEditor);
    renameEditor->setBounds (layoutFor (getLocalBounds(), false).name);
    renameEditor->selectAll();
    if (isShowing())
        renameEditor->grabKeyboardFocus();
}

void TrackHeadComponent::commitRename (const juce::String& newName)
{
    const auto trimmed = newName.trim();
    const auto old = track.getProperty (SongIDs::name).toString();
    endRename();
    if (trimmed.isNotEmpty() && trimmed != old && onRenamed)
        onRenamed (trimmed);
}

void TrackHeadComponent::endRename()
{
    if (renameEditor == nullptr)
        return;
    // The member is cleared first so the focus-lost callback that removal
    // triggers is a no-op; the editor outlives the callback that got us here.
    std::shared_ptr<juce::TextEditor> dying (std::move (renameEditor));
    // The removal posts a focus loss that can arrive after this head is gone,
    // so the detached editor must not keep [this]. Clearing the callback that
    // is running is safe: none of them touches its closure after this returns.
    dying->onReturnKey = nullptr;
    dying->onEscapeKey = nullptr;
    dying->onFocusLost = nullptr;
    removeChildComponent (dying.get());
    juce::MessageManager::callAsync ([dying] {});
}

namespace
{
    // The callout's content; its destruction (the callout was dismissed) reports the close.
    class PickerContent : public juce::ColourSelector, private juce::ChangeListener
    {
    public:
        PickerContent (juce::Colour initial, std::function<void (juce::Colour)> onPick, std::function<void()> onClose)
            : juce::ColourSelector (juce::ColourSelector::showColourspace | juce::ColourSelector::showSliders),
              pick (std::move (onPick)), onClosed (std::move (onClose))
        {
            setCurrentColour (initial, juce::dontSendNotification);
            setSize (240, 260);
            addChangeListener (this);
        }
        ~PickerContent() override
        {
            removeChangeListener (this);
            if (onClosed)
                onClosed();
        }
    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override { pick (getCurrentColour()); }
        std::function<void (juce::Colour)> pick;
        std::function<void()> onClosed;
    };
}

void TrackHeadComponent::openColourPicker()
{
    if (isConductorTrack() || pickerOpen)
        return;

    pickerOpen = true;
    pickerFirstChange = true;
    juce::Component::SafePointer<TrackHeadComponent> safe (this);
    auto content = std::make_unique<PickerContent> (
        juce::Colour ((juce::uint32) (int) track.getProperty (SongIDs::colorArgb)),
        [safe] (juce::Colour c) { if (safe != nullptr) safe->colourPicked (c); },
        [safe] { if (safe != nullptr) safe->colourPickerClosed(); });
    pickerBox = &juce::CallOutBox::launchAsynchronously (std::move (content), localAreaToGlobal (swatchBounds()), nullptr);
}

juce::ColourSelector* TrackHeadComponent::colourSelectorForTesting() const
{
    return pickerBox != nullptr ? dynamic_cast<juce::ColourSelector*> (pickerBox->getChildComponent (0)) : nullptr;
}

void TrackHeadComponent::dismissColourPickerForTesting()
{
    if (pickerBox != nullptr)
        pickerBox->dismiss();
}

TrackHeadComponent::~TrackHeadComponent()
{
    // The callout deletes itself later; its content's close report finds this head gone.
    if (pickerBox != nullptr)
        pickerBox->dismiss();
}

void TrackHeadComponent::colourPicked (juce::Colour c)
{
    if (onColourChanged)
        onColourChanged (c.getARGB(), std::exchange (pickerFirstChange, false));
}

void TrackHeadComponent::colourPickerClosed()
{
    pickerFirstChange = true;
    pickerOpen = false;
}

juce::Rectangle<int> TrackHeadComponent::swatchBounds() const
{
    return layoutFor (getLocalBounds(), isConductorTrack()).swatch;
}

} // namespace lotro

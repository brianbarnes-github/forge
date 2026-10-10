#include "UI/SongDocument.h"
#include "UI/TrackHeadComponent.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <string>
#include <vector>

using namespace lotro;

namespace
{
    void click (juce::Button& b)
    {
        b.triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    }
}

TEST_CASE ("TrackHead layout: two rows, nothing overlaps or leaves the bounds, at min and max heights", "[track-head][layout]")
{
    for (const int h : { 45, 62, 136 })   // minRowHeight(30)+band(16)-divider(1), default, max
    {
        const juce::Rectangle<int> bounds (0, 0, 198, h);
        const auto l = TrackHeadComponent::layoutFor (bounds, false);
        const juce::Rectangle<int> parts[] = { l.index, l.swatch, l.name, l.mute, l.solo, l.volume };
        for (const auto& r : parts)
        {
            CHECK (bounds.contains (r));
            CHECK (! r.isEmpty());
        }
        CHECK (l.mute.getY() >= l.name.getBottom());          // row 2 below row 1
        CHECK (! l.mute.intersects (l.solo));
        CHECK (! l.solo.intersects (l.volume));
        CHECK (! l.swatch.intersects (l.name));
        CHECK (! l.index.intersects (l.swatch));
    }
}

TEST_CASE ("TrackHead layout: conductor has only the index slot and the name", "[track-head][layout]")
{
    const auto l = TrackHeadComponent::layoutFor ({ 0, 0, 198, 62 }, true);
    CHECK (l.swatch.isEmpty());
    CHECK (l.mute.isEmpty());
    CHECK (l.solo.isEmpty());
    CHECK (l.volume.isEmpty());
    CHECK (! l.index.isEmpty());
    CHECK (! l.name.isEmpty());
    CHECK (! l.index.intersects (l.name));
}

TEST_CASE ("TrackHead: M and S fire their callbacks with the new state", "[track-head][mutesolo]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    TrackHeadComponent head (doc.addTrack ("A", (int) 0xFFAABBCC, 1, 1), 1);
    head.setBounds (0, 0, 198, 62);
    bool muted = false, soloed = false;
    head.onMuteToggled = [&] (bool s) { muted = s; };
    head.onSoloToggled = [&] (bool s) { soloed = s; };
    click (head.muteButtonForTesting());
    click (head.soloButtonForTesting());
    CHECK (muted);
    CHECK (soloed);
}

TEST_CASE ("TrackHead: setMuteSolo reflects state without firing callbacks", "[track-head][mutesolo]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    TrackHeadComponent head (doc.addTrack ("A", (int) 0xFFAABBCC, 1, 1), 1);
    bool fired = false;
    head.onMuteToggled = [&] (bool) { fired = true; };
    head.setMuteSolo (true, false);
    CHECK (head.muteButtonForTesting().getToggleState());
    CHECK (! head.soloButtonForTesting().getToggleState());
    CHECK (! fired);
}

TEST_CASE ("TrackHead: the slider shows the saved volume and reports user changes only", "[track-head][volume]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCC, 1, 1);
    track.setProperty (SongIDs::playbackVolume, 40, nullptr);
    TrackHeadComponent head (track, 1);
    CHECK ((int) head.volumeSliderForTesting().getValue() == 40);

    int reported = -1;
    head.onVolumeChanged = [&] (int v, bool) { reported = v; };
    track.setProperty (SongIDs::playbackVolume, 70, nullptr);
    head.refreshFromTrack();
    CHECK ((int) head.volumeSliderForTesting().getValue() == 70);
    CHECK (reported == -1);                       // programmatic refresh is silent

    head.volumeSliderForTesting().setValue (20, juce::sendNotificationSync);
    CHECK (reported == 20);
}

TEST_CASE ("TrackHead: double-clicking the slider resets it to 100, each as its own gesture", "[track-head][volume]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCC, 1, 1);
    track.setProperty (SongIDs::playbackVolume, 40, nullptr);
    TrackHeadComponent head (track, 1);
    auto& slider = head.volumeSliderForTesting();
    slider.setBounds (0, 0, 120, 18);
    std::vector<std::string> reports;   // "+" marks a change that starts a gesture
    head.onVolumeChanged = [&] (int v, bool starts) { reports.push_back (std::to_string (v) + (starts ? "+" : "")); };

    const auto doubleClick = [&slider]
    {
        slider.mouseDoubleClick (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                                   juce::Point<float> (30.0f, 9.0f), juce::ModifierKeys(),
                                                   0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &slider, &slider,
                                                   juce::Time::getCurrentTime(), juce::Point<float> (30.0f, 9.0f),
                                                   juce::Time::getCurrentTime(), 2, false));
    };
    doubleClick();
    slider.setValue (25, juce::sendNotificationSync);
    doubleClick();

    const std::vector<std::string> expected { "100+", "25+", "100+" };
    CHECK (reports == expected);
}

TEST_CASE ("TrackHead: the conductor head has no controls", "[track-head]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    TrackHeadComponent head (doc.getConductorTrack(), 0);
    head.setBounds (0, 0, 198, 62);
    CHECK (! head.muteButtonForTesting().isVisible());
    CHECK (! head.soloButtonForTesting().isVisible());
    CHECK (! head.volumeSliderForTesting().isVisible());
    CHECK (head.swatchBounds().isEmpty());
}

TEST_CASE ("TrackHead: setMuteSolo reflects a solo-only state too", "[track-head][mutesolo]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    TrackHeadComponent head (doc.addTrack ("A", (int) 0xFFAABBCC, 1, 1), 1);
    bool fired = false;
    head.onSoloToggled = [&] (bool) { fired = true; };
    head.setMuteSolo (true, true);
    head.setMuteSolo (false, true);
    CHECK (! head.muteButtonForTesting().getToggleState());
    CHECK (head.soloButtonForTesting().getToggleState());
    CHECK (! fired);
}

TEST_CASE ("TrackHead: each drag opens one gesture; changes outside a drag each open their own", "[track-head][volume]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("A", (int) 0xFFAABBCC, 1, 1);
    track.setProperty (SongIDs::playbackVolume, 10, nullptr);
    TrackHeadComponent head (track, 1);
    auto& slider = head.volumeSliderForTesting();
    std::vector<std::string> reports;   // "+" marks a change that starts a gesture
    head.onVolumeChanged = [&] (int v, bool starts) { reports.push_back (std::to_string (v) + (starts ? "+" : "")); };

    // Two lone changes in a row. Keyboard steps are not bracketed by JUCE with
    // onDragStart / onDragEnd, so each must still open its own step.
    slider.keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
    slider.keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
    slider.setValue (72, juce::sendNotificationSync);
    slider.setValue (74, juce::sendNotificationSync);

    // A drag: start, three value changes, end.
    slider.onDragStart();
    slider.setValue (61, juce::sendNotificationSync);
    slider.setValue (57, juce::sendNotificationSync);
    slider.setValue (52, juce::sendNotificationSync);
    slider.onDragEnd();

    // Lone changes after the drag.
    slider.keyPressed (juce::KeyPress (juce::KeyPress::leftKey));
    slider.setValue (33, juce::sendNotificationSync);

    const std::vector<std::string> expected {
        "11+", "12+", "72+", "74+",
        "61+", "57", "52",
        "51+", "33+" };
    CHECK (reports == expected);
}

TEST_CASE ("TrackHead: resized places the controls at layoutFor's rectangles", "[track-head][layout]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    TrackHeadComponent head (doc.addTrack ("A", (int) 0xFFAABBCC, 1, 1), 3);
    head.setBounds (0, 0, 210, 80);
    const auto l = TrackHeadComponent::layoutFor (head.getLocalBounds(), false);
    CHECK (head.muteButtonForTesting().isVisible());
    CHECK (head.soloButtonForTesting().isVisible());
    CHECK (head.volumeSliderForTesting().isVisible());
    CHECK (head.muteButtonForTesting().getBounds() == l.mute);
    CHECK (head.soloButtonForTesting().getBounds() == l.solo);
    CHECK (head.volumeSliderForTesting().getBounds() == l.volume);
    CHECK (head.swatchBounds() == l.swatch);
    CHECK (! head.swatchBounds().isEmpty());
}

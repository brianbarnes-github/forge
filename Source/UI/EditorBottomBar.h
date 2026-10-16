#pragma once

#include "Playback/TransportStrip.h"
#include "Theme/ThemePaint.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace lotro
{

// The MIDI editor's fixed bar along its bottom edge: the transport strip's height and
// background, no dividers, never docked or floated. An empty home for editor-only
// controls; add them as children and lay them out in resized().
class EditorBottomBar : public juce::Component
{
public:
    static constexpr int height = TransportStrip::height;

    void paint (juce::Graphics& g) override { ThemePaint::paintPanelGradient (g, getLocalBounds()); }
};

} // namespace lotro

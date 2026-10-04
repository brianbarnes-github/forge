#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace lotro
{

// A thin click/drag strip above a note canvas: pressing it moves the playhead.
class TimelineRuler : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr int height = 14;

    explicit TimelineRuler (std::function<double (int x)> tickForXIn) : tickForX (std::move (tickForXIn))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip ("Click or drag to move the playhead");
    }

    std::function<void (double tick)> onSeek;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override { seekTo (e.x); }
    void mouseDrag (const juce::MouseEvent& e) override { seekTo (e.x); }

private:
    void seekTo (int x)
    {
        if (onSeek && tickForX)
            onSeek (juce::jmax (0.0, tickForX (x)));
    }

    std::function<double (int)> tickForX;
};

} // namespace lotro

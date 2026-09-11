#pragma once

#include "SongDocument.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// One row in TrackListComponent's list, representing a single MIDI_TRACK.
// Phase 4, component B1 of the Songsmith plan's "Part strip UI" phase — see
// docs/ARCHITECTURE.md §9.x and the Songsmith UI Guide mockup's "MIDI track
// list" block for the visual spec.
namespace lotro
{

class TrackRowComponent : public juce::Component
{
public:
    // trackNode must be a valid MIDI_TRACK node; displayIndex is its 1-based
    // position in SOURCE_MIDI at construction time (TrackListComponent
    // rebuilds every row from scratch on any SOURCE_MIDI change, so this
    // never goes stale in place).
    TrackRowComponent (juce::ValueTree trackNode, int displayIndex);

    void setSelected (bool shouldBeSelected);

    juce::int64 getTrackId() const;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;

    // Fixed row height used by TrackListComponent to lay out its content.
    static constexpr int rowHeight = 34;

    // Fired on a plain (non-drag) click, with this row's trackId.
    std::function<void (juce::int64)> onTrackSelected;

private:
    juce::String buildSecondLine() const;

    juce::ValueTree track;
    int             index;
    bool            selected = false;
};

} // namespace lotro

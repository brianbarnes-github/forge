#pragma once

#include "PianoRollGeometry.h"
#include "PianoRollNoteSource.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Phase 5 — the shared piano-roll component (source role only this phase:
// rectangles, keyboard gutter, gridlines, scroll, minimal ctrl+wheel zoom —
// no note editing). Phase 6 adds a Preview role with range-band/ghost-note
// overlays to the same component; the Role enum below is the seam for that,
// even though Preview does nothing yet.
namespace lotro
{

class PianoRollComponent : public juce::Component
{
public:
    enum class Role { Source, Preview };

    explicit PianoRollComponent (Role roleIn = Role::Source);

    Role getRole() const noexcept { return role; }

    // Repoints the roll at a new note source (or nullptr for "no track
    // selected", which paints an empty roll) and refits the geometry/content
    // size to it. Does not take ownership of `source` — the caller
    // (SongsmithMainComponent) owns the SourceTrackNoteSource and must keep
    // it alive at least as long as it stays set here. ticksPerQuarter and
    // meterMapNode drive bar-boundary gridlines (first meter entry only, per
    // this project's one-meter-timeline convention elsewhere).
    void setNoteSource (PianoRollNoteSource* source, int ticksPerQuarter, juce::ValueTree meterMapNode);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    class Canvas : public juce::Component
    {
    public:
        explicit Canvas (PianoRollComponent& ownerIn) : owner (ownerIn) {}

        void paint (juce::Graphics& g) override { owner.paintCanvas (g, g.getClipBounds()); }
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    private:
        PianoRollComponent& owner;
    };

    void paintCanvas (juce::Graphics& g, juce::Rectangle<int> clip) const;
    void drawRowBands (juce::Graphics& g, juce::Rectangle<int> clip) const;
    void drawGridlines (juce::Graphics& g, juce::Rectangle<int> clip) const;
    void drawKeyboardGutter (juce::Graphics& g, juce::Rectangle<int> clip) const;
    void drawNotes (juce::Graphics& g, juce::Rectangle<int> clip) const;

    void rebuildContentSize();
    void zoom (float wheelDeltaY);

    Role role;
    PianoRollNoteSource* noteSource = nullptr;
    PianoRollGeometry geometry;
    int ticksPerQuarter = 480;
    juce::ValueTree meterMap;

    juce::Viewport viewport;
    Canvas canvas { *this };
};

} // namespace lotro

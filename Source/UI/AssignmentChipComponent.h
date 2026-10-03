#pragma once

#include "SongDocument.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Phase 4, component B3 — one assignment chip inside a PartSlotComponent's
// body: swatch, "Tk<n>" (n = the referenced track's 0-based SOURCE_MIDI
// child index; the conductor is 0, so chips start at Tk1), monospace transpose, and an unassign (x) button.
namespace lotro
{

class AssignmentChipComponent : public juce::Component
{
public:
    AssignmentChipComponent (SongDocument& document, juce::ValueTree partNode,
                              juce::ValueTree assignmentNode);

    void paint (juce::Graphics& g) override;
    void resized() override;

    // Width this chip would like to occupy, given its current text content —
    // used by the owning PartSlotComponent's wrapping layout.
    int getPreferredWidth() const;

    static constexpr int chipHeight = 18;

private:
    SongDocument&   doc;
    juce::ValueTree part;
    juce::ValueTree assignment;
    juce::TextButton removeButton { juce::String::fromUTF8 ("\xc3\x97") }; // "x" (multiplication sign)

    juce::String buildLabel() const;
};

} // namespace lotro

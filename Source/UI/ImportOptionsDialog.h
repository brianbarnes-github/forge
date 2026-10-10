#pragma once

#include "MidiImportPlan.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace lotro
{
    // The Import > MIDI choices: tempo map (keep / replace) and tracks (expand /
    // merge). OK delivers the options, Cancel only reports it; the window
    // closes itself either way (see showImportOptionsDialog).
    class ImportOptionsComponent : public juce::Component
    {
    public:
        // `songHasTempoMap` false (first import): the tempo choice is moot, so it is disabled.
        // `trackCount` is how many tracks Expand will add (previewImportTrackCount); 0 = unknown.
        // The Expand label shows it when it is 2 or more.
        ImportOptionsComponent (const juce::String& fileName, bool songHasTempoMap, int trackCount = 0);

        void paint (juce::Graphics& g) override;
        void resized() override;

        ImportOptions chosenOptions() const;

        std::function<void (const ImportOptions&)> onAccepted;
        std::function<void()>                      onCancelled;

        juce::ToggleButton& keepTempoForTesting()    { return keepTempo; }
        juce::ToggleButton& replaceTempoForTesting() { return replaceTempo; }
        juce::ToggleButton& expandTracksForTesting() { return expandTracks; }
        juce::ToggleButton& mergeTracksForTesting()  { return mergeTracks; }
        juce::TextButton&   okButtonForTesting()     { return okButton; }
        juce::TextButton&   cancelButtonForTesting() { return cancelButton; }

    private:
        juce::Label        heading, tempoLabel, tracksLabel;
        juce::ToggleButton keepTempo    { "Keep existing tempo map" };
        juce::ToggleButton replaceTempo { "Replace existing tempo map" };
        juce::ToggleButton expandTracks { "Expand into separate tracks" };
        juce::ToggleButton mergeTracks  { "Merge into one track" };
        juce::TextButton   okButton     { "OK" };
        juce::TextButton   cancelButton { "Cancel" };
    };

    // Opens the dialog fully modal. `onAccepted` runs only on OK; Cancel, Escape
    // and the title-bar X import nothing.
    void showImportOptionsDialog (juce::Component* centreAround, const juce::String& fileName,
                                  bool songHasTempoMap, int trackCount,
                                  std::function<void (const ImportOptions&)> onAccepted);
}

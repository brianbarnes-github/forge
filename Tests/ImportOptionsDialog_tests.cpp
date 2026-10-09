#include "UI/ImportOptionsDialog.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

TEST_CASE ("ImportOptionsDialog: defaults are keep the tempo map and expand the tracks", "[import-options-dialog]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    ImportOptionsComponent dialog ("song.mid", true);
    const auto options = dialog.chosenOptions();
    CHECK (options.tempo == TempoMode::keep);
    CHECK (options.tracks == TrackMode::expanded);
}

TEST_CASE ("ImportOptionsDialog: the radios choose replace and merge", "[import-options-dialog]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    ImportOptionsComponent dialog ("song.mid", true);
    dialog.replaceTempoForTesting().setToggleState (true, juce::sendNotification);
    dialog.mergeTracksForTesting().setToggleState (true, juce::sendNotification);
    CHECK_FALSE (dialog.keepTempoForTesting().getToggleState());   // one radio group
    CHECK_FALSE (dialog.expandTracksForTesting().getToggleState());
    const auto options = dialog.chosenOptions();
    CHECK (options.tempo == TempoMode::replace);
    CHECK (options.tracks == TrackMode::merged);
}

TEST_CASE ("ImportOptionsDialog: the tempo group is disabled when the Song has no tempo map yet", "[import-options-dialog]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    ImportOptionsComponent first ("song.mid", false);
    CHECK_FALSE (first.keepTempoForTesting().isEnabled());
    CHECK_FALSE (first.replaceTempoForTesting().isEnabled());
    CHECK (first.expandTracksForTesting().isEnabled());
    CHECK (first.mergeTracksForTesting().isEnabled());

    ImportOptionsComponent later ("song.mid", true);
    CHECK (later.keepTempoForTesting().isEnabled());
    CHECK (later.replaceTempoForTesting().isEnabled());
}

TEST_CASE ("ImportOptionsDialog: OK delivers the chosen options, Cancel delivers nothing", "[import-options-dialog]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    ImportOptionsComponent dialog ("song.mid", true);
    int accepted = 0, cancelled = 0;
    ImportOptions got;
    dialog.onAccepted  = [&] (const ImportOptions& o) { ++accepted; got = o; };
    dialog.onCancelled = [&] { ++cancelled; };
    dialog.mergeTracksForTesting().setToggleState (true, juce::sendNotification);

    dialog.cancelButtonForTesting().onClick();
    CHECK (accepted == 0);
    CHECK (cancelled == 1);

    dialog.okButtonForTesting().onClick();
    CHECK (accepted == 1);
    CHECK (got.tracks == TrackMode::merged);
    CHECK (got.tempo == TempoMode::keep);
}

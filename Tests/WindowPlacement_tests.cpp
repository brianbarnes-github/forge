// Verifies WindowPlacement: parsing a saved window state, deciding whether a
// saved position is still reachable on the current monitors (falling back to
// the centre of the primary monitor when it isn't), and the save/restore
// round trip through a real juce::PropertiesFile.

#include "UI/WindowPlacement.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace
{
    const juce::Rectangle<int> laptop    { 0, 0, 1920, 1040 };     // user area: taskbar excluded
    const juce::Rectangle<int> secondary { 1920, 0, 2560, 1400 };
    const juce::Rectangle<int> tertiary  { 4480, 0, 1920, 1040 };
}

TEST_CASE ("WindowPlacement: parse reads JUCE's window-state string, with or without full-screen and frame", "[window-placement]")
{
    SECTION ("plain bounds")
    {
        const auto state = WindowPlacement::parse ("10 20 800 600");
        REQUIRE (state.has_value());
        CHECK (state->bounds == juce::Rectangle<int> (10, 20, 800, 600));
        CHECK_FALSE (state->maximised);
    }

    SECTION ("maximised: the bounds are the restored (non-maximised) ones")
    {
        const auto state = WindowPlacement::parse ("fs 2000 100 1200 800");
        REQUIRE (state.has_value());
        CHECK (state->bounds == juce::Rectangle<int> (2000, 100, 1200, 800));
        CHECK (state->maximised);
    }

    SECTION ("trailing Linux frame sizes are ignored")
    {
        const auto state = WindowPlacement::parse ("10 20 800 600 frame 30 1 1 1");
        REQUIRE (state.has_value());
        CHECK (state->bounds == juce::Rectangle<int> (10, 20, 800, 600));
    }

    SECTION ("empty, truncated or zero-size input is rejected")
    {
        CHECK_FALSE (WindowPlacement::parse ("").has_value());
        CHECK_FALSE (WindowPlacement::parse ("10 20 800").has_value());
        CHECK_FALSE (WindowPlacement::parse ("fs").has_value());
        CHECK_FALSE (WindowPlacement::parse ("10 20 0 600").has_value());
    }
}

TEST_CASE ("WindowPlacement: a saved position on a still-connected monitor is restored exactly", "[window-placement]")
{
    const juce::Rectangle<int> onSecondary (2200, 150, 1200, 800);
    CHECK (WindowPlacement::resolve (onSecondary, { laptop, secondary }, laptop) == onSecondary);

    // Hanging partly off the edge is the user's choice -- the title bar is still grabbable.
    const juce::Rectangle<int> overhanging (1500, 300, 1000, 700);
    CHECK (WindowPlacement::resolve (overhanging, { laptop }, laptop) == overhanging);
}

TEST_CASE ("WindowPlacement: a position on a monitor that is gone is re-centred on the primary monitor", "[window-placement]")
{
    // Closed on the third monitor; reopened on the laptop alone.
    const juce::Rectangle<int> onTertiary (4700, 100, 1200, 800);
    const auto placed = WindowPlacement::resolve (onTertiary, { laptop }, laptop);

    CHECK (placed.getCentre() == laptop.getCentre());
    CHECK (placed.getWidth() == 1200);
    CHECK (placed.getHeight() == 800);
}

TEST_CASE ("WindowPlacement: a window whose title bar is out of reach counts as off-screen", "[window-placement]")
{
    // Only the bottom few pixels poke onto the laptop from above -- the
    // title bar can't be grabbed, so this must be re-centred.
    const juce::Rectangle<int> titleBarAbove (400, -780, 1000, 800);
    CHECK (WindowPlacement::resolve (titleBarAbove, { laptop }, laptop).getCentre() == laptop.getCentre());

    // Only a sliver of the title bar's left end reaches the right edge.
    const juce::Rectangle<int> sliver (1900, 200, 1000, 700);
    CHECK (WindowPlacement::resolve (sliver, { laptop }, laptop).getCentre() == laptop.getCentre());
}

TEST_CASE ("WindowPlacement: a re-centred window bigger than the primary monitor is shrunk to fit it", "[window-placement]")
{
    const juce::Rectangle<int> hugeOnSecondary (1920, 0, 2560, 1400);
    const auto placed = WindowPlacement::resolve (hugeOnSecondary, { laptop }, laptop);

    CHECK (laptop.contains (placed));
    CHECK (placed.getCentre() == laptop.getCentre());
}

TEST_CASE ("WindowPlacement: saving then restoring through a PropertiesFile round-trips the bounds and maximised flag", "[window-placement]")
{
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };

    {
        juce::PropertiesFile props (file, {});
        WindowPlacement::save (props, "mainWindow", "fs 2000 100 1200 800");
        props.saveIfNeeded();
    }

    juce::PropertiesFile reloaded (file, {});
    const auto state = WindowPlacement::load (reloaded, "mainWindow");
    REQUIRE (state.has_value());
    CHECK (state->bounds == juce::Rectangle<int> (2000, 100, 1200, 800));
    CHECK (state->maximised);

    CHECK_FALSE (WindowPlacement::load (reloaded, "someOtherWindow").has_value());
}

TEST_CASE ("WindowPlacement: restoreWindow applies a saved position against the real displays, re-centring an off-screen one", "[window-placement]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto file = juce::File::createTempFile (".settings");
    const juce::ScopeGuard cleanup { [&] { file.deleteFile(); } };
    juce::PropertiesFile props (file, {});

    juce::DocumentWindow window ("test", juce::Colours::black, juce::DocumentWindow::allButtons, false);
    window.setBounds (5, 5, 300, 200);

    SECTION ("nothing saved: the window is left alone")
    {
        CHECK_FALSE (WindowPlacement::restoreWindow (props, "mainWindow", window));
        CHECK (window.getBounds() == juce::Rectangle<int> (5, 5, 300, 200));
    }

    SECTION ("saved on a monitor that no longer exists: centred on the primary one")
    {
        WindowPlacement::save (props, "mainWindow", "100000 100000 400 300");
        REQUIRE (WindowPlacement::restoreWindow (props, "mainWindow", window));

        const auto primary = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
        CHECK (window.getBounds().getCentre() == primary.getCentre());
        CHECK (window.getBounds().getWidth() == 400);
    }
}

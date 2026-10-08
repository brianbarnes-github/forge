// Pins which key presses the main window treats as undo / redo.

#include "UI/HistoryKeys.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace
{
    const auto ctrl = juce::ModifierKeys::ctrlModifier;
    const auto ctrlShift = juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier;
}

TEST_CASE ("HistoryKeys: Ctrl+Z is undo, Ctrl+Y and Ctrl+Shift+Z are redo", "[history-keys]")
{
    CHECK (historyActionFor (juce::KeyPress ('z', ctrl, 0)) == HistoryAction::Undo);
    CHECK (historyActionFor (juce::KeyPress ('y', ctrl, 0)) == HistoryAction::Redo);
    CHECK (historyActionFor (juce::KeyPress ('z', ctrlShift, 0)) == HistoryAction::Redo);
}

TEST_CASE ("HistoryKeys: Cmd stands in for Ctrl", "[history-keys]")
{
    CHECK (historyActionFor (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)) == HistoryAction::Undo);
    CHECK (historyActionFor (juce::KeyPress ('y', juce::ModifierKeys::commandModifier, 0)) == HistoryAction::Redo);
}

TEST_CASE ("HistoryKeys: other keys are not history keys", "[history-keys]")
{
    CHECK_FALSE (historyActionFor (juce::KeyPress ('z')).has_value());                         // plain Z
    CHECK_FALSE (historyActionFor (juce::KeyPress ('y')).has_value());
    CHECK_FALSE (historyActionFor (juce::KeyPress ('a', ctrl, 0)).has_value());
    CHECK_FALSE (historyActionFor (juce::KeyPress ('y', ctrlShift, 0)).has_value());           // Ctrl+Shift+Y
    CHECK_FALSE (historyActionFor (juce::KeyPress (juce::KeyPress::spaceKey)).has_value());
}

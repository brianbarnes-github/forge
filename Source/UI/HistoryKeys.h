#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

// Which key presses mean undo / redo at the main-window level. A free function so
// the mapping is testable without a MainWindow.
namespace lotro
{

enum class HistoryAction { Undo, Redo };

// Ctrl/Cmd+Z undo; Ctrl/Cmd+Y and Ctrl/Cmd+Shift+Z redo. KeyPress equality includes
// modifiers, so plain Z / Y and extra modifiers never match.
inline std::optional<HistoryAction> historyActionFor (const juce::KeyPress& key)
{
    const auto cmd = juce::ModifierKeys::commandModifier;
    const auto ctrl = juce::ModifierKeys::ctrlModifier;
    const auto shift = juce::ModifierKeys::shiftModifier;
    for (const auto base : { ctrl, cmd })
    {
        if (key == juce::KeyPress ('z', base, 0))                return HistoryAction::Undo;
        if (key == juce::KeyPress ('y', base, 0)
            || key == juce::KeyPress ('z', base | shift, 0))     return HistoryAction::Redo;
    }
    return std::nullopt;
}

} // namespace lotro

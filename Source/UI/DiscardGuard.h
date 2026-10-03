#pragma once

#include <functional>

namespace lotro
{

enum class DiscardChoice { Save, DontSave, Cancel };

// Both hooks are asynchronous (native dialogs / file choosers); each must
// call its continuation exactly once. `save` reports success -- false for a
// failed write or a cancelled Save As chooser.
struct DiscardGuardHooks
{
    std::function<void (std::function<void (DiscardChoice)>)> prompt;
    std::function<void (std::function<void (bool)>)>          save;
};

// Runs `onProceed` once it is safe to discard the open Song: immediately if
// it is clean; otherwise after the user chooses Don't Save, or Save and the
// save succeeds. Cancel (or a failed/cancelled save) never proceeds.
// A callback chain, never a modal loop.
void confirmDiscardChanges (bool dirty, const DiscardGuardHooks& hooks, std::function<void()> onProceed);

} // namespace lotro

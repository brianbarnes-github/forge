#include "DiscardGuard.h"

namespace lotro
{

void confirmDiscardChanges (bool dirty, const DiscardGuardHooks& hooks, std::function<void()> onProceed)
{
    if (! dirty)
    {
        onProceed();
        return;
    }

    // Copy the hooks: the prompt answers later, after the caller's temporaries are gone.
    hooks.prompt ([hooks, onProceed] (DiscardChoice choice)
    {
        switch (choice)
        {
            case DiscardChoice::Cancel:   return;
            case DiscardChoice::DontSave: onProceed(); return;
            case DiscardChoice::Save:
                hooks.save ([onProceed] (bool saved) { if (saved) onProceed(); });
                return;
        }
    });
}

} // namespace lotro

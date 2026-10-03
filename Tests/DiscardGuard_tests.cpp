#include "UI/DiscardGuard.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace
{
    struct Fake
    {
        int prompts = 0, saves = 0, proceeds = 0;
        std::function<void (DiscardChoice)> pendingPrompt;
        std::function<void (bool)> pendingSave;

        DiscardGuardHooks hooks()
        {
            return {
                [this] (std::function<void (DiscardChoice)> done) { ++prompts; pendingPrompt = std::move (done); },
                [this] (std::function<void (bool)> done)          { ++saves;   pendingSave = std::move (done); }
            };
        }
    };
}

TEST_CASE ("DiscardGuard: a clean Song proceeds immediately without prompting", "[discardguard]")
{
    Fake f;
    confirmDiscardChanges (false, f.hooks(), [&] { ++f.proceeds; });
    CHECK (f.proceeds == 1);
    CHECK (f.prompts == 0);
}

TEST_CASE ("DiscardGuard: a dirty Song prompts and waits for the answer", "[discardguard]")
{
    Fake f;
    confirmDiscardChanges (true, f.hooks(), [&] { ++f.proceeds; });
    CHECK (f.prompts == 1);
    CHECK (f.proceeds == 0);          // nothing happens until the user answers
}

TEST_CASE ("DiscardGuard: Don't Save proceeds without saving", "[discardguard]")
{
    Fake f;
    confirmDiscardChanges (true, f.hooks(), [&] { ++f.proceeds; });
    f.pendingPrompt (DiscardChoice::DontSave);
    CHECK (f.proceeds == 1);
    CHECK (f.saves == 0);
}

TEST_CASE ("DiscardGuard: Cancel does nothing", "[discardguard]")
{
    Fake f;
    confirmDiscardChanges (true, f.hooks(), [&] { ++f.proceeds; });
    f.pendingPrompt (DiscardChoice::Cancel);
    CHECK (f.proceeds == 0);
    CHECK (f.saves == 0);
}

TEST_CASE ("DiscardGuard: Save proceeds only after the save reports success", "[discardguard]")
{
    Fake f;
    confirmDiscardChanges (true, f.hooks(), [&] { ++f.proceeds; });
    f.pendingPrompt (DiscardChoice::Save);
    CHECK (f.saves == 1);
    CHECK (f.proceeds == 0);          // the (possibly Save-As-chooser) save is still pending

    f.pendingSave (true);
    CHECK (f.proceeds == 1);
}

TEST_CASE ("DiscardGuard: a failed or cancelled save does not proceed", "[discardguard]")
{
    Fake f;
    confirmDiscardChanges (true, f.hooks(), [&] { ++f.proceeds; });
    f.pendingPrompt (DiscardChoice::Save);
    f.pendingSave (false);
    CHECK (f.proceeds == 0);
}

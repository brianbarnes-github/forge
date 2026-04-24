#pragma once

#include "Core/Config.h"
#include "Core/Song.h"

#include "PropertyPageHost.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace lotro
{

class InstrumentsTree : public juce::Component
{
public:
    InstrumentsTree (Config&     configRef,
                     const Song& rawRef,
                     std::function<void (PropertyPageHost::Kind, int, int)> onSelection,
                     std::function<void()> onMutation);

    // Rebuilds the tree from scratch to match the current Config + Song.
    void rebuild();

    void resized() override;

private:
    Config&                                                config;
    const Song&                                             raw;
    std::function<void (PropertyPageHost::Kind, int, int)>  notifySelection;
    std::function<void()>                                   notifyMutation;
    juce::TreeView                                          treeView;
};

} // namespace lotro

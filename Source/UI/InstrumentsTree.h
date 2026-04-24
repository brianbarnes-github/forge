#pragma once

#include "Core/Config.h"
#include "Core/Song.h"

#include "PropertyPageHost.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace lotro
{

class InstrumentsTree : public juce::Component
{
public:
    InstrumentsTree (Config&     configRef,
                     const Song& rawRef,
                     std::function<void (PropertyPageHost::Kind, int, int)> onSelection,
                     std::function<void()> onMutation);

    ~InstrumentsTree() override;

    // Rebuilds the tree from scratch to match the current Config + Song.
    // Select the Song node after a rebuild.
    void rebuild();

    void resized() override;

private:
    class SongItem;
    class InstrumentItem;
    class SourceItem;

    Config&                                                config;
    const Song&                                             raw;
    std::function<void (PropertyPageHost::Kind, int, int)>  notifySelection;
    std::function<void()>                                   notifyMutation;
    juce::TreeView                                          treeView;
    std::unique_ptr<SongItem>                               rootItem;
};

} // namespace lotro

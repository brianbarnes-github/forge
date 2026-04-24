#include "InstrumentsTree.h"

namespace lotro
{

InstrumentsTree::InstrumentsTree (Config& cfgRef, const Song& rawRef,
                                  std::function<void (PropertyPageHost::Kind, int, int)> onSel,
                                  std::function<void()> onMut)
    : config (cfgRef), raw (rawRef),
      notifySelection (std::move (onSel)),
      notifyMutation  (std::move (onMut))
{
    addAndMakeVisible (treeView);
}

void InstrumentsTree::rebuild() {}
void InstrumentsTree::resized() { treeView.setBounds (getLocalBounds()); }

} // namespace lotro

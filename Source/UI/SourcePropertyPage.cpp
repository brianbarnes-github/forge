#include "SourcePropertyPage.h"

namespace lotro
{

SourcePropertyPage::SourcePropertyPage (Config& cfgRef, const Song& rawRef,
                                        std::function<void()> onChange)
    : config (cfgRef), raw (rawRef), notifyChange (std::move (onChange))
{
}

void SourcePropertyPage::editSource (int iIdx, int sIdx)
{
    instrumentIndex = iIdx;
    sourceIndex     = sIdx;
    refresh();
}

void SourcePropertyPage::refresh() {}
void SourcePropertyPage::resized() {}

} // namespace lotro

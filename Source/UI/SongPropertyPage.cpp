#include "SongPropertyPage.h"

namespace lotro
{

SongPropertyPage::SongPropertyPage (Config& cfgRef, std::function<void()> onChange)
    : config (cfgRef), notifyChange (std::move (onChange))
{
}

void SongPropertyPage::refresh() {}
void SongPropertyPage::resized() {}

} // namespace lotro

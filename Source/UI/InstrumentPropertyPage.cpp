#include "InstrumentPropertyPage.h"

namespace lotro
{

InstrumentPropertyPage::InstrumentPropertyPage (Config& cfgRef, std::function<void()> onChange)
    : config (cfgRef), notifyChange (std::move (onChange))
{
}

void InstrumentPropertyPage::editInstrument (int instrumentIndex)
{
    currentIndex = instrumentIndex;
    refresh();
}

void InstrumentPropertyPage::refresh() {}
void InstrumentPropertyPage::resized() {}

} // namespace lotro

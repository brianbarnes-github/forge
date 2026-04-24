#include "PropertyPageHost.h"

namespace lotro
{

PropertyPageHost::PropertyPageHost (Config& cfgRef, const Song& rawRef,
                                    std::function<void()> onChange)
    : config (cfgRef), raw (rawRef),
      songPage       (std::make_unique<SongPropertyPage>       (config, onChange)),
      instrumentPage (std::make_unique<InstrumentPropertyPage> (config, onChange)),
      sourcePage     (std::make_unique<SourcePropertyPage>     (config, raw, onChange))
{
    addChildComponent (*songPage);
    addChildComponent (*instrumentPage);
    addChildComponent (*sourcePage);
}

void PropertyPageHost::showFor (Kind kind, int instrumentIndex, int sourceIndex)
{
    songPage->setVisible       (kind == Kind::Song);
    instrumentPage->setVisible (kind == Kind::Instrument);
    sourcePage->setVisible     (kind == Kind::Source);

    if (kind == Kind::Instrument)
        instrumentPage->editInstrument (instrumentIndex);
    if (kind == Kind::Source)
        sourcePage->editSource (instrumentIndex, sourceIndex);
    if (kind == Kind::Song)
        songPage->refresh();

    currentKind = kind;
    resized();
}

void PropertyPageHost::refresh()
{
    songPage->refresh();
    instrumentPage->refresh();
    sourcePage->refresh();
}

void PropertyPageHost::resized()
{
    const auto bounds = getLocalBounds();
    songPage->setBounds (bounds);
    instrumentPage->setBounds (bounds);
    sourcePage->setBounds (bounds);
}

} // namespace lotro

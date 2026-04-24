#pragma once

#include "Core/Config.h"
#include "Core/Song.h"

#include "SongPropertyPage.h"
#include "InstrumentPropertyPage.h"
#include "SourcePropertyPage.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace lotro
{

class PropertyPageHost : public juce::Component
{
public:
    enum class Kind { None, Song, Instrument, Source };

    PropertyPageHost (Config& configRef, const Song& rawRef,
                      std::function<void()> onChange);

    // Show the appropriate property page for the given selection. For
    // Instrument, pass (instrumentIndex, -1); for Source, pass
    // (instrumentIndex, sourceIndex).
    void showFor (Kind kind, int instrumentIndex = -1, int sourceIndex = -1);

    // Called after external mutation to the underlying Config or Song so
    // the visible page can re-read its source data.
    void refresh();

    void resized() override;

private:
    Config&                                 config;
    const Song&                             raw;
    std::unique_ptr<SongPropertyPage>       songPage;
    std::unique_ptr<InstrumentPropertyPage> instrumentPage;
    std::unique_ptr<SourcePropertyPage>     sourcePage;
    Kind                                    currentKind = Kind::None;
};

} // namespace lotro

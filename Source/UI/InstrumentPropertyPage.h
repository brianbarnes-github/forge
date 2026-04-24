#pragma once

#include "Core/Config.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace lotro
{

class InstrumentPropertyPage : public juce::Component
{
public:
    InstrumentPropertyPage (Config& configRef, std::function<void()> onChange);

    // Sets which instrument index the form edits (-1 for "no selection").
    void editInstrument (int instrumentIndex);
    void refresh();
    void resized() override;

private:
    Config&               config;
    std::function<void()> notifyChange;
    int                   currentIndex = -1;
};

} // namespace lotro

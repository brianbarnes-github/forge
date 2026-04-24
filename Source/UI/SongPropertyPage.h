#pragma once

#include "Core/Config.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace lotro
{

class SongPropertyPage : public juce::Component
{
public:
    SongPropertyPage (Config& configRef, std::function<void()> onChange);

    void refresh();
    void resized() override;

private:
    Config&               config;
    std::function<void()> notifyChange;
};

} // namespace lotro

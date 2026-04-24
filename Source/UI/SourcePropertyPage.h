#pragma once

#include "Core/Config.h"
#include "Core/Song.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace lotro
{

class SourcePropertyPage : public juce::Component
{
public:
    SourcePropertyPage (Config& configRef, const Song& rawRef,
                        std::function<void()> onChange);

    // Selects the source for editing by instrument and source indices
    // inside the Config. Pass (-1, -1) to clear.
    void editSource (int instrumentIndex, int sourceIndex);
    void refresh();
    void resized() override;

private:
    Config&               config;
    const Song&           raw;
    std::function<void()> notifyChange;
    int                   instrumentIndex = -1;
    int                   sourceIndex     = -1;
};

} // namespace lotro

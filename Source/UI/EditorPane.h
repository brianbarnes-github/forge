#pragma once

#include "Core/Config.h"
#include "Core/Song.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace lotro
{

class InstrumentsTree;
class PropertyPageHost;

class EditorPane : public juce::Component
{
public:
    EditorPane();
    ~EditorPane() override;

    void loadFromMidi (Song raw, Config cfg);

    const Config& getConfig()  const noexcept { return config; }
    const Song&   getRawSong() const noexcept { return raw; }

    std::function<void()> onConfigChanged;
    std::function<void()> onRunRequested;

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    Config                              config;
    Song                                raw;
    std::unique_ptr<InstrumentsTree>    tree;
    std::unique_ptr<PropertyPageHost>   host;
    juce::TextButton                    runButton { "Run Converter" };
};

} // namespace lotro

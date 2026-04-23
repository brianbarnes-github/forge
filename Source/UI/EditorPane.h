#pragma once

#include "Core/Config.h"
#include "Core/Song.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace lotro
{

class GlobalSettingsView;
class InstrumentsTable;
class InstrumentDetailForm;

class EditorPane : public juce::Component
{
public:
    EditorPane();
    ~EditorPane() override;

    // Replaces the in-memory state with a freshly imported MIDI and an
    // auto-synthesised Config. Triggers a refresh of all sub-views.
    void loadFromMidi (Song raw, Config cfg);

    // Read-only accessors for Run / Save flows.
    const Config& getConfig() const noexcept { return config; }
    const Song&   getRawSong() const noexcept { return raw; }

    // Notifies listeners (typically MainWindow) that the in-memory Config
    // has been mutated.
    std::function<void()> onConfigChanged;

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    Config                                config;
    Song                                  raw;
    std::unique_ptr<GlobalSettingsView>   globalView;
    std::unique_ptr<InstrumentsTable>     instrumentsTable;
    std::unique_ptr<InstrumentDetailForm> detailForm;
};

} // namespace lotro

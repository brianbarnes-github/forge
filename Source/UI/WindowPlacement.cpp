#include "WindowPlacement.h"

namespace lotro::WindowPlacement
{

namespace
{
    // DocumentWindow's default title-bar height; the strip that has to stay
    // reachable for the user to drag the window back.
    constexpr int titleBarHeight = 26;
    constexpr int minGrabbableWidth = 100;

    bool titleBarReachable (juce::Rectangle<int> window, const juce::Array<juce::Rectangle<int>>& displayUserAreas)
    {
        const auto titleBar = window.withHeight (juce::jmin (titleBarHeight, window.getHeight()));
        const int neededWidth = juce::jmin (minGrabbableWidth, window.getWidth());
        const int neededHeight = juce::jmax (1, titleBar.getHeight() / 2);

        for (auto& area : displayUserAreas)
        {
            const auto overlap = titleBar.getIntersection (area);
            if (overlap.getWidth() >= neededWidth && overlap.getHeight() >= neededHeight)
                return true;
        }
        return false;
    }
}

std::optional<SavedState> parse (const juce::String& windowState)
{
    juce::StringArray tokens;
    tokens.addTokens (windowState, false);
    tokens.removeEmptyStrings();

    const bool maximised = tokens.size() > 0 && tokens[0].equalsIgnoreCase ("fs");
    const int first = maximised ? 1 : 0;
    if (tokens.size() < first + 4)
        return std::nullopt;

    const juce::Rectangle<int> bounds (tokens[first].getIntValue(), tokens[first + 1].getIntValue(),
                                       tokens[first + 2].getIntValue(), tokens[first + 3].getIntValue());
    if (bounds.isEmpty())
        return std::nullopt;

    return SavedState { bounds, maximised };
}

juce::Rectangle<int> resolve (juce::Rectangle<int> saved,
                              const juce::Array<juce::Rectangle<int>>& displayUserAreas,
                              juce::Rectangle<int> primaryUserArea)
{
    if (titleBarReachable (saved, displayUserAreas))
        return saved;

    return juce::Rectangle<int> (juce::jmin (saved.getWidth(), primaryUserArea.getWidth()),
                                 juce::jmin (saved.getHeight(), primaryUserArea.getHeight()))
        .withCentre (primaryUserArea.getCentre());
}

void save (juce::PropertiesFile& settings, const juce::String& key, const juce::String& windowState)
{
    settings.setValue (key, windowState);
}

std::optional<SavedState> load (const juce::PropertiesFile& settings, const juce::String& key)
{
    return parse (settings.getValue (key));
}

void saveWindow (juce::PropertiesFile& settings, const juce::String& key, juce::ResizableWindow& window)
{
    save (settings, key, window.getWindowStateAsString());
}

bool restoreWindow (const juce::PropertiesFile& settings, const juce::String& key, juce::ResizableWindow& window)
{
    const auto state = load (settings, key);
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    const auto* primary = displays.getPrimaryDisplay();
    if (! state.has_value() || primary == nullptr)
        return false;

    juce::Array<juce::Rectangle<int>> userAreas;
    for (auto& display : displays.displays)
        userAreas.add (display.userArea);

    window.setBounds (resolve (state->bounds, userAreas, primary->userArea));
    return true;
}

void applyMaximised (const juce::PropertiesFile& settings, const juce::String& key, juce::ResizableWindow& window)
{
    const auto state = load (settings, key);
    if (state.has_value() && state->maximised)
        window.setFullScreen (true);
}

} // namespace lotro::WindowPlacement

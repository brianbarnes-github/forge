#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

// Remembering a top-level window's position across runs, and putting it
// somewhere reachable when the monitor it was on is no longer connected
// (e.g. closed on the third of three monitors, reopened on a laptop alone).
//
// The pure pieces (parse/resolve, and save/load against a PropertiesFile)
// take plain rectangles and strings so they're tested headlessly;
// saveWindow/restoreWindow are the thin glue that reads a real window and
// the real Desktop displays.
namespace lotro::WindowPlacement
{
    struct SavedState
    {
        juce::Rectangle<int> bounds; // the restored (non-maximised) bounds
        bool maximised = false;
    };

    // Reads ResizableWindow::getWindowStateAsString()'s format: an optional
    // leading "fs" (maximised), then "x y w h", then (Linux only) a
    // "frame t l b r" suffix, which is ignored. Empty, truncated or
    // zero-size input yields nullopt.
    std::optional<SavedState> parse (const juce::String& windowState);

    // Where to put a window saved at `saved`. Kept exactly as saved if its
    // title bar is still grabbable on one of `displayUserAreas` (hanging
    // partly off an edge is the user's choice); otherwise re-centred on
    // `primaryUserArea`, shrunk to fit it if necessary.
    juce::Rectangle<int> resolve (juce::Rectangle<int> saved,
                                  const juce::Array<juce::Rectangle<int>>& displayUserAreas,
                                  juce::Rectangle<int> primaryUserArea);

    void save (juce::PropertiesFile& settings, const juce::String& key, const juce::String& windowState);
    std::optional<SavedState> load (const juce::PropertiesFile& settings, const juce::String& key);

    void saveWindow (juce::PropertiesFile& settings, const juce::String& key, juce::ResizableWindow& window);

    // Applies a saved position (resolved against the current displays) to
    // `window`, returning false -- leaving the window untouched -- if
    // nothing usable was saved under `key`. Call before the window is made
    // visible; call applyMaximised afterwards, since maximising only takes
    // effect on a showing window.
    bool restoreWindow (const juce::PropertiesFile& settings, const juce::String& key, juce::ResizableWindow& window);
    void applyMaximised (const juce::PropertiesFile& settings, const juce::String& key, juce::ResizableWindow& window);
}

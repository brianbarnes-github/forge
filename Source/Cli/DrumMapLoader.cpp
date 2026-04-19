#include "DrumMapLoader.h"

#include <juce_core/juce_core.h>

namespace lotro
{

std::string loadDrumMapFromFile (const std::string& path, DrumMap& target)
{
    const juce::File file (path);
    if (! file.existsAsFile())
        return "Drum-map file not found: " + path;

    const auto text = file.loadFileAsString();
    if (text.isEmpty())
        return "Drum-map file is empty: " + path;

    const auto parsed = juce::JSON::parse (text);
    if (! parsed.isObject())
        return "Drum-map JSON must be an object at the top level: " + path;

    const auto* obj = parsed.getDynamicObject();
    if (obj == nullptr)
        return "Drum-map JSON: could not read object properties: " + path;

    // Build a fresh map of entries first so a partially-parsed error leaves
    // the caller's existing drum map untouched.
    DrumMap staged = target;

    for (const auto& entry : obj->getProperties())
    {
        const auto keyText = entry.name.toString().toStdString();
        int gmNote = 0;
        try
        {
            size_t consumed = 0;
            gmNote = std::stoi (keyText, &consumed);
            if (consumed != keyText.size())
                return "Drum-map key is not an integer: \"" + keyText + "\"";
        }
        catch (const std::exception&)
        {
            return "Drum-map key is not an integer: \"" + keyText + "\"";
        }

        if (gmNote < 0 || gmNote > 127)
            return "Drum-map GM pitch out of range [0..127]: " + std::to_string (gmNote);

        if (! entry.value.isString())
            return "Drum-map value for " + keyText + " is not a string";

        const auto abcToken = entry.value.toString().toStdString();
        if (abcToken.empty())
            return "Drum-map value for " + keyText + " is an empty string";

        staged.set (gmNote, abcToken);
    }

    target = std::move (staged);
    return {};
}

} // namespace lotro

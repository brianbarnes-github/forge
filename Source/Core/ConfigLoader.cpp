#include "ConfigLoader.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace lotro
{

namespace
{
    ConfigFormat detectFormat (const std::string& path)
    {
        const auto dot = path.find_last_of ('.');
        if (dot == std::string::npos) return ConfigFormat::Json;

        std::string ext = path.substr (dot + 1);
        std::transform (ext.begin(), ext.end(), ext.begin(),
                        [] (unsigned char c) { return (char) std::tolower (c); });

        if (ext == "json") return ConfigFormat::Json;
        if (ext == "toml") return ConfigFormat::Toml;
        if (ext == "xml")  return ConfigFormat::Xml;
        return ConfigFormat::Json;
    }

    std::string asString (const juce::var& v)
    {
        return v.toString().toStdString();
    }

    std::optional<std::string> optString (const juce::DynamicObject* obj, const char* key)
    {
        if (obj == nullptr) return std::nullopt;
        if (! obj->hasProperty (key)) return std::nullopt;
        const auto v = obj->getProperty (key);
        if (! v.isString()) return std::nullopt;
        return std::optional<std::string>{ asString (v) };
    }

    std::string loadJson (std::string_view text, Config& out)
    {
        const auto parsed = juce::JSON::parse (juce::String (text.data(), text.size()));
        if (! parsed.isObject())
            return "config JSON must be an object at the top level";

        const auto* top = parsed.getDynamicObject();
        if (top == nullptr)
            return "config JSON: cannot read top-level object";

        if (! top->hasProperty ("input") || ! top->getProperty ("input").isString())
            return "config JSON: 'input' field missing or not a string";

        out.input = asString (top->getProperty ("input"));

        out.output      = optString (top, "output");
        out.title       = optString (top, "title");
        out.transcriber = optString (top, "transcriber");

        if (top->hasProperty ("tempo"))
        {
            const auto v = top->getProperty ("tempo");
            if (! v.isDouble() && ! v.isInt())
                return "config JSON: 'tempo' must be a number";
            out.tempo = (double) v;
        }

        if (top->hasProperty ("transpose"))
        {
            const auto v = top->getProperty ("transpose");
            if (! v.isInt())
                return "config JSON: 'transpose' must be an integer";
            out.transpose = (int) v;
        }

        if (top->hasProperty ("instruments"))
        {
            const auto v = top->getProperty ("instruments");
            if (! v.isArray())
                return "config JSON: 'instruments' must be an array";

            const auto& arr = *v.getArray();
            for (int i = 0; i < arr.size(); ++i)
            {
                const auto elem = arr[i];
                if (! elem.isObject())
                    return "config JSON: instruments[" + std::to_string (i) + "] is not an object";

                const auto* o = elem.getDynamicObject();
                ConfigInstrument inst;

                if (o->hasProperty ("x"))
                {
                    const auto xv = o->getProperty ("x");
                    if (! xv.isInt())
                        return "config JSON: instruments[" + std::to_string (i) + "].x must be integer";
                    inst.x = (int) xv;
                }

                if (o->hasProperty ("name"))
                {
                    const auto nv = o->getProperty ("name");
                    if (! nv.isString())
                        return "config JSON: instruments[" + std::to_string (i) + "].name must be string";
                    inst.name = asString (nv);
                }

                inst.label = optString (o, "label");

                if (o->hasProperty ("sources"))
                {
                    const auto sv = o->getProperty ("sources");
                    if (! sv.isArray())
                        return "config JSON: instruments[" + std::to_string (i) + "].sources must be array";
                    const auto& sarr = *sv.getArray();
                    for (int j = 0; j < sarr.size(); ++j)
                    {
                        if (! sarr[j].isInt())
                            return "config JSON: instruments[" + std::to_string (i)
                                 + "].sources[" + std::to_string (j) + "] must be integer";
                        inst.sources.push_back ((int) sarr[j]);
                    }
                }

                if (o->hasProperty ("transposeSemitones"))
                {
                    const auto tv = o->getProperty ("transposeSemitones");
                    if (! tv.isInt())
                        return "config JSON: instruments[" + std::to_string (i)
                             + "].transposeSemitones must be integer";
                    inst.transposeSemitones = (int) tv;
                }

                if (o->hasProperty ("volumePercent"))
                {
                    const auto vv = o->getProperty ("volumePercent");
                    if (! vv.isInt())
                        return "config JSON: instruments[" + std::to_string (i)
                             + "].volumePercent must be integer";
                    inst.volumePercent = (int) vv;
                }

                inst.drumMap = optString (o, "drumMap");

                out.instruments.push_back (std::move (inst));
            }
        }

        return {};
    }
}

std::string loadConfigFromString (std::string_view text, ConfigFormat format, Config& out)
{
    out = Config{};

    if (format == ConfigFormat::Auto)
        format = ConfigFormat::Json;

    switch (format)
    {
        case ConfigFormat::Json: return loadJson (text, out);
        case ConfigFormat::Toml: return "TOML config not yet supported (Phase 2)";
        case ConfigFormat::Xml:  return "XML config not yet supported (Phase 2)";
        case ConfigFormat::Auto: return "internal error: Auto format not resolved";
    }
    return "internal error: unknown format";
}

std::string loadConfigFromFile (const std::string& path, ConfigFormat format, Config& out)
{
    if (format == ConfigFormat::Auto)
        format = detectFormat (path);

    std::ifstream in (path);
    if (! in)
        return "config file not found or unreadable: " + path;

    std::stringstream buffer;
    buffer << in.rdbuf();
    return loadConfigFromString (buffer.str(), format, out);
}

} // namespace lotro

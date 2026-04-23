#include "ConfigWriter.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cctype>
#include <fstream>

namespace lotro
{

namespace
{
    juce::var toJsonVar (const Config& cfg)
    {
        auto* top = new juce::DynamicObject();
        top->setProperty ("input", juce::String (cfg.input));
        if (cfg.output.has_value())
            top->setProperty ("output", juce::String (*cfg.output));
        if (cfg.title.has_value())
            top->setProperty ("title", juce::String (*cfg.title));
        if (cfg.transcriber.has_value())
            top->setProperty ("transcriber", juce::String (*cfg.transcriber));
        if (cfg.tempo.has_value())
            top->setProperty ("tempo", *cfg.tempo);
        if (cfg.transpose != 0)
            top->setProperty ("transpose", cfg.transpose);

        juce::Array<juce::var> instrumentsArr;
        for (const auto& inst : cfg.instruments)
        {
            auto* obj = new juce::DynamicObject();
            obj->setProperty ("x",    inst.x);
            obj->setProperty ("name", juce::String (inst.name));
            if (inst.label.has_value())
                obj->setProperty ("label", juce::String (*inst.label));

            juce::Array<juce::var> sourcesArr;
            for (int s : inst.sources)
                sourcesArr.add (juce::var (s));
            obj->setProperty ("sources", sourcesArr);

            if (inst.transposeSemitones != 0)
                obj->setProperty ("transposeSemitones", inst.transposeSemitones);
            if (inst.volumePercent != 100)
                obj->setProperty ("volumePercent", inst.volumePercent);
            if (inst.drumMap.has_value())
                obj->setProperty ("drumMap", juce::String (*inst.drumMap));

            instrumentsArr.add (juce::var (obj));
        }
        top->setProperty ("instruments", instrumentsArr);

        return juce::var (top);
    }

    std::string writeJson (const Config& cfg, std::string& out)
    {
        const auto v = toJsonVar (cfg);
        out = juce::JSON::toString (v, /*allOnOneLine=*/false).toStdString();
        return {};
    }

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
}

std::string writeConfigToString (ConfigFormat format, const Config& cfg, std::string& out)
{
    if (format == ConfigFormat::Auto)
        format = ConfigFormat::Json;

    switch (format)
    {
        case ConfigFormat::Json: return writeJson (cfg, out);
        case ConfigFormat::Toml: return "TOML writer not yet implemented";
        case ConfigFormat::Xml:  return "XML writer not yet implemented";
        case ConfigFormat::Auto: return "internal error: Auto not resolved";
    }
    return "internal error: unknown format";
}

std::string writeConfigToFile (const std::string& path, ConfigFormat format, const Config& cfg)
{
    if (format == ConfigFormat::Auto)
        format = detectFormat (path);

    std::string text;
    const auto err = writeConfigToString (format, cfg, text);
    if (! err.empty()) return err;

    std::ofstream out (path);
    if (! out) return "config file not writable: " + path;
    out << text;
    return {};
}

} // namespace lotro

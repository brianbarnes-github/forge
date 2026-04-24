#include "ConfigWriter.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>

namespace lotro
{

namespace
{
    juce::var toJsonVar (const Config& cfg)
    {
        auto* top = new juce::DynamicObject();
        top->setProperty ("input", juce::String (cfg.input));
        if (cfg.output.has_value())       top->setProperty ("output",      juce::String (*cfg.output));
        if (cfg.title.has_value())        top->setProperty ("title",       juce::String (*cfg.title));
        if (cfg.transcriber.has_value())  top->setProperty ("transcriber", juce::String (*cfg.transcriber));
        if (cfg.tempo.has_value())        top->setProperty ("tempo",       *cfg.tempo);
        if (cfg.transpose != 0)           top->setProperty ("transpose",   cfg.transpose);

        juce::Array<juce::var> instrumentsArr;
        for (const auto& inst : cfg.instruments)
        {
            auto* obj = new juce::DynamicObject();
            obj->setProperty ("x",    inst.x);
            obj->setProperty ("name", juce::String (inst.name));
            if (inst.label.has_value())
                obj->setProperty ("label", juce::String (*inst.label));

            juce::Array<juce::var> sourcesArr;
            for (const auto& src : inst.sources)
            {
                auto* sobj = new juce::DynamicObject();
                sobj->setProperty ("midiTrack", src.midiTrackIndex);
                if (src.transposeSemitones != 0)
                    sobj->setProperty ("transposeSemitones", src.transposeSemitones);
                if (src.volumePercent != 0)
                    sobj->setProperty ("volumePercent", src.volumePercent);
                sourcesArr.add (juce::var (sobj));
            }
            obj->setProperty ("sources", sourcesArr);

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
        out = juce::JSON::toString (v, false).toStdString();
        return {};
    }

    std::string escapeToml (const std::string& s)
    {
        std::string out = "\"";
        for (char c : s)
        {
            if      (c == '\\') out += "\\\\";
            else if (c == '"')  out += "\\\"";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else if (c == '\t') out += "\\t";
            else                out += c;
        }
        out += "\"";
        return out;
    }

    std::string writeToml (const Config& cfg, std::string& out)
    {
        std::string s;
        s += "input = " + escapeToml (cfg.input) + "\n";
        if (cfg.output.has_value())       s += "output = "      + escapeToml (*cfg.output)      + "\n";
        if (cfg.title.has_value())        s += "title = "       + escapeToml (*cfg.title)       + "\n";
        if (cfg.transcriber.has_value())  s += "transcriber = " + escapeToml (*cfg.transcriber) + "\n";
        if (cfg.tempo.has_value())        s += "tempo = "       + std::to_string ((int) std::lround (*cfg.tempo)) + "\n";
        if (cfg.transpose != 0)           s += "transpose = "   + std::to_string (cfg.transpose) + "\n";

        for (const auto& inst : cfg.instruments)
        {
            s += "\n[[instruments]]\n";
            s += "x = "    + std::to_string (inst.x) + "\n";
            s += "name = " + escapeToml (inst.name) + "\n";
            if (inst.label.has_value())
                s += "label = " + escapeToml (*inst.label) + "\n";
            if (inst.drumMap.has_value())
                s += "drumMap = " + escapeToml (*inst.drumMap) + "\n";

            for (const auto& src : inst.sources)
            {
                s += "\n[[instruments.sources]]\n";
                s += "midiTrack = " + std::to_string (src.midiTrackIndex) + "\n";
                if (src.transposeSemitones != 0)
                    s += "transposeSemitones = " + std::to_string (src.transposeSemitones) + "\n";
                if (src.volumePercent != 0)
                    s += "volumePercent = " + std::to_string (src.volumePercent) + "\n";
            }
        }

        out = std::move (s);
        return {};
    }

    std::string writeXml (const Config& cfg, std::string& out)
    {
        juce::XmlElement top ("config");
        top.createNewChildElement ("input")->addTextElement (juce::String (cfg.input));

        auto addOptional = [&] (const char* tag, const std::optional<std::string>& v)
        {
            if (v.has_value())
                top.createNewChildElement (tag)->addTextElement (juce::String (*v));
        };
        addOptional ("output",      cfg.output);
        addOptional ("title",       cfg.title);
        addOptional ("transcriber", cfg.transcriber);

        if (cfg.tempo.has_value())
            top.createNewChildElement ("tempo")->addTextElement (juce::String ((int) std::lround (*cfg.tempo)));
        if (cfg.transpose != 0)
            top.createNewChildElement ("transpose")->addTextElement (juce::String (cfg.transpose));

        auto* instrumentsElem = top.createNewChildElement ("instruments");
        for (const auto& inst : cfg.instruments)
        {
            auto* iElem = instrumentsElem->createNewChildElement ("instrument");
            iElem->setAttribute ("x", inst.x);
            iElem->setAttribute ("name", juce::String (inst.name));
            if (inst.label.has_value())
                iElem->setAttribute ("label", juce::String (*inst.label));

            auto* sources = iElem->createNewChildElement ("sources");
            for (const auto& src : inst.sources)
            {
                auto* sElem = sources->createNewChildElement ("source");
                sElem->setAttribute ("midiTrack", src.midiTrackIndex);
                if (src.transposeSemitones != 0)
                    sElem->setAttribute ("transposeSemitones", src.transposeSemitones);
                if (src.volumePercent != 0)
                    sElem->setAttribute ("volumePercent", src.volumePercent);
            }

            if (inst.drumMap.has_value())
                iElem->createNewChildElement ("drumMap")->addTextElement (juce::String (*inst.drumMap));
        }

        out = top.toString().toStdString();
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
        case ConfigFormat::Toml: return writeToml (cfg, out);
        case ConfigFormat::Xml:  return writeXml  (cfg, out);
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

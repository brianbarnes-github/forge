#include "ConfigLoader.h"

#include <juce_core/juce_core.h>
#include <toml.hpp>

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

    void emitRemovedFieldWarning (Diagnostics& diag, size_t instrumentIdx,
                                  const char* fieldName)
    {
        Diagnostic d;
        d.severity   = Severity::Warning;
        d.source     = "ConfigLoader";
        d.message    = "ignoring removed field '" + std::string (fieldName)
                     + "' on instruments[" + std::to_string (instrumentIdx)
                     + "]; this field is no longer part of the schema";
        d.trackIndex = -1;
        diag.push_back (std::move (d));
    }

    std::string asString (const juce::var& v) { return v.toString().toStdString(); }

    std::optional<std::string> optString (const juce::DynamicObject* obj, const char* key)
    {
        if (obj == nullptr) return std::nullopt;
        if (! obj->hasProperty (key)) return std::nullopt;
        const auto v = obj->getProperty (key);
        if (! v.isString()) return std::nullopt;
        return std::optional<std::string>{ asString (v) };
    }

    // Parses a single JSON source entry, which can be either a bare integer
    // (shorthand for {midiTrack: N}) or an object with midiTrack +
    // per-source adjustment fields.
    std::string parseJsonSource (const juce::var& elem, size_t instrumentIdx,
                                 size_t sourceIdx, ConfigSource& out)
    {
        if (elem.isInt())
        {
            out.midiTrackIndex = (int) elem;
            return {};
        }
        if (! elem.isObject())
            return "config JSON: instruments[" + std::to_string (instrumentIdx)
                 + "].sources[" + std::to_string (sourceIdx)
                 + "] must be an integer or an object";

        const auto* o = elem.getDynamicObject();
        if (o == nullptr)
            return "config JSON: instruments[" + std::to_string (instrumentIdx)
                 + "].sources[" + std::to_string (sourceIdx) + "] unreadable";

        if (o->hasProperty ("midiTrack"))
        {
            const auto v = o->getProperty ("midiTrack");
            if (! v.isInt())
                return "config JSON: instruments[" + std::to_string (instrumentIdx)
                     + "].sources[" + std::to_string (sourceIdx)
                     + "].midiTrack must be an integer";
            out.midiTrackIndex = (int) v;
        }
        if (o->hasProperty ("transposeSemitones"))
        {
            const auto v = o->getProperty ("transposeSemitones");
            if (! v.isInt())
                return "config JSON: instruments[" + std::to_string (instrumentIdx)
                     + "].sources[" + std::to_string (sourceIdx)
                     + "].transposeSemitones must be an integer";
            out.transposeSemitones = (int) v;
        }
        if (o->hasProperty ("volumePercent"))
        {
            const auto v = o->getProperty ("volumePercent");
            if (! v.isInt())
                return "config JSON: instruments[" + std::to_string (instrumentIdx)
                     + "].sources[" + std::to_string (sourceIdx)
                     + "].volumePercent must be an integer";
            out.volumePercent = (int) v;
        }
        if (out.midiTrackIndex < 0)
            return "config JSON: instruments[" + std::to_string (instrumentIdx)
                 + "].sources[" + std::to_string (sourceIdx)
                 + "] object must include 'midiTrack'";
        return {};
    }

    std::string loadJson (std::string_view text, Config& out, Diagnostics& mig)
    {
        const auto parsed = juce::JSON::parse (juce::String (text.data(), text.size()));
        if (! parsed.isObject())
            return "config JSON must be an object at the top level";

        const auto* top = parsed.getDynamicObject();
        if (top == nullptr)
            return "config JSON: cannot read top-level object";

        if (! top->hasProperty ("input") || ! top->getProperty ("input").isString())
            return "config JSON: 'input' field missing or not a string";

        out.input       = asString (top->getProperty ("input"));
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
                        ConfigSource src;
                        const auto err = parseJsonSource (sarr[j], (size_t) i, (size_t) j, src);
                        if (! err.empty()) return err;
                        inst.sources.push_back (src);
                    }
                }

                // Removed schema fields — permissive drop with a warning.
                if (o->hasProperty ("transposeSemitones"))
                    emitRemovedFieldWarning (mig, (size_t) i, "transposeSemitones");
                if (o->hasProperty ("volumePercent"))
                    emitRemovedFieldWarning (mig, (size_t) i, "volumePercent");

                inst.drumMap = optString (o, "drumMap");

                out.instruments.push_back (std::move (inst));
            }
        }

        return {};
    }

    std::optional<std::string> childText (const juce::XmlElement& parent, const char* tag)
    {
        if (auto* c = parent.getChildByName (tag))
        {
            const auto text = c->getAllSubText().toStdString();
            if (! text.empty()) return text;
        }
        return std::nullopt;
    }

    // Heuristic: reject trivially truncated XML before JUCE's permissive parser
    // silently accepts it. Checks that the last '<' is followed by a '>' on
    // the same or later input. Doesn't attempt full well-formedness.
    bool looksWellFormed (std::string_view text)
    {
        const auto lt = text.rfind ('<');
        if (lt == std::string::npos) return false;
        return text.find ('>', lt) != std::string::npos;
    }

    std::string loadXml (std::string_view text, Config& out, Diagnostics& mig)
    {
        if (! looksWellFormed (text))
            return "config XML: truncated or malformed input";

        auto xml = juce::XmlDocument::parse (juce::String (text.data(), text.size()));
        if (xml == nullptr)
            return "config XML: failed to parse";
        if (xml->getTagName() != "config")
            return "config XML: root element must be <config>";

        if (auto v = childText (*xml, "input"))
            out.input = *v;
        else
            return "config XML: <input> is required";

        out.output      = childText (*xml, "output");
        out.title       = childText (*xml, "title");
        out.transcriber = childText (*xml, "transcriber");

        if (auto t = childText (*xml, "tempo"))
        {
            try { out.tempo = std::stod (*t); }
            catch (const std::exception&)
            {
                return "config XML: <tempo> is not a number (got '" + *t + "')";
            }
        }
        if (auto t = childText (*xml, "transpose"))
        {
            try { out.transpose = std::stoi (*t); }
            catch (const std::exception&)
            {
                return "config XML: <transpose> is not an integer (got '" + *t + "')";
            }
        }

        auto* instrumentsElem = xml->getChildByName ("instruments");
        if (instrumentsElem == nullptr) return {};

        size_t instIdx = 0;
        for (auto* inst = instrumentsElem->getFirstChildElement(); inst != nullptr;
             inst = inst->getNextElement())
        {
            if (inst->getTagName() != "instrument")
                continue;

            ConfigInstrument ci;
            ci.x    = inst->getIntAttribute ("x", 0);
            ci.name = inst->getStringAttribute ("name").toStdString();
            if (inst->hasAttribute ("label"))
                ci.label = inst->getStringAttribute ("label").toStdString();

            if (auto* srcElem = inst->getChildByName ("sources"))
            {
                size_t srcIdx = 0;
                for (auto* s = srcElem->getFirstChildElement(); s != nullptr;
                     s = s->getNextElement())
                {
                    if (s->getTagName() != "source") continue;

                    ConfigSource cs;

                    // Accept either <source>N</source> shorthand OR
                    // <source midiTrack="N" ... /> object form.
                    const auto textContent = s->getAllSubText().trim().toStdString();
                    if (s->hasAttribute ("midiTrack"))
                        cs.midiTrackIndex = s->getIntAttribute ("midiTrack");
                    else if (! textContent.empty())
                    {
                        try { cs.midiTrackIndex = std::stoi (textContent); }
                        catch (const std::exception&)
                        {
                            return "config XML: instruments[" + std::to_string (instIdx)
                                 + "].sources[" + std::to_string (srcIdx)
                                 + "] text content is not an integer (got '" + textContent + "')";
                        }
                    }
                    else
                    {
                        return "config XML: instruments[" + std::to_string (instIdx)
                             + "].sources[" + std::to_string (srcIdx)
                             + "] must have 'midiTrack' attribute or integer text content";
                    }
                    if (s->hasAttribute ("transposeSemitones"))
                        cs.transposeSemitones = s->getIntAttribute ("transposeSemitones");
                    if (s->hasAttribute ("volumePercent"))
                        cs.volumePercent = s->getIntAttribute ("volumePercent");

                    ci.sources.push_back (cs);
                    ++srcIdx;
                }
            }

            // Removed instrument-level fields: drop with warning.
            if (inst->getChildByName ("transposeSemitones") != nullptr)
                emitRemovedFieldWarning (mig, instIdx, "transposeSemitones");
            if (inst->getChildByName ("volumePercent") != nullptr)
                emitRemovedFieldWarning (mig, instIdx, "volumePercent");

            ci.drumMap = childText (*inst, "drumMap");

            out.instruments.push_back (std::move (ci));
            ++instIdx;
        }

        return {};
    }

    std::string loadToml (std::string_view text, Config& out, Diagnostics& mig)
    {
        toml::table tbl;
        try { tbl = toml::parse (text); }
        catch (const toml::parse_error& e)
        {
            return std::string ("config TOML: ") + e.what();
        }

        if (auto v = tbl["input"].value<std::string>())
            out.input = *v;
        else
            return "config TOML: 'input' is required";

        if (auto v = tbl["output"].value<std::string>())      out.output      = *v;
        if (auto v = tbl["title"].value<std::string>())       out.title       = *v;
        if (auto v = tbl["transcriber"].value<std::string>()) out.transcriber = *v;
        if (auto v = tbl["tempo"].value<double>())            out.tempo       = *v;
        if (auto v = tbl["transpose"].value<int64_t>())       out.transpose   = (int) *v;

        if (auto* arr = tbl["instruments"].as_array())
        {
            size_t instIdx = 0;
            for (auto& el : *arr)
            {
                const auto* instTbl = el.as_table();
                if (instTbl == nullptr) { ++instIdx; continue; }

                ConfigInstrument inst;
                if (auto v = (*instTbl)["x"].value<int64_t>())          inst.x    = (int) *v;
                if (auto v = (*instTbl)["name"].value<std::string>())   inst.name = *v;
                if (auto v = (*instTbl)["label"].value<std::string>())  inst.label = *v;
                if (auto v = (*instTbl)["drumMap"].value<std::string>()) inst.drumMap = *v;

                if (auto* src = (*instTbl)["sources"].as_array())
                {
                    size_t srcIdx = 0;
                    for (auto& s : *src)
                    {
                        ConfigSource cs;
                        if (auto i = s.value<int64_t>())
                        {
                            cs.midiTrackIndex = (int) *i;
                        }
                        else if (const auto* srcTbl = s.as_table())
                        {
                            if (auto v = (*srcTbl)["midiTrack"].value<int64_t>())
                                cs.midiTrackIndex = (int) *v;
                            if (auto v = (*srcTbl)["transposeSemitones"].value<int64_t>())
                                cs.transposeSemitones = (int) *v;
                            if (auto v = (*srcTbl)["volumePercent"].value<int64_t>())
                                cs.volumePercent = (int) *v;
                            if (cs.midiTrackIndex < 0)
                                return "config TOML: instruments[" + std::to_string (instIdx)
                                     + "].sources[" + std::to_string (srcIdx)
                                     + "] table must include 'midiTrack'";
                        }
                        else
                        {
                            return "config TOML: instruments[" + std::to_string (instIdx)
                                 + "].sources[" + std::to_string (srcIdx)
                                 + "] must be an integer or a table";
                        }
                        inst.sources.push_back (cs);
                        ++srcIdx;
                    }
                }

                if ((*instTbl)["transposeSemitones"])
                    emitRemovedFieldWarning (mig, instIdx, "transposeSemitones");
                if ((*instTbl)["volumePercent"])
                    emitRemovedFieldWarning (mig, instIdx, "volumePercent");

                out.instruments.push_back (std::move (inst));
                ++instIdx;
            }
        }

        return {};
    }
}

std::string loadConfigFromString (std::string_view text, ConfigFormat format,
                                  Config& out, Diagnostics& mig)
{
    out = Config{};

    if (format == ConfigFormat::Auto) format = ConfigFormat::Json;

    switch (format)
    {
        case ConfigFormat::Json: return loadJson (text, out, mig);
        case ConfigFormat::Toml: return loadToml (text, out, mig);
        case ConfigFormat::Xml:  return loadXml  (text, out, mig);
        case ConfigFormat::Auto: return "internal error: Auto format not resolved";
    }
    return "internal error: unknown format";
}

std::string loadConfigFromFile (const std::string& path, ConfigFormat format,
                                Config& out, Diagnostics& mig)
{
    if (format == ConfigFormat::Auto) format = detectFormat (path);

    std::ifstream in (path);
    if (! in) return "config file not found or unreadable: " + path;

    std::stringstream buffer;
    buffer << in.rdbuf();
    return loadConfigFromString (buffer.str(), format, out, mig);
}

} // namespace lotro

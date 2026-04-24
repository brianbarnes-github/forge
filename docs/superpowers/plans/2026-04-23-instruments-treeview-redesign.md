# Instruments Treeview UI redesign implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the Editor pane's three-region layout (Global Settings + Instruments Table + Instrument Detail Form) with a single three-level tree (Song → Instruments → Sources) plus swappable property pages. Extend the data model so per-source `transposeSemitones` and `volumePercent` live on a new `ConfigSource` struct, removing those two fields from `ConfigInstrument`.

**Architecture:** Single phase, single branch. Task 1 does an atomic Core data-model migration (touches ~15 files, updates every test fixture). Tasks 2–3 scaffold the new UI classes and rewire `EditorPane` before deleting the old ones (keeps the build green throughout). Tasks 4–9 flesh out each new UI class. Tasks 10–11 sync documentation. No new automated GUI tests — this codebase's testing cohort audience (per memory) is manual smoke tests for the UI; data-model changes are fully unit-tested.

**Tech Stack:** C++17, JUCE GUI (`juce_gui_basics` for `TreeView`/`TreeViewItem`/`PopupMenu`), CMake + Ninja, Catch2 for data-model tests.

**Spec reference:** `docs/superpowers/specs/2026-04-23-instruments-treeview-redesign-design.md`

## Workspace setup

If you are executing this plan via subagent-driven-development, set up an isolated worktree first (per `superpowers:using-git-worktrees`):

```bash
cd /home/brian/sandbox/converter-cli
git worktree add .worktrees/instruments-treeview -b feat/instruments-treeview
cd .worktrees/instruments-treeview
git submodule update --init --recursive
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build
```

Expected baseline: all current tests pass (should be `130/130` or similar on current main).

All paths in this plan are relative to the worktree root.

## File plan

### New files (Phase C)

| Path | Responsibility |
|---|---|
| `Source/UI/InstrumentsTree.h` / `.cpp` | `juce::TreeView` host with three `TreeViewItem` subclasses: `SongItem`, `InstrumentItem`, `SourceItem`. Builds from `Config`+`Song`, dispatches selection changes, handles right-click context menus, rebuilds on mutation. |
| `Source/UI/SongPropertyPage.h` / `.cpp` | Form for the Song node (title, transcriber, tempo, transpose; input/output read-only). Absorbs today's `GlobalSettingsView`. |
| `Source/UI/InstrumentPropertyPage.h` / `.cpp` | Form for an Instrument node (x, name dropdown, label, drumMap — drumMap only enabled when name == "Drums"). |
| `Source/UI/SourcePropertyPage.h` / `.cpp` | Form for a Source node (read-only MIDI info; editable transposeSemitones, volumePercent). |
| `Source/UI/PropertyPageHost.h` / `.cpp` | Container for all three pages; shows one at a time driven by `showFor()` calls. |

### Deleted files (Phase B, end)

| Path | Reason |
|---|---|
| `Source/UI/GlobalSettingsView.h` / `.cpp` | Replaced by `SongPropertyPage`. |
| `Source/UI/InstrumentsTable.h` / `.cpp` | Replaced by `InstrumentsTree`. |
| `Source/UI/InstrumentDetailForm.h` / `.cpp` | Replaced by `InstrumentPropertyPage` + `SourcePropertyPage`. |

### Modified files

| Path | Change |
|---|---|
| `Source/Core/Config.h` | Add `struct ConfigSource`; change `ConfigInstrument::sources` type; remove `transposeSemitones` + `volumePercent` from `ConfigInstrument`. |
| `Source/Core/Config.cpp` | Validator: new rules for `ConfigSource.midiTrackIndex` (required, in-range, unique within instrument) and `ConfigSource.volumePercent > -100`. Remove instrument-level volumePercent rule. |
| `Source/Core/ConfigLoader.cpp` | Each loader (JSON/TOML/XML): parse the new object-shape sources plus bare-integer shorthand; drop removed instrument fields with a Warning `Diagnostic` through an out-parameter or a side-channel. |
| `Source/Core/ConfigLoader.h` | Signatures extended with a `Diagnostics&` out-parameter so migration warnings can be surfaced. |
| `Source/Core/ConfigWriter.cpp` | Each writer: emit the new object-shape sources, always; omit per-source fields at default (0). Skip the removed instrument fields entirely. |
| `Source/Core/InstrumentAssembly.cpp` | Assembly math reads `source.transposeSemitones` and `source.volumePercent` per-source. `totalTranspose = src.transposeSemitones + config.transpose`. Volume clamp Diagnostic message includes the source MIDI index. |
| `Source/Core/Pipeline.cpp` | `synthesiseConfig` emits `ConfigSource` objects (default transposeSemitones=0, volumePercent=0). |
| `Source/UI/EditorPane.h` / `.cpp` | Replace the three old sub-views with `InstrumentsTree` + `PropertyPageHost`. Tree on top, host in middle, Run button at bottom. Selection change callback drives `PropertyPageHost::showFor()`. |
| `Source/UI/MainWindow.cpp` | On Open MIDI / Open Config: call `EditorPane::loadFromMidi` as before — the tree rebuilds itself from the updated `raw` and `config`. No logic change. |
| `Source/Main.cpp` | `synthesiseConfig` call site — signature unchanged, but the returned Config now carries `ConfigSource` objects; Main.cpp doesn't need to care. Verify compile. |
| `CMakeLists.txt` | `target_sources(converter_ui …)`: replace three old UI filenames with five new ones. |
| `Tests/Config_tests.cpp` | Update `minimalValid()` helper; replace deprecated instrument-field tests with source-field rules. |
| `Tests/ConfigLoader_tests.cpp` / `ConfigLoaderXml_tests.cpp` / `ConfigLoaderToml_tests.cpp` | Round-trip tests updated to new shape; add one shorthand-form test per format. |
| `Tests/ConfigWriter_tests.cpp` | `richConfig()` helper updated; `checkConfigsEqual` updated to compare the new source shape. |
| `Tests/InstrumentAssembly_tests.cpp` | `inst.sources = { i }` becomes `inst.sources = { { i } }`; volume/transpose tests moved to per-source fields. |
| `Tests/ConfigPipeline_tests.cpp` | Same in-memory-construction updates. |
| `Tests/EndToEndConfig_tests.cpp` | Same in-memory-construction updates. |
| `Tests/Pipeline_tests.cpp` | `synthesiseConfig` produces `ConfigSource` objects; test assertions updated. |
| `docs/UI_GUIDE.md` | Rewrite the Editor pane section for the new tree + property pages. |
| `CLAUDE.md` | Source layout: UI/ tree updated with new class list; test count bumped. |

---

## Phase A — Core data-model migration

### Task 1: Migrate Config model to `ConfigSource` (single atomic refactor)

This is a large task because the type change `vector<int> → vector<ConfigSource>` cascades through every consumer and every test fixture in one compile unit. Subsequent tasks are small; this one carries the model change.

**Files (touched in one commit):**
- Modify: `Source/Core/Config.h`
- Modify: `Source/Core/Config.cpp`
- Modify: `Source/Core/ConfigLoader.h`
- Modify: `Source/Core/ConfigLoader.cpp`
- Modify: `Source/Core/ConfigWriter.cpp`
- Modify: `Source/Core/InstrumentAssembly.cpp`
- Modify: `Source/Core/Pipeline.cpp`
- Modify: `Source/Main.cpp` (call-site adjustment only, if any)
- Modify: `Source/UI/InstrumentsTable.cpp`, `Source/UI/InstrumentDetailForm.cpp` (temporary: use `.midiTrackIndex`) — these files get deleted in Phase B but must compile until then
- Modify: `Tests/Config_tests.cpp`, `Tests/ConfigLoader_tests.cpp`, `Tests/ConfigLoaderXml_tests.cpp`, `Tests/ConfigLoaderToml_tests.cpp`, `Tests/ConfigWriter_tests.cpp`, `Tests/InstrumentAssembly_tests.cpp`, `Tests/ConfigPipeline_tests.cpp`, `Tests/EndToEndConfig_tests.cpp`, `Tests/Pipeline_tests.cpp`

### Step 1: Update `Source/Core/Config.h`

Replace the entire namespace body (the header is small — full rewrite is clearer than an in-place patch):

```cpp
#pragma once

#include "LotroInstrument.h"

#include <optional>
#include <string>
#include <vector>

namespace lotro
{

// A single MIDI track feeding an instrument, with its own per-source
// transpose and volume adjustment. Multiple sources can feed one instrument
// (the assembler merges their notes); the same midiTrackIndex can legally
// appear in multiple instruments' sources.
struct ConfigSource
{
    int midiTrackIndex      = -1;   // required; 0-based index into raw Song.tracks
    int transposeSemitones  = 0;    // additive with Config.transpose
    int volumePercent       = 0;    // 0 = no change, +N louder, -N quieter; > -100
};

struct ConfigInstrument
{
    int                                   x = 0;
    std::string                           name;                    // LOTRO enum identifier, e.g. "LuteOfAges"
    std::optional<std::string>            label;                   // T: header suffix; fallback in assembler
    std::vector<ConfigSource>             sources;                 // list of source tracks feeding this instrument
    std::optional<std::string>            drumMap;                 // path to drum-map JSON; only valid when name == "Drums"
};

struct Config
{
    std::string                           input;
    std::optional<std::string>            output;
    std::optional<std::string>            title;
    std::optional<std::string>            transcriber;
    std::optional<double>                 tempo;
    int                                   transpose = 0;
    std::vector<ConfigInstrument>         instruments;
};

// Validates a Config against a MIDI file with `midiTrackCount` tracks.
// Returns an empty string on success; otherwise a human-readable error
// message suitable for stderr. All rules are checked eagerly — the
// message reports the first failure.
std::string validateConfig (const Config& config, int midiTrackCount);

} // namespace lotro
```

### Step 2: Rewrite `Source/Core/Config.cpp` validator

```cpp
#include "Config.h"
#include "LotroInstrument.h"

#include <set>
#include <string>

namespace lotro
{

std::string validateConfig (const Config& config, int midiTrackCount)
{
    if (config.input.empty())
        return "config: 'input' path is required";

    if (config.instruments.empty())
        return "config: 'instruments' array must have at least one entry";

    std::set<int> seenX;

    for (size_t idx = 0; idx < config.instruments.size(); ++idx)
    {
        const auto& inst = config.instruments[idx];
        const auto where = "instruments[" + std::to_string (idx) + "]: ";

        if (inst.x < 1)
            return where + "'x' must be >= 1 (got " + std::to_string (inst.x) + ")";

        if (! seenX.insert (inst.x).second)
            return where + "duplicate 'x' value " + std::to_string (inst.x);

        if (inst.name.empty())
            return where + "'name' is required";

        LotroInstrument parsed;
        const auto parseError = parseName (inst.name, parsed);
        if (! parseError.empty())
            return where + parseError;

        if (inst.sources.empty())
            return where + "'sources' array must have at least one entry";

        std::set<int> seenMidi;
        for (size_t si = 0; si < inst.sources.size(); ++si)
        {
            const auto& src = inst.sources[si];
            const auto sWhere = where + "sources[" + std::to_string (si) + "]: ";

            if (src.midiTrackIndex < 0)
                return sWhere + "'midiTrack' must be >= 0 (got "
                     + std::to_string (src.midiTrackIndex) + ")";
            if (src.midiTrackIndex >= midiTrackCount)
                return sWhere + "'midiTrack' index " + std::to_string (src.midiTrackIndex)
                     + " exceeds MIDI track count (" + std::to_string (midiTrackCount) + ")";
            if (! seenMidi.insert (src.midiTrackIndex).second)
                return sWhere + "duplicate MIDI track index " + std::to_string (src.midiTrackIndex)
                     + " in this instrument";

            // volumePercent is an adjustment: 0 = no change, +N louder, -N quieter.
            // <= -100 would silence or invert the velocity.
            if (src.volumePercent <= -100)
                return sWhere + "'volumePercent' must be greater than -100 (got "
                     + std::to_string (src.volumePercent) + ")";
        }

        if (inst.drumMap.has_value() && parsed != LotroInstrument::Drums)
            return where + "'drumMap' is only valid on name == \"Drums\"";
    }

    return {};
}

} // namespace lotro
```

### Step 3: Extend `Source/Core/ConfigLoader.h` to carry migration warnings

Replace the two load-function signatures with versions that accept a `Diagnostics&` for permissive-drop warnings:

```cpp
#pragma once

#include "Config.h"
#include "Diagnostics.h"

#include <string>
#include <string_view>

namespace lotro
{

enum class ConfigFormat { Auto, Json, Toml, Xml };

// Loads and parses a config file. Old-format fields that are no longer part
// of the schema (e.g. instrument-level transposeSemitones or volumePercent)
// are silently dropped; a Warning-severity Diagnostic is pushed onto
// `migrationDiagnostics` so the caller can surface it. Validation is NOT
// performed here.
std::string loadConfigFromFile (const std::string& path,
                                ConfigFormat       format,
                                Config&            config,
                                Diagnostics&       migrationDiagnostics);

std::string loadConfigFromString (std::string_view text,
                                  ConfigFormat     format,
                                  Config&          config,
                                  Diagnostics&     migrationDiagnostics);

} // namespace lotro
```

**Caller-compatibility note.** The old two-argument signatures used to pass `config` alone. Every call site needs a `Diagnostics` object supplied. That's a trivial mechanical update.

### Step 4: Update `Source/Core/ConfigLoader.cpp`

Three loaders change in symmetrical ways. The full replacement file is long; here's the pattern as applied to each loader. **Replace the entire file** with this content:

```cpp
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
```

### Step 5: Update `Source/Core/ConfigWriter.cpp`

Update each format to emit the new source object shape. Replace `Source/Core/ConfigWriter.cpp` with:

```cpp
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
    if (format == ConfigFormat::Auto) format = ConfigFormat::Json;

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
    if (format == ConfigFormat::Auto) format = detectFormat (path);

    std::string text;
    const auto err = writeConfigToString (format, cfg, text);
    if (! err.empty()) return err;

    std::ofstream out (path);
    if (! out) return "config file not writable: " + path;
    out << text;
    return {};
}

} // namespace lotro
```

### Step 6: Update `Source/Core/InstrumentAssembly.cpp`

Two blocks change: the label-fallback `inst.sources.front()` is now a `ConfigSource`; and the per-source loop reads transpose+volume from the source, not the instrument. Inside `assembleInstruments`, replace the body of the per-instrument loop (the block starting `for (const auto& inst : config.instruments)`) — the label fallback, the math setup, and the inner per-source loop — with:

```cpp
    for (const auto& inst : config.instruments)
    {
        Track t;
        t.x = inst.x;
        LotroInstrument parsed = LotroInstrument::LuteOfAges;
        parseName (inst.name, parsed);
        t.instrument = parsed;

        // Label fallback: explicit → first source's MIDI track name → LOTRO enum name.
        if (inst.label.has_value())
        {
            t.name = *inst.label;
        }
        else if (! inst.sources.empty()
                 && inst.sources.front().midiTrackIndex >= 0
                 && inst.sources.front().midiTrackIndex < (int) raw.tracks.size()
                 && ! raw.tracks[(size_t) inst.sources.front().midiTrackIndex].name.empty())
        {
            t.name = raw.tracks[(size_t) inst.sources.front().midiTrackIndex].name;
        }
        else
        {
            t.name = inst.name;
        }

        for (const auto& src : inst.sources)
        {
            const int sIdx = src.midiTrackIndex;
            if (sIdx < 0 || sIdx >= (int) raw.tracks.size()) continue;
            const auto& srcTrack = raw.tracks[(size_t) sIdx];

            if (srcTrack.sourceMidiChannel == 10)
                t.sourceMidiChannel = 10;

            const int    totalTranspose = src.transposeSemitones + config.transpose;
            const double volumeScale    = 1.0 + ((double) src.volumePercent / 100.0);

            for (const auto& srcNote : srcTrack.notes)
            {
                Note n = srcNote;
                n.pitch = srcNote.pitch + totalTranspose;

                if (std::abs (volumeScale - 1.0) > 1e-9)
                {
                    const double scaledD     = (double) srcNote.velocity * volumeScale;
                    const int    scaled      = clampVelocity (scaledD);
                    const bool   clampedHigh = scaledD > 127.0;
                    const bool   clampedLow  = scaledD < 1.0;

                    if (clampedHigh || clampedLow)
                    {
                        Diagnostic d;
                        d.severity         = Severity::Warning;
                        d.source           = "VolumeScale";
                        d.message          = "velocity clamped on '" + t.name
                                           + "' source MIDI-" + std::to_string (sIdx);
                        d.tick             = srcNote.startTick;
                        d.pitch            = srcNote.pitch;
                        d.sourceTrackIndex = srcNote.sourceTrackIndex;
                        d.sourceEventIndex = srcNote.sourceEventIndex;
                        diagnostics.push_back (std::move (d));
                    }
                    n.velocity = scaled;
                }

                t.notes.push_back (n);
            }
        }

        std::stable_sort (t.notes.begin(), t.notes.end(),
                          [] (const Note& a, const Note& b) { return a.startTick < b.startTick; });

        if (parsed == LotroInstrument::Drums && inst.drumMap.has_value())
        {
            DrumMap dm = defaultDrumMap();
            const auto err = loadDrumMapFromFile (*inst.drumMap, dm);
            if (! err.empty())
            {
                Diagnostic d;
                d.severity   = Severity::Warning;
                d.source     = "InstrumentAssembly";
                d.message    = "failed to load drumMap '" + *inst.drumMap + "': " + err;
                d.trackIndex = -1;
                diagnostics.push_back (std::move (d));
            }
            else
            {
                t.drumMap = std::move (dm);
            }
        }

        out.tracks.push_back (std::move (t));
    }
```

Also update `emitUnreferencedDiags` to iterate the new source-object shape:

```cpp
    void emitUnreferencedDiags (const Song&         raw,
                                const Config&       config,
                                Diagnostics&        diagnostics)
    {
        std::set<int> referenced;
        for (const auto& inst : config.instruments)
            for (const auto& s : inst.sources)
                referenced.insert (s.midiTrackIndex);

        for (int i = 0; i < (int) raw.tracks.size(); ++i)
        {
            if (referenced.count (i) > 0) continue;
            Diagnostic d;
            d.severity         = Severity::Info;
            d.source           = "InstrumentAssembly";
            d.message          = "MIDI track " + std::to_string (i)
                               + " not referenced by any instrument; skipped";
            d.trackIndex       = -1;
            d.sourceTrackIndex = i;
            diagnostics.push_back (std::move (d));
        }
    }
```

### Step 7: Update `Source/Core/Pipeline.cpp`'s `synthesiseConfig`

Change the `inst.sources = { (int) i };` line to use a `ConfigSource`:

```cpp
        inst.sources = { ConfigSource{ (int) i, 0, 0 } };
```

Full updated function body:

```cpp
Config synthesiseConfig (const Song&                                  raw,
                         const std::string&                            inputPath,
                         const std::string&                            outputPath,
                         std::optional<double>                         tempo,
                         int                                           transpose,
                         const std::map<int, LotroInstrument>&         instrumentOverrides)
{
    Config cfg;
    cfg.input = inputPath;
    if (! outputPath.empty())
        cfg.output = outputPath;
    if (tempo.has_value())
        cfg.tempo = *tempo;
    cfg.transpose = transpose;

    for (size_t i = 0; i < raw.tracks.size(); ++i)
    {
        ConfigInstrument inst;
        inst.x       = (int) (i + 1);
        inst.sources = { ConfigSource{ (int) i, 0, 0 } };

        const auto picked = pickInstrumentForTrack (raw.tracks[i]);
        inst.name = std::string (displayName (picked));

        const auto overrideIt = instrumentOverrides.find ((int) i);
        if (overrideIt != instrumentOverrides.end())
            inst.name = std::string (displayName (overrideIt->second));

        cfg.instruments.push_back (inst);
    }
    return cfg;
}
```

### Step 8: Update `Source/Main.cpp` call sites to the loader's new signature

In `Source/Main.cpp`, the `loadConfigFromFile` call needs a `Diagnostics&` argument. Find the call:

```cpp
            const auto loadErr = lotro::loadConfigFromFile (
                opts.configFile.getFullPathName().toStdString(),
                format,
                cfg);
```

Replace with:

```cpp
            const auto loadErr = lotro::loadConfigFromFile (
                opts.configFile.getFullPathName().toStdString(),
                format,
                cfg,
                diagnostics);
```

(The `diagnostics` variable is already in scope, declared at the top of the `try` block. The migration warnings flow into it alongside pipeline warnings, which the verbose flag surfaces.)

### Step 9: Update UI call-site references before deleting old UI files

`Source/UI/InstrumentsTable.cpp` and `Source/UI/InstrumentDetailForm.cpp` reference `inst.sources` as `vector<int>`. They get deleted in Phase B, but they must COMPILE before then or the refactor commit is broken.

In `Source/UI/InstrumentsTable.cpp` line 63:

```cpp
            for (int s : inst.sources) parts.add (juce::String (s));
```

Replace with:

```cpp
            for (const auto& s : inst.sources) parts.add (juce::String (s.midiTrackIndex));
```

In `Source/UI/InstrumentDetailForm.cpp` line 114:

```cpp
        cb->setToggleState (
            std::find (inst.sources.begin(), inst.sources.end(), (int) i) != inst.sources.end(),
            juce::dontSendNotification);
```

Replace with:

```cpp
        const bool checked = std::any_of (inst.sources.begin(), inst.sources.end(),
            [i] (const lotro::ConfigSource& s) { return s.midiTrackIndex == (int) i; });
        cb->setToggleState (checked, juce::dontSendNotification);
```

And `Source/UI/InstrumentDetailForm.cpp` line 153:

```cpp
        inst.sources = std::move (picked);
```

Replace with:

```cpp
        std::vector<lotro::ConfigSource> newSources;
        newSources.reserve (picked.size());
        for (int idx : picked) newSources.push_back ({ idx, 0, 0 });
        inst.sources = std::move (newSources);
```

Also ensure `#include <algorithm>` is present for `std::any_of` if not already.

### Step 10: Update `Tests/Config_tests.cpp`

Update the `minimalValid()` helper and replace the removed-field tests:

Find:

```cpp
    lotro::Config minimalValid (int x = 1, const std::string& name = "LuteOfAges")
    {
        lotro::Config c;
        c.input = "song.mid";
        lotro::ConfigInstrument inst;
        inst.x       = x;
        inst.name    = name;
        inst.sources = { 0 };
        c.instruments.push_back (inst);
        return c;
    }
```

Replace `inst.sources = { 0 };` with `inst.sources = { { 0, 0, 0 } };`.

Find the test `"config: source index negative fails"` — update its body:

```cpp
TEST_CASE ("config: source midiTrack negative fails", "[config][validate]")
{
    auto config = minimalValid();
    config.instruments[0].sources = { { -1, 0, 0 } };
    const auto err = lotro::validateConfig (config, 1);
    CHECK_FALSE (err.empty());
    CHECK (err.find ("midiTrack") != std::string::npos);
}
```

Find `"config: source index out of range fails"`:

```cpp
TEST_CASE ("config: source midiTrack out of range fails", "[config][validate]")
{
    auto config = minimalValid();
    config.instruments[0].sources = { { 5, 0, 0 } };
    const auto err = lotro::validateConfig (config, 1);
    CHECK_FALSE (err.empty());
    CHECK (err.find ("midiTrack") != std::string::npos);
}
```

Find the two volumePercent tests (`"config: volumePercent 0 (no change) passes"` and `"config: volumePercent <= -100 fails"`) and rewrite them against sources (volumePercent is now a source-level field):

```cpp
TEST_CASE ("config: source volumePercent 0 (no change) passes", "[config][validate]")
{
    auto config = minimalValid();
    config.instruments[0].sources[0].volumePercent = 0;
    CHECK (lotro::validateConfig (config, 1).empty());
}

TEST_CASE ("config: source volumePercent <= -100 fails (would silence the note)",
           "[config][validate]")
{
    auto config = minimalValid();
    config.instruments[0].sources[0].volumePercent = -100;
    const auto err = lotro::validateConfig (config, 1);
    CHECK_FALSE (err.empty());
    CHECK (err.find ("volumePercent") != std::string::npos);
}
```

Add a new test for the uniqueness rule:

```cpp
TEST_CASE ("config: duplicate midiTrack within one instrument fails", "[config][validate]")
{
    auto config = minimalValid();
    config.instruments[0].sources = { { 0, 0, 0 }, { 0, 0, 0 } };
    const auto err = lotro::validateConfig (config, 5);
    CHECK_FALSE (err.empty());
    CHECK (err.find ("duplicate") != std::string::npos);
}
```

### Step 11: Update `Tests/ConfigLoader_tests.cpp` (JSON)

Every test that checks `sources` now compares against `std::vector<ConfigSource>{…}`. Update each test. Most important: the full round-trip test. Find `"config-loader: JSON full config round-trips all fields"` and update its assertions:

```cpp
    CHECK (lead.sources.size() == 2);
    CHECK (lead.sources[0].midiTrackIndex == 0);
    CHECK (lead.sources[1].midiTrackIndex == 2);
```

(The JSON fixture in the same test still uses integer sources shorthand — that's the backward-compat exercise.)

Update the full-config JSON body to include the new object shape for one source so transpose+volume round-trip:

```cpp
    const std::string text = R"({
        "input": "in.mid",
        "output": "out.abc",
        "title": "A Song",
        "transcriber": "Brian",
        "tempo": 140,
        "transpose": -2,
        "instruments": [
            {
                "x": 1,
                "name": "LuteOfAges",
                "label": "Lead",
                "sources": [0, { "midiTrack": 2, "transposeSemitones": -12, "volumePercent": 10 }]
            },
            {
                "x": 3,
                "name": "Drums",
                "sources": [9],
                "drumMap": "kit.json"
            }
        ]
    })";
```

Assertions:

```cpp
    CHECK (lead.sources.size() == 2);
    CHECK (lead.sources[0].midiTrackIndex    == 0);
    CHECK (lead.sources[0].transposeSemitones == 0);
    CHECK (lead.sources[0].volumePercent     == 0);
    CHECK (lead.sources[1].midiTrackIndex     == 2);
    CHECK (lead.sources[1].transposeSemitones == -12);
    CHECK (lead.sources[1].volumePercent      == 10);
```

The "minimal config" test can keep bare integers — that's the shorthand case. Rename the TEST_CASE to clarify: `"config-loader: JSON minimal config parses (bare-integer sources)"`.

Every `loadConfigFromString` call in this file needs a `lotro::Diagnostics` argument. Update every test body to declare:

```cpp
    lotro::Diagnostics mig;
    const auto err = lotro::loadConfigFromString (text, lotro::ConfigFormat::Json, config, mig);
```

(Or rename to `diag` if cleaner; keep consistent within the file.)

Add a new test for migration warnings:

```cpp
TEST_CASE ("config-loader: JSON old instrument-level fields drop with warning", "[config-loader][json][migration]")
{
    const std::string text = R"({
        "input": "song.mid",
        "instruments": [
            { "x": 1, "name": "LuteOfAges", "sources": [0],
              "transposeSemitones": -12, "volumePercent": 80 }
        ]
    })";

    lotro::Config config;
    lotro::Diagnostics mig;
    const auto err = lotro::loadConfigFromString (text, lotro::ConfigFormat::Json, config, mig);
    REQUIRE (err.empty());
    REQUIRE (config.instruments.size() == 1);
    CHECK (config.instruments[0].sources.size() == 1);
    CHECK (config.instruments[0].sources[0].midiTrackIndex == 0);
    // Warnings
    int warnings = 0;
    for (const auto& d : mig)
        if (d.severity == lotro::Severity::Warning
            && d.source == "ConfigLoader")
            ++warnings;
    CHECK (warnings == 2);
}
```

### Step 12: Update `Tests/ConfigLoaderXml_tests.cpp`

Same pattern: update the full round-trip test to use the new XML shape with source attributes:

```cpp
    const std::string text = R"(<?xml version="1.0"?>
<config>
  <input>in.mid</input>
  <output>out.abc</output>
  <title>A Song</title>
  <transcriber>Brian</transcriber>
  <tempo>140</tempo>
  <transpose>-2</transpose>
  <instruments>
    <instrument x="1" name="LuteOfAges" label="Lead">
      <sources>
        <source>0</source>
        <source midiTrack="2" transposeSemitones="-12" volumePercent="10" />
      </sources>
    </instrument>
    <instrument x="3" name="Drums">
      <sources><source midiTrack="9" /></sources>
      <drumMap>kit.json</drumMap>
    </instrument>
  </instruments>
</config>)";
```

Update assertions to the new source shape and add the migration test for XML with removed instrument-level elements, analogous to JSON.

Every call gets the `Diagnostics&` argument.

### Step 13: Update `Tests/ConfigLoaderToml_tests.cpp`

Analogous update for TOML:

```cpp
    const std::string text = R"(
input = "in.mid"
output = "out.abc"
title = "A Song"
transcriber = "Brian"
tempo = 140
transpose = -2

[[instruments]]
x = 1
name = "LuteOfAges"
label = "Lead"

[[instruments.sources]]
midiTrack = 0

[[instruments.sources]]
midiTrack = 2
transposeSemitones = -12
volumePercent = 10

[[instruments]]
x = 3
name = "Drums"
sources = [9]
drumMap = "kit.json"
)";
```

Assertions updated to the new shape. Migration test added. `Diagnostics&` parameter added to calls.

### Step 14: Update `Tests/ConfigWriter_tests.cpp`

Update `richConfig()` helper:

```cpp
    lotro::Config richConfig()
    {
        lotro::Config c;
        c.input       = "in.mid";
        c.output      = std::string ("out.abc");
        c.title       = std::string ("A Song");
        c.transcriber = std::string ("Brian");
        c.tempo       = 140.0;
        c.transpose   = -2;
        {
            lotro::ConfigInstrument i;
            i.x       = 1;
            i.name    = "LuteOfAges";
            i.label   = std::string ("Lead");
            i.sources = { { 0, 0, 0 }, { 2, -12, 10 } };
            c.instruments.push_back (i);
        }
        {
            lotro::ConfigInstrument i;
            i.x       = 3;
            i.name    = "Drums";
            i.sources = { { 9, 0, 0 } };
            i.drumMap = std::string ("kit.json");
            c.instruments.push_back (i);
        }
        return c;
    }
```

Update `checkConfigsEqual()`:

```cpp
    void checkConfigsEqual (const lotro::Config& a, const lotro::Config& b)
    {
        CHECK (a.input       == b.input);
        CHECK (a.output      == b.output);
        CHECK (a.title       == b.title);
        CHECK (a.transcriber == b.transcriber);
        CHECK (a.tempo       == b.tempo);
        CHECK (a.transpose   == b.transpose);
        REQUIRE (a.instruments.size() == b.instruments.size());
        for (size_t i = 0; i < a.instruments.size(); ++i)
        {
            const auto& ai = a.instruments[i];
            const auto& bi = b.instruments[i];
            CHECK (ai.x       == bi.x);
            CHECK (ai.name    == bi.name);
            CHECK (ai.label   == bi.label);
            CHECK (ai.drumMap == bi.drumMap);
            REQUIRE (ai.sources.size() == bi.sources.size());
            for (size_t j = 0; j < ai.sources.size(); ++j)
            {
                CHECK (ai.sources[j].midiTrackIndex     == bi.sources[j].midiTrackIndex);
                CHECK (ai.sources[j].transposeSemitones == bi.sources[j].transposeSemitones);
                CHECK (ai.sources[j].volumePercent      == bi.sources[j].volumePercent);
            }
        }
    }
```

Every `loadConfigFromString` call needs the `mig` argument (this file uses the loader for round-trip verification).

### Step 15: Update `Tests/InstrumentAssembly_tests.cpp`

Update `threeTrackRaw()` helper — no change needed, that's just Song fixtures.

Update every test that sets `inst.sources`:

```cpp
    inst.sources = { { i, 0, 0 } };         // one source
    inst.sources = { { 0, 0, 0 }, { 2, 0, 0 } };   // two sources
```

Update the two per-source-feature tests:

- `"assembly: volumePercent -20 scales velocity down 20%"` — now sets `inst.sources = { { 0, 0, -20 } };` (per-source volumePercent at position 2 of the initializer).
- `"assembly: volumePercent 0 leaves velocity unchanged"` — `inst.sources = { { 0, 0, 0 } };`.
- `"assembly: volumePercent 100 on velocity 100 clamps to 127 and emits Diagnostic"` — `inst.sources = { { 0, 0, 100 } };`.

Update the transpose tests to set per-source:

- `"assembly: per-instrument transposeSemitones shifts every note"` — rename to `"assembly: per-source transposeSemitones shifts every note from that source"` and set `inst.sources = { { 0, -12, 0 } };`. Remove the `inst.transposeSemitones = -12;` line.
- `"assembly: global transpose adds to per-instrument transpose"` — rename to `"...adds to per-source transpose"`; set `inst.sources = { { 0, -7, 0 } };` and `cfg.transpose = -5;`. Remove `inst.transposeSemitones`.

### Step 16: Update `Tests/ConfigPipeline_tests.cpp`, `Tests/EndToEndConfig_tests.cpp`, `Tests/Pipeline_tests.cpp`

Replace every occurrence of `inst.sources = { … };` with the new syntax. Search the three files with `grep -n "inst.sources = {" Tests/ConfigPipeline_tests.cpp Tests/EndToEndConfig_tests.cpp Tests/Pipeline_tests.cpp` for the full list; each assignment changes from `{ 0, 1 }` style to `{ { 0, 0, 0 }, { 1, 0, 0 } }` style.

For `Tests/Pipeline_tests.cpp`, the synthesised-config assertions compare `cfg.instruments[i].sources == std::vector<int>{ i }`. Update:

```cpp
        REQUIRE (cfg.instruments[(size_t) i].sources.size() == 1);
        CHECK (cfg.instruments[(size_t) i].sources[0].midiTrackIndex == i);
        CHECK (cfg.instruments[(size_t) i].sources[0].transposeSemitones == 0);
        CHECK (cfg.instruments[(size_t) i].sources[0].volumePercent == 0);
```

For `Tests/ConfigPipeline_tests.cpp` and `Tests/EndToEndConfig_tests.cpp`, the `inst.transposeSemitones = N` / `inst.volumePercent = N` lines on instruments go away. Where a test previously tweaked the per-instrument value, move it to the source:

- Replace `inst.transposeSemitones = 12;` with: first source entry is `{ idx, 12, 0 }`.
- Replace `inst.transposeSemitones = -12;` with: first source entry is `{ idx, -12, 0 }`.
- Replace `inst.volumePercent = -70;` with: `inst.sources = { { 0, 0, -70 } };`.

Search each file for those exact-pattern lines with `grep -n "\.transposeSemitones\|\.volumePercent" Tests/ConfigPipeline_tests.cpp Tests/EndToEndConfig_tests.cpp` and apply.

### Step 17: Build the whole thing and iterate until green

```bash
cd /home/brian/sandbox/converter-cli/.worktrees/instruments-treeview
cmake --build build 2>&1 | tail -20
```

If the build fails, fix compile errors one at a time. Common issues:

- `inst.sources.push_back (N)` where N is an `int` — wrap in `ConfigSource{ N, 0, 0 }`.
- `std::vector<int>` comparisons against `inst.sources` — change expected type.
- Missing `#include <algorithm>` for `std::any_of`.

When it builds, run:

```bash
ctest --test-dir build --output-on-failure 2>&1 | tail -30
```

Expected test count: a few more than the baseline (the 4 new tests added: duplicate-midi-in-one-instrument, JSON migration, XML migration, TOML migration — +4 to the baseline). If the baseline was 130, expect `134/134`.

### Step 18: Commit

```bash
git add -A
git commit -m "$(cat <<'EOF'
refactor(config): add ConfigSource; move transpose/volume off instruments

Large atomic migration of the Config data model:

- New ConfigSource struct carries per-source midiTrackIndex +
  transposeSemitones + volumePercent.
- ConfigInstrument.sources is now vector<ConfigSource>
  (was vector<int>). ConfigInstrument loses transposeSemitones and
  volumePercent — those knobs now live on individual sources.
- Config.transpose stays song-wide; Config still has no volumePercent
  (volume lives ONLY on sources, per the spec).

Pipeline + loaders + writers + synthesiseConfig + validator all
updated for the new shape. Each loader accepts both the new object
shape and a bare-integer shorthand for backward compatibility.
Removed instrument-level fields in old configs are dropped during
load with a Warning Diagnostic ("ignoring removed field ...").

Loader signatures gained a Diagnostics& out-parameter for migration
warnings. Callers (Main.cpp and all loader tests) updated.

InstrumentAssembly math now reads transpose and volume per-source.
VolumeScale clamp diagnostic message includes the MIDI track index
for clarity when one instrument has multiple sources.

Existing test fixtures updated throughout. Four new tests cover:
the duplicate-midi-in-one-instrument rule and per-format migration
warnings for the three loaders.

Spec reference:
  docs/superpowers/specs/2026-04-23-instruments-treeview-redesign-design.md

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Phase B — UI scaffolding

This phase creates the new UI classes as minimal stubs, rewires `EditorPane` to use them, and deletes the old ones. Each task ends with a compiling build — the UI won't do anything meaningful yet.

### Task 2: Create five stub UI classes

**Files (create, stub-level):**
- `Source/UI/SongPropertyPage.h`
- `Source/UI/SongPropertyPage.cpp`
- `Source/UI/InstrumentPropertyPage.h`
- `Source/UI/InstrumentPropertyPage.cpp`
- `Source/UI/SourcePropertyPage.h`
- `Source/UI/SourcePropertyPage.cpp`
- `Source/UI/PropertyPageHost.h`
- `Source/UI/PropertyPageHost.cpp`
- `Source/UI/InstrumentsTree.h`
- `Source/UI/InstrumentsTree.cpp`

Each stub is a minimal `juce::Component` subclass that compiles — real content lands later.

- [ ] **Step 1: Create SongPropertyPage stub**

`Source/UI/SongPropertyPage.h`:

```cpp
#pragma once

#include "Core/Config.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace lotro
{

class SongPropertyPage : public juce::Component
{
public:
    SongPropertyPage (Config& configRef, std::function<void()> onChange);

    void refresh();
    void resized() override;

private:
    Config&               config;
    std::function<void()> notifyChange;
};

} // namespace lotro
```

`Source/UI/SongPropertyPage.cpp`:

```cpp
#include "SongPropertyPage.h"

namespace lotro
{

SongPropertyPage::SongPropertyPage (Config& cfgRef, std::function<void()> onChange)
    : config (cfgRef), notifyChange (std::move (onChange))
{
}

void SongPropertyPage::refresh() {}
void SongPropertyPage::resized() {}

} // namespace lotro
```

- [ ] **Step 2: Create InstrumentPropertyPage stub**

`Source/UI/InstrumentPropertyPage.h`:

```cpp
#pragma once

#include "Core/Config.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace lotro
{

class InstrumentPropertyPage : public juce::Component
{
public:
    InstrumentPropertyPage (Config& configRef, std::function<void()> onChange);

    // Sets which instrument index the form edits (-1 for "no selection").
    void editInstrument (int instrumentIndex);
    void refresh();
    void resized() override;

private:
    Config&               config;
    std::function<void()> notifyChange;
    int                   currentIndex = -1;
};

} // namespace lotro
```

`Source/UI/InstrumentPropertyPage.cpp`:

```cpp
#include "InstrumentPropertyPage.h"

namespace lotro
{

InstrumentPropertyPage::InstrumentPropertyPage (Config& cfgRef, std::function<void()> onChange)
    : config (cfgRef), notifyChange (std::move (onChange))
{
}

void InstrumentPropertyPage::editInstrument (int instrumentIndex)
{
    currentIndex = instrumentIndex;
    refresh();
}

void InstrumentPropertyPage::refresh() {}
void InstrumentPropertyPage::resized() {}

} // namespace lotro
```

- [ ] **Step 3: Create SourcePropertyPage stub**

`Source/UI/SourcePropertyPage.h`:

```cpp
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
```

`Source/UI/SourcePropertyPage.cpp`:

```cpp
#include "SourcePropertyPage.h"

namespace lotro
{

SourcePropertyPage::SourcePropertyPage (Config& cfgRef, const Song& rawRef,
                                        std::function<void()> onChange)
    : config (cfgRef), raw (rawRef), notifyChange (std::move (onChange))
{
}

void SourcePropertyPage::editSource (int instrumentIdx, int sourceIdx)
{
    instrumentIndex = instrumentIdx;
    sourceIndex     = sourceIdx;
    refresh();
}

void SourcePropertyPage::refresh() {}
void SourcePropertyPage::resized() {}

} // namespace lotro
```

- [ ] **Step 4: Create PropertyPageHost stub**

`Source/UI/PropertyPageHost.h`:

```cpp
#pragma once

#include "Core/Config.h"
#include "Core/Song.h"

#include "SongPropertyPage.h"
#include "InstrumentPropertyPage.h"
#include "SourcePropertyPage.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace lotro
{

class PropertyPageHost : public juce::Component
{
public:
    enum class Kind { None, Song, Instrument, Source };

    PropertyPageHost (Config& configRef, const Song& rawRef,
                      std::function<void()> onChange);

    // Show the appropriate property page for the given selection. For
    // Instrument, pass (instrumentIndex, -1); for Source, pass
    // (instrumentIndex, sourceIndex).
    void showFor (Kind kind, int instrumentIndex = -1, int sourceIndex = -1);

    // Called after external mutation to the underlying Config or Song so
    // the visible page can re-read its source data.
    void refresh();

    void resized() override;

private:
    Config&                                 config;
    const Song&                             raw;
    std::unique_ptr<SongPropertyPage>       songPage;
    std::unique_ptr<InstrumentPropertyPage> instrumentPage;
    std::unique_ptr<SourcePropertyPage>     sourcePage;
    Kind                                    currentKind = Kind::None;
};

} // namespace lotro
```

`Source/UI/PropertyPageHost.cpp`:

```cpp
#include "PropertyPageHost.h"

namespace lotro
{

PropertyPageHost::PropertyPageHost (Config& cfgRef, const Song& rawRef,
                                    std::function<void()> onChange)
    : config (cfgRef), raw (rawRef),
      songPage       (std::make_unique<SongPropertyPage>       (config, onChange)),
      instrumentPage (std::make_unique<InstrumentPropertyPage> (config, onChange)),
      sourcePage     (std::make_unique<SourcePropertyPage>     (config, raw, onChange))
{
    addChildComponent (*songPage);
    addChildComponent (*instrumentPage);
    addChildComponent (*sourcePage);
}

void PropertyPageHost::showFor (Kind kind, int instrumentIndex, int sourceIndex)
{
    songPage->setVisible       (kind == Kind::Song);
    instrumentPage->setVisible (kind == Kind::Instrument);
    sourcePage->setVisible     (kind == Kind::Source);

    if (kind == Kind::Instrument)
        instrumentPage->editInstrument (instrumentIndex);
    if (kind == Kind::Source)
        sourcePage->editSource (instrumentIndex, sourceIndex);
    if (kind == Kind::Song)
        songPage->refresh();

    currentKind = kind;
    resized();
}

void PropertyPageHost::refresh()
{
    songPage->refresh();
    instrumentPage->refresh();
    sourcePage->refresh();
}

void PropertyPageHost::resized()
{
    const auto bounds = getLocalBounds();
    songPage->setBounds (bounds);
    instrumentPage->setBounds (bounds);
    sourcePage->setBounds (bounds);
}

} // namespace lotro
```

- [ ] **Step 5: Create InstrumentsTree stub**

`Source/UI/InstrumentsTree.h`:

```cpp
#pragma once

#include "Core/Config.h"
#include "Core/Song.h"

#include "PropertyPageHost.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace lotro
{

class InstrumentsTree : public juce::Component
{
public:
    InstrumentsTree (Config&     configRef,
                     const Song& rawRef,
                     std::function<void (PropertyPageHost::Kind, int, int)> onSelection,
                     std::function<void()> onMutation);

    // Rebuilds the tree from scratch to match the current Config + Song.
    void rebuild();

    void resized() override;

private:
    Config&                                                config;
    const Song&                                             raw;
    std::function<void (PropertyPageHost::Kind, int, int)>  notifySelection;
    std::function<void()>                                   notifyMutation;
    juce::TreeView                                          treeView;
};

} // namespace lotro
```

`Source/UI/InstrumentsTree.cpp`:

```cpp
#include "InstrumentsTree.h"

namespace lotro
{

InstrumentsTree::InstrumentsTree (Config& cfgRef, const Song& rawRef,
                                  std::function<void (PropertyPageHost::Kind, int, int)> onSel,
                                  std::function<void()> onMut)
    : config (cfgRef), raw (rawRef),
      notifySelection (std::move (onSel)),
      notifyMutation  (std::move (onMut))
{
    addAndMakeVisible (treeView);
}

void InstrumentsTree::rebuild() {}
void InstrumentsTree::resized() { treeView.setBounds (getLocalBounds()); }

} // namespace lotro
```

- [ ] **Step 6: Register the five new source files in CMakeLists.txt**

In `CMakeLists.txt`, inside `target_sources(converter_ui PRIVATE …)`, add the five new `.cpp` files alongside the existing ones (don't delete the old ones yet — that's Task 3):

```cmake
target_sources(converter_ui PRIVATE
    Source/UI/UiMain.cpp
    Source/UI/MainWindow.cpp
    Source/UI/EditorPane.cpp
    Source/UI/GlobalSettingsView.cpp
    Source/UI/InstrumentsTable.cpp
    Source/UI/InstrumentDetailForm.cpp
    Source/UI/DiagnosticsPane.cpp
    Source/UI/DiagnosticListView.cpp
    Source/UI/AbcPreviewView.cpp
    Source/UI/SongPropertyPage.cpp
    Source/UI/InstrumentPropertyPage.cpp
    Source/UI/SourcePropertyPage.cpp
    Source/UI/PropertyPageHost.cpp
    Source/UI/InstrumentsTree.cpp)
```

- [ ] **Step 7: Build and verify**

```bash
cmake --build build 2>&1 | tail -5
ctest --test-dir build 2>&1 | tail -3
```

Expected: clean build, `134/134` tests still passing. The new classes are registered but no consumer uses them yet.

- [ ] **Step 8: Commit**

```bash
git add Source/UI/SongPropertyPage.h Source/UI/SongPropertyPage.cpp \
        Source/UI/InstrumentPropertyPage.h Source/UI/InstrumentPropertyPage.cpp \
        Source/UI/SourcePropertyPage.h Source/UI/SourcePropertyPage.cpp \
        Source/UI/PropertyPageHost.h Source/UI/PropertyPageHost.cpp \
        Source/UI/InstrumentsTree.h Source/UI/InstrumentsTree.cpp \
        CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(ui): stub new property pages, host, and instruments tree

Adds five new UI classes as no-op stubs that compile but don't yet
render anything meaningful:
  - SongPropertyPage, InstrumentPropertyPage, SourcePropertyPage
  - PropertyPageHost (swaps the visible page)
  - InstrumentsTree (TreeView host)

Registered in CMake but not yet consumed by EditorPane; wired in the
next task.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: Rewire EditorPane and delete old UI files

**Files:**
- Modify: `Source/UI/EditorPane.h`
- Modify: `Source/UI/EditorPane.cpp`
- Modify: `CMakeLists.txt`
- Delete: `Source/UI/GlobalSettingsView.h`, `Source/UI/GlobalSettingsView.cpp`
- Delete: `Source/UI/InstrumentsTable.h`, `Source/UI/InstrumentsTable.cpp`
- Delete: `Source/UI/InstrumentDetailForm.h`, `Source/UI/InstrumentDetailForm.cpp`

- [ ] **Step 1: Rewrite `Source/UI/EditorPane.h`**

```cpp
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
```

- [ ] **Step 2: Rewrite `Source/UI/EditorPane.cpp`**

```cpp
#include "EditorPane.h"
#include "InstrumentsTree.h"
#include "PropertyPageHost.h"

namespace lotro
{

EditorPane::EditorPane()
{
    host = std::make_unique<PropertyPageHost> (
        config, raw,
        [this] { if (onConfigChanged) onConfigChanged(); });

    tree = std::make_unique<InstrumentsTree> (
        config, raw,
        /*onSelection*/ [this] (PropertyPageHost::Kind k, int iIdx, int sIdx)
                         { host->showFor (k, iIdx, sIdx); },
        /*onMutation*/  [this] { if (onConfigChanged) onConfigChanged();
                                  host->refresh(); });

    addAndMakeVisible (*tree);
    addAndMakeVisible (*host);
    addAndMakeVisible (runButton);

    runButton.onClick = [this] { if (onRunRequested) onRunRequested(); };
    runButton.setEnabled (false);
}

EditorPane::~EditorPane() = default;

void EditorPane::loadFromMidi (Song newRaw, Config newCfg)
{
    raw    = std::move (newRaw);
    config = std::move (newCfg);
    tree->rebuild();
    host->showFor (PropertyPageHost::Kind::Song);
    host->refresh();
    runButton.setEnabled (! raw.tracks.empty());
    repaint();
    if (onConfigChanged) onConfigChanged();
}

void EditorPane::resized()
{
    auto area = getLocalBounds().reduced (8);
    runButton.setBounds (area.removeFromBottom (32));
    area.removeFromBottom (8);
    const int treeHeight = area.getHeight() * 2 / 5;   // tree gets top 40%
    tree->setBounds (area.removeFromTop (treeHeight));
    area.removeFromTop (8);
    host->setBounds (area);
}

void EditorPane::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::white);
    if (raw.tracks.empty())
    {
        g.setColour (juce::Colours::grey);
        g.setFont (16.0f);
        g.drawText ("Drop a MIDI file here, or use File -> Open MIDI...",
                    getLocalBounds(), juce::Justification::centred);
    }
}

} // namespace lotro
```

- [ ] **Step 3: Delete the old files**

```bash
git rm Source/UI/GlobalSettingsView.h Source/UI/GlobalSettingsView.cpp \
       Source/UI/InstrumentsTable.h Source/UI/InstrumentsTable.cpp \
       Source/UI/InstrumentDetailForm.h Source/UI/InstrumentDetailForm.cpp
```

- [ ] **Step 4: Remove the deleted files from `CMakeLists.txt`**

In `target_sources(converter_ui PRIVATE …)`, drop the three lines:

```cmake
    Source/UI/GlobalSettingsView.cpp
    Source/UI/InstrumentsTable.cpp
    Source/UI/InstrumentDetailForm.cpp
```

Final list should look like:

```cmake
target_sources(converter_ui PRIVATE
    Source/UI/UiMain.cpp
    Source/UI/MainWindow.cpp
    Source/UI/EditorPane.cpp
    Source/UI/DiagnosticsPane.cpp
    Source/UI/DiagnosticListView.cpp
    Source/UI/AbcPreviewView.cpp
    Source/UI/SongPropertyPage.cpp
    Source/UI/InstrumentPropertyPage.cpp
    Source/UI/SourcePropertyPage.cpp
    Source/UI/PropertyPageHost.cpp
    Source/UI/InstrumentsTree.cpp)
```

- [ ] **Step 5: Build and verify**

```bash
cmake --build build 2>&1 | tail -10
ctest --test-dir build 2>&1 | tail -3
```

Expected: clean build, `134/134` passing. `converter_ui` binary exists at `build/converter_ui_artefacts/Debug/converter_ui`. Running it now would show an empty editor pane (tree is stubbed, property pages are stubbed) — that's expected until Phase C lands.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "$(cat <<'EOF'
refactor(ui): rewire EditorPane to use PropertyPageHost + InstrumentsTree

EditorPane now owns the tree (top 40%) and the property-page host
(remaining area above the Run button). The three old sub-views
(GlobalSettingsView, InstrumentsTable, InstrumentDetailForm) are
deleted; their roles are subsumed into the new classes which will be
fleshed out in Phase C.

Build still green, tests still green. The UI's editor pane is
functionally blank until property pages and tree items are filled
in, but the scaffolding is in place.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Phase C — Fill in the UI

Five tasks, each flesh out one class with the real content.

### Task 4: Implement `SongPropertyPage`

**Files:**
- Modify: `Source/UI/SongPropertyPage.h`
- Modify: `Source/UI/SongPropertyPage.cpp`

The Song property page is the six-row form formerly known as GlobalSettingsView — Input MIDI (read-only), Output ABC (read-only), Title, Transcriber, Tempo, Global transpose.

- [ ] **Step 1: Update `Source/UI/SongPropertyPage.h`**

```cpp
#pragma once

#include "Core/Config.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace lotro
{

class SongPropertyPage : public juce::Component,
                         private juce::TextEditor::Listener
{
public:
    SongPropertyPage (Config& configRef, std::function<void()> onChange);

    void refresh();
    void resized() override;

private:
    void textEditorTextChanged (juce::TextEditor&) override;

    Config&               config;
    std::function<void()> notifyChange;

    juce::Label       inputLabel       { {}, "Input MIDI:" };
    juce::Label       inputValue;
    juce::Label       outputLabel      { {}, "Output ABC:" };
    juce::Label       outputValue;
    juce::Label       titleLabel       { {}, "Title:" };
    juce::TextEditor  titleField;
    juce::Label       transcriberLabel { {}, "Transcriber:" };
    juce::TextEditor  transcriberField;
    juce::Label       tempoLabel       { {}, "Tempo (BPM):" };
    juce::TextEditor  tempoField;
    juce::Label       transposeLabel   { {}, "Global transpose:" };
    juce::TextEditor  transposeField;
};

} // namespace lotro
```

- [ ] **Step 2: Implement `Source/UI/SongPropertyPage.cpp`**

```cpp
#include "SongPropertyPage.h"

namespace lotro
{

SongPropertyPage::SongPropertyPage (Config& cfgRef, std::function<void()> onChange)
    : config (cfgRef), notifyChange (std::move (onChange))
{
    auto setUpLabel = [this] (juce::Label& l)
    {
        addAndMakeVisible (l);
        l.setColour (juce::Label::textColourId, juce::Colours::darkgrey);
    };
    auto setUpField = [this] (juce::TextEditor& f, bool numeric)
    {
        addAndMakeVisible (f);
        f.setMultiLine (false);
        f.setReturnKeyStartsNewLine (false);
        f.setBorder (juce::BorderSize<int> (1));
        if (numeric) f.setInputRestrictions (16, "-0123456789.");
        f.addListener (this);
    };

    setUpLabel (inputLabel);       setUpLabel (inputValue);
    setUpLabel (outputLabel);      setUpLabel (outputValue);
    setUpLabel (titleLabel);       setUpField (titleField, false);
    setUpLabel (transcriberLabel); setUpField (transcriberField, false);
    setUpLabel (tempoLabel);       setUpField (tempoField, true);
    setUpLabel (transposeLabel);   setUpField (transposeField, true);

    refresh();
}

void SongPropertyPage::refresh()
{
    inputValue.setText  (juce::String (config.input),
                         juce::dontSendNotification);
    outputValue.setText (juce::String (config.output.value_or (std::string{})),
                         juce::dontSendNotification);
    titleField.setText  (juce::String (config.title.value_or (std::string{})),
                         juce::dontSendNotification);
    transcriberField.setText (juce::String (config.transcriber.value_or (std::string{})),
                              juce::dontSendNotification);
    tempoField.setText  (config.tempo.has_value() ? juce::String (*config.tempo) : juce::String(),
                         juce::dontSendNotification);
    transposeField.setText (juce::String (config.transpose), juce::dontSendNotification);
}

void SongPropertyPage::resized()
{
    auto area = getLocalBounds().reduced (4);
    const int rowH = 26;
    const int labelW = 140;

    auto row = [&] (juce::Label& l, juce::Component& f)
    {
        auto r = area.removeFromTop (rowH);
        l.setBounds (r.removeFromLeft (labelW));
        f.setBounds (r);
        area.removeFromTop (4);
    };

    row (inputLabel,       inputValue);
    row (outputLabel,      outputValue);
    row (titleLabel,       titleField);
    row (transcriberLabel, transcriberField);
    row (tempoLabel,       tempoField);
    row (transposeLabel,   transposeField);
}

void SongPropertyPage::textEditorTextChanged (juce::TextEditor& ed)
{
    if (&ed == &titleField)
    {
        const auto s = titleField.getText().toStdString();
        config.title = s.empty() ? std::optional<std::string>{} : std::optional<std::string>{ s };
    }
    else if (&ed == &transcriberField)
    {
        const auto s = transcriberField.getText().toStdString();
        config.transcriber = s.empty() ? std::optional<std::string>{} : std::optional<std::string>{ s };
    }
    else if (&ed == &tempoField)
    {
        const auto s = tempoField.getText();
        if (s.isEmpty()) config.tempo.reset();
        else             config.tempo = s.getDoubleValue();
    }
    else if (&ed == &transposeField)
    {
        config.transpose = transposeField.getText().getIntValue();
    }
    if (notifyChange) notifyChange();
}

} // namespace lotro
```

- [ ] **Step 3: Build**

```bash
cmake --build build 2>&1 | tail -3
ctest --test-dir build 2>&1 | tail -3
```

Expected: clean build, `134/134`.

- [ ] **Step 4: Commit**

```bash
git add Source/UI/SongPropertyPage.h Source/UI/SongPropertyPage.cpp
git commit -m "$(cat <<'EOF'
feat(ui): flesh out SongPropertyPage

Six-row form: Input MIDI + Output ABC as read-only labels; Title,
Transcriber, Tempo, Global transpose as editable fields. Edits
push back into the Config and fire the onConfigChanged callback.
Structurally identical to the old GlobalSettingsView it replaces.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 5: Implement `InstrumentPropertyPage`

**Files:**
- Modify: `Source/UI/InstrumentPropertyPage.h`
- Modify: `Source/UI/InstrumentPropertyPage.cpp`

Per the spec, this page has: X: index (numeric), Name (dropdown), Label (text), Drum map (text + Browse; enabled only when Name == "Drums").

- [ ] **Step 1: Update `Source/UI/InstrumentPropertyPage.h`**

```cpp
#pragma once

#include "Core/Config.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace lotro
{

class InstrumentPropertyPage : public juce::Component,
                               private juce::TextEditor::Listener,
                               private juce::ComboBox::Listener
{
public:
    InstrumentPropertyPage (Config& configRef, std::function<void()> onChange);

    void editInstrument (int instrumentIndex);
    void refresh();
    void resized() override;

private:
    void textEditorTextChanged (juce::TextEditor&) override;
    void comboBoxChanged       (juce::ComboBox*) override;
    void pushToConfig();

    Config&               config;
    std::function<void()> notifyChange;
    int                   currentIndex = -1;

    juce::Label      xLabel          { {}, "X: index:" };
    juce::TextEditor xField;
    juce::Label      nameLabel       { {}, "Name:" };
    juce::ComboBox   nameCombo;
    juce::Label      labelLabel      { {}, "Label:" };
    juce::TextEditor labelField;
    juce::Label      drumMapLabel    { {}, "Drum map:" };
    juce::TextEditor drumMapField;
    juce::TextButton drumMapBrowse   { "Browse..." };
    std::unique_ptr<juce::FileChooser> fileChooser;
};

} // namespace lotro
```

- [ ] **Step 2: Implement `Source/UI/InstrumentPropertyPage.cpp`**

```cpp
#include "InstrumentPropertyPage.h"
#include "Core/LotroInstrument.h"

namespace lotro
{

InstrumentPropertyPage::InstrumentPropertyPage (Config& cfgRef, std::function<void()> onChange)
    : config (cfgRef), notifyChange (std::move (onChange))
{
    auto setUpLabel = [this] (juce::Label& l)
    {
        addAndMakeVisible (l);
        l.setColour (juce::Label::textColourId, juce::Colours::darkgrey);
    };
    auto setUpField = [this] (juce::TextEditor& f, bool numeric)
    {
        addAndMakeVisible (f);
        f.setMultiLine (false);
        f.setBorder (juce::BorderSize<int> (1));
        if (numeric) f.setInputRestrictions (8, "0123456789");
        f.addListener (this);
    };

    setUpLabel (xLabel);
    setUpField (xField, true);
    setUpLabel (nameLabel);
    addAndMakeVisible (nameCombo);
    int id = 1;
    for (const auto name : allInstrumentNames())
        nameCombo.addItem (juce::String (std::string (name)), id++);
    nameCombo.addListener (this);

    setUpLabel (labelLabel);
    setUpField (labelField, false);
    setUpLabel (drumMapLabel);
    setUpField (drumMapField, false);
    addAndMakeVisible (drumMapBrowse);
    drumMapBrowse.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser> (
            "Choose drum map", juce::File(), "*.json");
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();
                if (file == juce::File()) return;
                drumMapField.setText (file.getFullPathName(), juce::sendNotification);
            });
    };

    refresh();
}

void InstrumentPropertyPage::editInstrument (int idx)
{
    currentIndex = idx;
    refresh();
}

void InstrumentPropertyPage::refresh()
{
    const bool valid = currentIndex >= 0
                    && currentIndex < (int) config.instruments.size();

    xField.setEnabled         (valid);
    nameCombo.setEnabled      (valid);
    labelField.setEnabled     (valid);
    drumMapField.setEnabled   (false);
    drumMapBrowse.setEnabled  (false);

    if (! valid)
    {
        xField.setText       ({}, juce::dontSendNotification);
        labelField.setText   ({}, juce::dontSendNotification);
        drumMapField.setText ({}, juce::dontSendNotification);
        nameCombo.setSelectedId (0, juce::dontSendNotification);
        repaint();
        return;
    }

    const auto& inst = config.instruments[(size_t) currentIndex];
    xField.setText (juce::String (inst.x), juce::dontSendNotification);
    labelField.setText (juce::String (inst.label.value_or (std::string{})),
                        juce::dontSendNotification);

    int comboId = 1;
    for (const auto name : allInstrumentNames())
    {
        if (std::string (name) == inst.name)
        {
            nameCombo.setSelectedId (comboId, juce::dontSendNotification);
            break;
        }
        ++comboId;
    }

    drumMapField.setText (juce::String (inst.drumMap.value_or (std::string{})),
                          juce::dontSendNotification);

    LotroInstrument parsed = LotroInstrument::LuteOfAges;
    parseName (inst.name, parsed);
    const bool isDrums = (parsed == LotroInstrument::Drums);
    drumMapField.setEnabled  (isDrums);
    drumMapBrowse.setEnabled (isDrums);

    repaint();
}

void InstrumentPropertyPage::pushToConfig()
{
    if (currentIndex < 0 || currentIndex >= (int) config.instruments.size()) return;
    auto& inst = config.instruments[(size_t) currentIndex];

    {
        const auto x = xField.getText().getIntValue();
        if (x > 0) inst.x = x;   // validator catches x < 1
    }
    {
        const auto sel = nameCombo.getSelectedId();
        if (sel > 0)
        {
            const auto names = allInstrumentNames();
            if ((size_t) sel <= names.size())
                inst.name = std::string (names[(size_t) sel - 1]);
        }
    }
    {
        const auto s = labelField.getText().toStdString();
        inst.label = s.empty() ? std::optional<std::string>{} : std::optional<std::string>{ s };
    }
    {
        const auto s = drumMapField.getText().toStdString();
        inst.drumMap = s.empty() ? std::optional<std::string>{} : std::optional<std::string>{ s };
    }

    // Drum-map enable tied to Name.
    LotroInstrument parsed = LotroInstrument::LuteOfAges;
    parseName (inst.name, parsed);
    const bool isDrums = (parsed == LotroInstrument::Drums);
    drumMapField.setEnabled  (isDrums);
    drumMapBrowse.setEnabled (isDrums);

    if (notifyChange) notifyChange();
}

void InstrumentPropertyPage::textEditorTextChanged (juce::TextEditor&) { pushToConfig(); }
void InstrumentPropertyPage::comboBoxChanged       (juce::ComboBox*)    { pushToConfig(); }

void InstrumentPropertyPage::resized()
{
    auto area = getLocalBounds().reduced (4);
    const int rowH = 26;
    const int labelW = 120;

    auto row = [&] (juce::Label& l, juce::Component& f)
    {
        auto r = area.removeFromTop (rowH);
        l.setBounds (r.removeFromLeft (labelW));
        f.setBounds (r);
        area.removeFromTop (4);
    };

    row (xLabel,     xField);
    row (nameLabel,  nameCombo);
    row (labelLabel, labelField);

    // drumMap row has an extra Browse button on the right.
    {
        auto r = area.removeFromTop (rowH);
        drumMapLabel.setBounds (r.removeFromLeft (labelW));
        drumMapBrowse.setBounds (r.removeFromRight (80));
        drumMapField.setBounds (r.withTrimmedRight (4));
        area.removeFromTop (4);
    }
}

} // namespace lotro
```

- [ ] **Step 3: Build + verify**

```bash
cmake --build build 2>&1 | tail -3
ctest --test-dir build 2>&1 | tail -3
```

Expected: clean build, `134/134`.

- [ ] **Step 4: Commit**

```bash
git add Source/UI/InstrumentPropertyPage.h Source/UI/InstrumentPropertyPage.cpp
git commit -m "$(cat <<'EOF'
feat(ui): flesh out InstrumentPropertyPage

Four-row form: X: index (numeric), Name (LOTRO instrument dropdown),
Label (text), Drum map (text + Browse). Drum-map field+button are
enabled only when Name resolves to LotroInstrument::Drums. Edits
push back into the Config.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 6: Implement `SourcePropertyPage`

**Files:**
- Modify: `Source/UI/SourcePropertyPage.h`
- Modify: `Source/UI/SourcePropertyPage.cpp`

Three-row form: read-only MIDI info; transpose semitones; volume %.

- [ ] **Step 1: Update `Source/UI/SourcePropertyPage.h`**

```cpp
#pragma once

#include "Core/Config.h"
#include "Core/Song.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace lotro
{

class SourcePropertyPage : public juce::Component,
                           private juce::TextEditor::Listener
{
public:
    SourcePropertyPage (Config& configRef, const Song& rawRef,
                        std::function<void()> onChange);

    void editSource (int instrumentIndex, int sourceIndex);
    void refresh();
    void resized() override;

private:
    void textEditorTextChanged (juce::TextEditor&) override;
    void pushToConfig();

    Config&               config;
    const Song&           raw;
    std::function<void()> notifyChange;
    int                   instrumentIndex = -1;
    int                   sourceIndex     = -1;

    juce::Label      midiInfoLabel { {}, "MIDI track:" };
    juce::Label      midiInfoValue;
    juce::Label      transposeLabel { {}, "Transpose semitones:" };
    juce::TextEditor transposeField;
    juce::Label      volumeLabel  { {}, "Volume %:" };
    juce::TextEditor volumeField;
};

} // namespace lotro
```

- [ ] **Step 2: Implement `Source/UI/SourcePropertyPage.cpp`**

```cpp
#include "SourcePropertyPage.h"

namespace lotro
{

SourcePropertyPage::SourcePropertyPage (Config& cfgRef, const Song& rawRef,
                                        std::function<void()> onChange)
    : config (cfgRef), raw (rawRef), notifyChange (std::move (onChange))
{
    auto setUpLabel = [this] (juce::Label& l)
    {
        addAndMakeVisible (l);
        l.setColour (juce::Label::textColourId, juce::Colours::darkgrey);
    };
    auto setUpField = [this] (juce::TextEditor& f)
    {
        addAndMakeVisible (f);
        f.setMultiLine (false);
        f.setBorder (juce::BorderSize<int> (1));
        f.setInputRestrictions (8, "-0123456789");
        f.addListener (this);
    };

    setUpLabel (midiInfoLabel);
    setUpLabel (midiInfoValue);
    setUpLabel (transposeLabel);
    setUpField (transposeField);
    setUpLabel (volumeLabel);
    setUpField (volumeField);

    refresh();
}

void SourcePropertyPage::editSource (int iIdx, int sIdx)
{
    instrumentIndex = iIdx;
    sourceIndex     = sIdx;
    refresh();
}

void SourcePropertyPage::refresh()
{
    const bool valid =
        instrumentIndex >= 0 && instrumentIndex < (int) config.instruments.size()
        && sourceIndex >= 0
        && sourceIndex < (int) config.instruments[(size_t) instrumentIndex].sources.size();

    transposeField.setEnabled (valid);
    volumeField.setEnabled    (valid);

    if (! valid)
    {
        midiInfoValue.setText  ("(no selection)", juce::dontSendNotification);
        transposeField.setText ({}, juce::dontSendNotification);
        volumeField.setText    ({}, juce::dontSendNotification);
        return;
    }

    const auto& src = config.instruments[(size_t) instrumentIndex]
                            .sources[(size_t) sourceIndex];

    juce::String info = "MIDI " + juce::String (src.midiTrackIndex);
    if (src.midiTrackIndex >= 0 && src.midiTrackIndex < (int) raw.tracks.size())
    {
        const auto& t = raw.tracks[(size_t) src.midiTrackIndex];
        info += ": " + juce::String (t.name)
              + " (chan " + juce::String (t.sourceMidiChannel)
              + ", " + juce::String ((int) t.notes.size()) + " notes)";
    }
    midiInfoValue.setText (info, juce::dontSendNotification);

    transposeField.setText (juce::String (src.transposeSemitones),
                            juce::dontSendNotification);
    volumeField.setText (juce::String (src.volumePercent),
                         juce::dontSendNotification);
}

void SourcePropertyPage::pushToConfig()
{
    if (instrumentIndex < 0 || instrumentIndex >= (int) config.instruments.size()) return;
    auto& inst = config.instruments[(size_t) instrumentIndex];
    if (sourceIndex < 0 || sourceIndex >= (int) inst.sources.size()) return;
    auto& src = inst.sources[(size_t) sourceIndex];

    {
        const auto t = transposeField.getText();
        src.transposeSemitones = t.isEmpty() ? 0 : t.getIntValue();
    }
    {
        const auto v = volumeField.getText();
        src.volumePercent = v.isEmpty() ? 0 : v.getIntValue();
    }

    if (notifyChange) notifyChange();
}

void SourcePropertyPage::textEditorTextChanged (juce::TextEditor&) { pushToConfig(); }

void SourcePropertyPage::resized()
{
    auto area = getLocalBounds().reduced (4);
    const int rowH = 26;
    const int labelW = 180;

    auto row = [&] (juce::Label& l, juce::Component& f)
    {
        auto r = area.removeFromTop (rowH);
        l.setBounds (r.removeFromLeft (labelW));
        f.setBounds (r);
        area.removeFromTop (4);
    };

    row (midiInfoLabel,  midiInfoValue);
    row (transposeLabel, transposeField);
    row (volumeLabel,    volumeField);
}

} // namespace lotro
```

- [ ] **Step 3: Build + verify**

```bash
cmake --build build 2>&1 | tail -3
ctest --test-dir build 2>&1 | tail -3
```

Expected: clean build, `134/134`.

- [ ] **Step 4: Commit**

```bash
git add Source/UI/SourcePropertyPage.h Source/UI/SourcePropertyPage.cpp
git commit -m "$(cat <<'EOF'
feat(ui): flesh out SourcePropertyPage

Three-row form: read-only MIDI track info (index, name, channel,
note count); transpose semitones (numeric, -0-9 restricted); volume
% (numeric, same). Edits push back into the ConfigSource.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 7: Implement `InstrumentsTree` (tree items + selection dispatch)

**Files:**
- Modify: `Source/UI/InstrumentsTree.h`
- Modify: `Source/UI/InstrumentsTree.cpp`

The tree uses `juce::TreeViewItem` subclasses — one per node type — and the root TreeViewItem is a `SongItem`. Left-click a node → `itemSelectionChanged` fires → dispatch via the `notifySelection` callback. (Context menus come in Task 9.)

- [ ] **Step 1: Update `Source/UI/InstrumentsTree.h`**

```cpp
#pragma once

#include "Core/Config.h"
#include "Core/Song.h"

#include "PropertyPageHost.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace lotro
{

class InstrumentsTree : public juce::Component
{
public:
    InstrumentsTree (Config&     configRef,
                     const Song& rawRef,
                     std::function<void (PropertyPageHost::Kind, int, int)> onSelection,
                     std::function<void()> onMutation);

    ~InstrumentsTree() override;

    // Rebuilds the tree from scratch to match the current Config + Song.
    // Select the Song node after a rebuild.
    void rebuild();

    void resized() override;

private:
    class SongItem;
    class InstrumentItem;
    class SourceItem;

    Config&                                                config;
    const Song&                                             raw;
    std::function<void (PropertyPageHost::Kind, int, int)>  notifySelection;
    std::function<void()>                                   notifyMutation;
    juce::TreeView                                          treeView;
    std::unique_ptr<SongItem>                               rootItem;
};

} // namespace lotro
```

- [ ] **Step 2: Implement `Source/UI/InstrumentsTree.cpp`**

```cpp
#include "InstrumentsTree.h"

namespace lotro
{

namespace
{
    juce::String songLabel (const Config& config)
    {
        if (config.title.has_value() && ! config.title->empty())
            return juce::String (*config.title);
        if (! config.input.empty())
            return juce::File (juce::String (config.input)).getFileNameWithoutExtension();
        return "(no MIDI loaded)";
    }

    juce::String instrumentLabel (const ConfigInstrument& inst)
    {
        juce::String s = "X:" + juce::String (inst.x) + "  " + juce::String (inst.name);
        if (inst.label.has_value() && ! inst.label->empty())
            s += " — \"" + juce::String (*inst.label) + "\"";
        return s;
    }

    juce::String sourceLabel (const ConfigSource& src, const Song& raw)
    {
        juce::String s = "MIDI " + juce::String (src.midiTrackIndex);
        if (src.midiTrackIndex >= 0 && src.midiTrackIndex < (int) raw.tracks.size())
        {
            const auto& t = raw.tracks[(size_t) src.midiTrackIndex];
            s += ": " + juce::String (t.name)
               + " (chan " + juce::String (t.sourceMidiChannel)
               + ", " + juce::String ((int) t.notes.size()) + ")";
        }
        return s;
    }
}

// ---- SourceItem ------------------------------------------------------------

class InstrumentsTree::SourceItem : public juce::TreeViewItem
{
public:
    SourceItem (InstrumentsTree& ownerIn, int instrumentIdxIn, int sourceIdxIn)
        : owner (ownerIn), instrumentIdx (instrumentIdxIn), sourceIdx (sourceIdxIn) {}

    bool mightContainSubItems() override { return false; }
    int  getItemHeight() const override  { return 22; }

    void paintItem (juce::Graphics& g, int width, int height) override
    {
        if (isSelected()) g.fillAll (juce::Colours::lightblue);
        g.setColour (juce::Colours::black);
        g.setFont (13.0f);
        const auto& inst = owner.config.instruments[(size_t) instrumentIdx];
        const auto& src  = inst.sources[(size_t) sourceIdx];
        g.drawText (sourceLabel (src, owner.raw),
                    4, 0, width - 8, height, juce::Justification::centredLeft);
    }

    void itemSelectionChanged (bool isNowSelected) override
    {
        if (isNowSelected && owner.notifySelection)
            owner.notifySelection (PropertyPageHost::Kind::Source, instrumentIdx, sourceIdx);
    }

    int instrumentIdx;
    int sourceIdx;
private:
    InstrumentsTree& owner;
};

// ---- InstrumentItem --------------------------------------------------------

class InstrumentsTree::InstrumentItem : public juce::TreeViewItem
{
public:
    InstrumentItem (InstrumentsTree& ownerIn, int instrumentIdxIn)
        : owner (ownerIn), instrumentIdx (instrumentIdxIn) {}

    bool mightContainSubItems() override
    {
        return ! owner.config.instruments[(size_t) instrumentIdx].sources.empty();
    }
    int getItemHeight() const override { return 22; }

    void paintItem (juce::Graphics& g, int width, int height) override
    {
        if (isSelected()) g.fillAll (juce::Colours::lightblue);
        g.setColour (juce::Colours::black);
        g.setFont (13.0f);
        const auto& inst = owner.config.instruments[(size_t) instrumentIdx];
        g.drawText (instrumentLabel (inst),
                    4, 0, width - 8, height, juce::Justification::centredLeft);
    }

    void itemSelectionChanged (bool isNowSelected) override
    {
        if (isNowSelected && owner.notifySelection)
            owner.notifySelection (PropertyPageHost::Kind::Instrument, instrumentIdx, -1);
    }

    // Populate children (called by JUCE on first expand).
    void itemOpennessChanged (bool isNowOpen) override
    {
        if (isNowOpen && getNumSubItems() == 0)
        {
            const auto& inst = owner.config.instruments[(size_t) instrumentIdx];
            for (size_t s = 0; s < inst.sources.size(); ++s)
                addSubItem (new SourceItem (owner, instrumentIdx, (int) s));
        }
        else if (! isNowOpen)
        {
            clearSubItems();
        }
    }

    int instrumentIdx;
private:
    InstrumentsTree& owner;
};

// ---- SongItem --------------------------------------------------------------

class InstrumentsTree::SongItem : public juce::TreeViewItem
{
public:
    explicit SongItem (InstrumentsTree& ownerIn) : owner (ownerIn) {}

    bool mightContainSubItems() override
    {
        return ! owner.config.instruments.empty();
    }
    int getItemHeight() const override { return 22; }

    void paintItem (juce::Graphics& g, int width, int height) override
    {
        if (isSelected()) g.fillAll (juce::Colours::lightblue);
        g.setColour (juce::Colours::black);
        g.setFont (juce::Font (13.0f, juce::Font::bold));
        g.drawText (songLabel (owner.config),
                    4, 0, width - 8, height, juce::Justification::centredLeft);
    }

    void itemSelectionChanged (bool isNowSelected) override
    {
        if (isNowSelected && owner.notifySelection)
            owner.notifySelection (PropertyPageHost::Kind::Song, -1, -1);
    }

    void itemOpennessChanged (bool isNowOpen) override
    {
        if (isNowOpen && getNumSubItems() == 0)
        {
            for (size_t i = 0; i < owner.config.instruments.size(); ++i)
                addSubItem (new InstrumentItem (owner, (int) i));
        }
        else if (! isNowOpen)
        {
            clearSubItems();
        }
    }

private:
    InstrumentsTree& owner;
};

// ---- InstrumentsTree -------------------------------------------------------

InstrumentsTree::InstrumentsTree (Config& cfgRef, const Song& rawRef,
                                  std::function<void (PropertyPageHost::Kind, int, int)> onSel,
                                  std::function<void()> onMut)
    : config (cfgRef), raw (rawRef),
      notifySelection (std::move (onSel)),
      notifyMutation  (std::move (onMut))
{
    addAndMakeVisible (treeView);
    treeView.setDefaultOpenness (true);
    rebuild();
}

InstrumentsTree::~InstrumentsTree()
{
    treeView.setRootItem (nullptr);
}

void InstrumentsTree::rebuild()
{
    treeView.setRootItem (nullptr);
    rootItem = std::make_unique<SongItem> (*this);
    treeView.setRootItem (rootItem.get());
    rootItem->setOpen (true);
    rootItem->setSelected (true, true, juce::dontSendNotification);
    if (notifySelection) notifySelection (PropertyPageHost::Kind::Song, -1, -1);
}

void InstrumentsTree::resized() { treeView.setBounds (getLocalBounds()); }

} // namespace lotro
```

- [ ] **Step 3: Build and smoke-test**

```bash
cmake --build build 2>&1 | tail -5
ctest --test-dir build 2>&1 | tail -3
./run-ui.sh &
sleep 3
pkill -f converter_ui || true
```

Expected: clean build; `134/134`; the UI opens with a tree visible (empty until a MIDI is loaded). Opening a MIDI should now populate the tree with Song → Instrument → Source levels. Left-clicking nodes should change the property page visible in the lower region.

- [ ] **Step 4: Commit**

```bash
git add Source/UI/InstrumentsTree.h Source/UI/InstrumentsTree.cpp
git commit -m "$(cat <<'EOF'
feat(ui): InstrumentsTree with three-level tree items

Three TreeViewItem subclasses — SongItem (root, shows title or input
filename), InstrumentItem (shows X:N  Name — "Label"), SourceItem
(shows MIDI index + track info). Left-click a node fires the
selection callback with (Kind, instrumentIdx, sourceIdx) so
PropertyPageHost can swap the visible form.

Context menus come in a later task; this one just gets the visual
tree + selection dispatch working.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 8: Implement `PropertyPageHost` tightening (verify visuals match spec)

**Files:**
- Modify: `Source/UI/PropertyPageHost.cpp` (only if refresh/layout needs tuning after tasks 4–7)

The stub PropertyPageHost from Task 2 is already functional: `showFor` sets visibility, `refresh` calls each page. Now that real pages exist, verify:

- [ ] **Step 1: Verify runtime behaviour**

```bash
./run-ui.sh &
sleep 2
# Manually: load a MIDI, click Song node, check Song page appears with globals.
# Click an Instrument, check Instrument page appears. Click a Source, check
# Source page appears. All three pages should be visually in the same panel
# area, only one visible at a time.
pkill -f converter_ui || true
```

- [ ] **Step 2: If there are issues, adjust `PropertyPageHost::showFor` or `::refresh`**

No changes anticipated — the stub should work. This task is primarily a verification step.

- [ ] **Step 3: Commit nothing if no changes; otherwise a small "fix(ui)" commit**

If you had to adjust anything:

```bash
git add Source/UI/PropertyPageHost.cpp
git commit -m "fix(ui): PropertyPageHost refresh/layout tweak after real pages landed"
```

Otherwise skip. This task will often be a no-op.

---

### Task 9: Context menus + mutations + tree-refresh + selection persistence

**Files:**
- Modify: `Source/UI/InstrumentsTree.h`
- Modify: `Source/UI/InstrumentsTree.cpp`

Wire the right-click context menus per spec (Add Instrument on Song, Add Source + Delete Instrument on Instrument, Delete Source on Source) and hook mutations to a tree rebuild + selection persistence.

- [ ] **Step 1: Add context-menu handlers to each TreeViewItem**

In `Source/UI/InstrumentsTree.cpp`, add an `itemClicked` override to each of the three classes. In each, handle right-click (`event.mods.isRightButtonDown()`) by popping an appropriate `juce::PopupMenu`.

Replace the class bodies of `SourceItem`, `InstrumentItem`, `SongItem` as follows. (Add these methods alongside the existing ones; don't remove `paintItem`, `itemSelectionChanged`, `itemOpennessChanged`, `mightContainSubItems`, `getItemHeight`.)

For `SourceItem`:

```cpp
    void itemClicked (const juce::MouseEvent& e) override
    {
        if (! e.mods.isRightButtonDown()) return;

        juce::PopupMenu m;
        m.addItem (1, "Delete Source");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (owner),
            [this] (int result)
            {
                if (result == 1) deleteSelf();
            });
    }

    void deleteSelf()
    {
        auto& inst = owner.config.instruments[(size_t) instrumentIdx];
        if (sourceIdx >= 0 && sourceIdx < (int) inst.sources.size())
            inst.sources.erase (inst.sources.begin() + sourceIdx);
        if (owner.notifyMutation) owner.notifyMutation();
        owner.rebuild();
    }
```

For `InstrumentItem`:

```cpp
    void itemClicked (const juce::MouseEvent& e) override
    {
        if (! e.mods.isRightButtonDown()) return;

        juce::PopupMenu m;
        juce::PopupMenu addSub;
        const auto& inst = owner.config.instruments[(size_t) instrumentIdx];
        std::set<int> already;
        for (const auto& s : inst.sources) already.insert (s.midiTrackIndex);

        int anyAvailable = 0;
        for (size_t i = 0; i < owner.raw.tracks.size(); ++i)
        {
            if (already.count ((int) i) > 0) continue;
            const auto& t = owner.raw.tracks[i];
            juce::String label = "MIDI " + juce::String ((int) i) + ": "
                               + juce::String (t.name)
                               + " (chan " + juce::String (t.sourceMidiChannel)
                               + ", " + juce::String ((int) t.notes.size()) + ")";
            addSub.addItem (100 + (int) i, label);
            ++anyAvailable;
        }
        if (anyAvailable == 0)
            addSub.addItem (-1, "(no unused MIDI tracks)", false);

        m.addSubMenu ("Add Source", addSub);
        m.addSeparator();
        m.addItem (2, "Delete Instrument");

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (owner),
            [this] (int result)
            {
                if (result >= 100)
                    addSourceForMidi (result - 100);
                else if (result == 2)
                    deleteSelf();
            });
    }

    void addSourceForMidi (int midiIndex)
    {
        auto& inst = owner.config.instruments[(size_t) instrumentIdx];
        inst.sources.push_back (ConfigSource{ midiIndex, 0, 0 });
        if (owner.notifyMutation) owner.notifyMutation();
        owner.rebuild();
    }

    void deleteSelf()
    {
        if (instrumentIdx >= 0 && instrumentIdx < (int) owner.config.instruments.size())
            owner.config.instruments.erase (owner.config.instruments.begin() + instrumentIdx);
        if (owner.notifyMutation) owner.notifyMutation();
        owner.rebuild();
    }
```

For `SongItem`:

```cpp
    void itemClicked (const juce::MouseEvent& e) override
    {
        if (! e.mods.isRightButtonDown()) return;

        juce::PopupMenu m;
        m.addItem (1, "Add Instrument");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (owner),
            [this] (int result)
            {
                if (result == 1) addInstrument();
            });
    }

    void addInstrument()
    {
        ConfigInstrument fresh;
        int nextX = 1;
        for (const auto& inst : owner.config.instruments)
            nextX = std::max (nextX, inst.x + 1);
        fresh.x    = nextX;
        fresh.name = "LuteOfAges";
        owner.config.instruments.push_back (fresh);
        if (owner.notifyMutation) owner.notifyMutation();
        owner.rebuild();
    }
```

Add `#include <set>` at the top of `InstrumentsTree.cpp` for the `std::set<int>` use.

- [ ] **Step 2: Build + smoke test**

```bash
cmake --build build 2>&1 | tail -5
./run-ui.sh &
sleep 2
# Manual test: right-click Song → Add Instrument appears in menu. Click it.
# A new X:1 LuteOfAges instrument appears in tree, is selected, its page shown.
# Right-click that instrument → Add Source submenu lists all MIDI tracks.
# Pick one; it appears as a child. Right-click it → Delete Source; gone.
# Right-click the instrument → Delete Instrument; gone.
pkill -f converter_ui || true
```

- [ ] **Step 3: Commit**

```bash
git add Source/UI/InstrumentsTree.cpp
git commit -m "$(cat <<'EOF'
feat(ui): context menus for tree (Add / Delete per node type)

Right-click handlers on SongItem (Add Instrument), InstrumentItem
(Add Source submenu listing unused MIDI tracks; Delete Instrument),
and SourceItem (Delete Source). Each mutation updates the Config
in-memory, fires the mutation callback (so EditorPane can
re-propagate onConfigChanged), and rebuilds the tree from scratch.

After a rebuild, the Song root is selected and expanded — simplest
selection-persistence policy.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Phase D — Documentation

### Task 10: Rewrite `docs/UI_GUIDE.md` for the treeview

**Files:**
- Modify: `docs/UI_GUIDE.md`

- [ ] **Step 1: Replace the Editor pane section**

In `docs/UI_GUIDE.md`, the "Layout overview" diagram and the "Naming reference" table reference the deprecated classes (GlobalSettingsView, InstrumentsTable, InstrumentDetailForm). Rewrite those sections to describe the new treeview+property-pages model.

Replace the "Layout overview" ASCII art with:

```
┌──────────────────────────────────────────────────────────────────────┐
│ Title bar (JUCE-drawn; "LOTRO ABC Converter UI")                    │
├──────────────────────────────────────────────────────────────────────┤
│ Menu bar              [File ▾]                                       │  ← MENU BAR (24 px)
├─────────────────────────────────────┬────────────────────────────────┤
│                                     │                                │
│  EDITOR PANE                        │  DIAGNOSTICS PANE              │
│                                     │                                │
│  ┌─ InstrumentsTree ─────────────┐  │  ┌─ DiagnosticListView ───┐   │
│  │  📄 Song title                  │  │  │                         │   │
│  │  ├─ 🎵 X:1 LuteOfAges  Lead     │  │  └─────────────────────────┘   │
│  │  │   ├─ 🎹 MIDI 0: Drums…       │  │  ──── INNER SPLITTER ────     │
│  │  │   └─ 🎹 MIDI 2: Guitar…      │  │  ┌─ AbcPreviewView ───────┐   │
│  │  └─ 🎵 X:2 Theorbo              │  │  │  (ABC text)             │   │
│  │      └─ 🎹 MIDI 1: Bass…        │  │  │                         │   │
│  └─────────────────────────────────┘  │  └─────────────────────────┘   │
│  ┌─ PropertyPageHost ────────────┐  │                                │
│  │   (shows Song / Instrument /    │  │                                │
│  │    Source page based on         │  │                                │
│  │    tree selection)              │  │                                │
│  └─────────────────────────────────┘  │                                │
│                                     │                                │
│  [ Run Converter ]                  │                                │
│                                     │                                │
└─────────────────────────────────────┴────────────────────────────────┘
                                     ↑
                            OUTER SPLITTER
```

Replace the "Naming reference" rows that mention the deleted classes with:

| Name                                | Code class / file                                            |
|-------------------------------------|--------------------------------------------------------------|
| **InstrumentsTree**                 | `Source/UI/InstrumentsTree.{h,cpp}` — the treeview itself    |
| **SongItem / InstrumentItem / SourceItem** | Private inner classes of `InstrumentsTree`          |
| **Song node**                       | The root `SongItem`; always one of them                      |
| **Instrument node**                 | An `InstrumentItem` child of the Song node                   |
| **Source node**                     | A `SourceItem` child of an Instrument node                   |
| **PropertyPageHost**                | `Source/UI/PropertyPageHost.{h,cpp}` — the page switcher      |
| **Song property page**              | `SongPropertyPage` — shown when the Song node is selected    |
| **Instrument property page**        | `InstrumentPropertyPage` — shown for Instrument selection    |
| **Source property page**            | `SourcePropertyPage` — shown for Source selection            |

Replace the "Field reference (Editor pane)" section with three subsections — one per property page — showing the fields and which Config path they bind to. Use this content:

```markdown
## Field reference (Editor pane)

### Song property page (root selected)

| Field             | Control        | Config path        |
|-------------------|----------------|--------------------|
| Input MIDI        | read-only      | `Config::input`    |
| Output ABC        | read-only      | `Config::output`   |
| Title             | text           | `Config::title`    |
| Transcriber       | text           | `Config::transcriber` |
| Tempo (BPM)       | numeric        | `Config::tempo`    |
| Global transpose  | numeric        | `Config::transpose` |

### Instrument property page (Instrument selected)

| Field         | Control          | Config path                                |
|---------------|------------------|--------------------------------------------|
| X: index      | numeric          | `ConfigInstrument::x`                      |
| Name          | dropdown         | `ConfigInstrument::name`                   |
| Label         | text             | `ConfigInstrument::label`                  |
| Drum map      | text + Browse    | `ConfigInstrument::drumMap` (Drums only)   |

### Source property page (Source selected)

| Field                  | Control          | Source path                                     |
|------------------------|------------------|-------------------------------------------------|
| MIDI track (read-only) | label            | `ConfigSource::midiTrackIndex` + `Song.tracks[]` |
| Transpose semitones    | numeric          | `ConfigSource::transposeSemitones`               |
| Volume %               | numeric          | `ConfigSource::volumePercent`                    |
```

Add a "Context menus" section:

```markdown
## Context menus

Right-click a tree node for the actions available to it. Left-click always
just selects the node (which swaps the property page).

| Node        | Right-click menu                                           |
|-------------|------------------------------------------------------------|
| Song        | `Add Instrument`                                           |
| Instrument  | `Add Source ▸` (submenu of unused MIDI tracks); `Delete Instrument` |
| Source      | `Delete Source`                                            |
```

- [ ] **Step 2: Commit**

```bash
git add docs/UI_GUIDE.md
git commit -m "$(cat <<'EOF'
docs(ui): rewrite UI_GUIDE for the treeview editor

Replaces references to the deleted GlobalSettingsView, InstrumentsTable,
and InstrumentDetailForm with the new InstrumentsTree,
PropertyPageHost, and the three property pages. Adds a Context menus
section describing the right-click actions per node type.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 11: Update `CLAUDE.md` source layout and test count

**Files:**
- Modify: `CLAUDE.md`

- [ ] **Step 1: Update the UI/ section in the Source layout tree**

In `CLAUDE.md`, find the `### Source layout` block. Replace the `├── UI/` sub-tree with:

```
├── UI/                          JUCE GUI app — converter_ui binary.
│   ├── UiMain.cpp               JUCE app entry point
│   ├── MainWindow.{h,cpp}       Window, menus, splitter, drag-drop
│   ├── EditorPane.{h,cpp}       Left pane: tree + property page host + Run
│   ├── InstrumentsTree.{h,cpp}  Three-level treeview (Song/Instrument/Source)
│   ├── PropertyPageHost.{h,cpp} Swaps the visible property page on selection
│   ├── SongPropertyPage.{h,cpp}      Song-node property page
│   ├── InstrumentPropertyPage.{h,cpp} Instrument-node property page
│   ├── SourcePropertyPage.{h,cpp}    Source-node property page
│   ├── DiagnosticsPane.{h,cpp}       Right pane container
│   ├── DiagnosticListView.{h,cpp}    Diagnostic table
│   └── AbcPreviewView.{h,cpp}        Read-only ABC text + status line
```

- [ ] **Step 2: Bump the test count**

In `## Testing notes`, change `Test count: **127/127**` (or whatever the previous number was) to match the new count. Run locally to verify:

```bash
ctest --test-dir build 2>&1 | tail -3
```

Expected: `134/134` (or whatever the new total is; use the actual number from ctest output).

- [ ] **Step 3: Commit**

```bash
git add CLAUDE.md
git commit -m "$(cat <<'EOF'
docs: sync CLAUDE.md with treeview redesign

Updates the Source layout UI/ sub-tree to list the new classes
(InstrumentsTree, PropertyPageHost, three property pages) and drops
the deleted ones. Bumps the test count.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Self-review notes

**Spec coverage:**

- Data model (`ConfigSource`, reshaped `ConfigInstrument`, drop of instrument-level knobs) → Task 1.
- Assembly math with per-source fields → Task 1.
- File-format changes + backward-compat shorthand + migration warning → Task 1 (all three loaders + writers).
- Tree structure (Song → Instrument → Source) → Tasks 2, 7.
- Left-click selects + Property page swaps → Tasks 2 (stubs), 7 (real tree + dispatch), 8 (verify).
- Property pages for each node type → Tasks 4, 5, 6.
- Context menus (Add Instrument / Add Source / Delete Instrument / Delete Source) → Task 9.
- Validator additions (midiTrackIndex range + unique-within-instrument) → Task 1.
- Deletion of GlobalSettingsView / InstrumentsTable / InstrumentDetailForm → Task 3.
- `EditorPane` rewire → Task 3 (stub wire-up) + Task 7 (refreshed once the tree is real).
- CMake + file-list updates → Tasks 2, 3.
- `docs/UI_GUIDE.md` rewrite → Task 10.
- `CLAUDE.md` source-layout + test-count sync → Task 11.

**Placeholder scan:** No "TBD" / "TODO" / incomplete-implementation in any step. Every code block is directly applicable.

**Type consistency:** `ConfigSource` fields `midiTrackIndex`, `transposeSemitones`, `volumePercent` are used consistently across Task 1 (data model), Tasks 4-6 (property pages), Task 7 (tree labels), Task 9 (context-menu mutations). `PropertyPageHost::Kind` enum values `None / Song / Instrument / Source` are defined in Task 2 and used identically in Tasks 7 and 9. The `onSelection` callback signature `void (PropertyPageHost::Kind, int, int)` is defined in Task 2 and matches every call site.

**Risk callouts:**

- **Task 1 is unusually large** (15+ files in one commit). The spec reviewer should verify the build and test suite are both green after; if any individual loader/writer format has bugs, they'll be caught by the round-trip tests within the same commit.
- **Tree context menus dispatch via `MouseEvent::mods.isRightButtonDown()`** inside `itemClicked`. JUCE's `TreeViewItem::itemClicked` is called for both left and right clicks; we gate on the modifier. If a platform-specific quirk makes right-click fire a different handler (unlikely on Linux/WSL/X11 with JUCE's event loop), the menus won't appear — fall back to `TreeViewItem::mouseDown` override if needed.
- **Tree rebuild on mutation** clears and reselects Song. Users performing rapid add/delete might find this mildly disorienting (they always get kicked back to the root). An optional polish is trying to re-select the same-index node; deferred per the spec's out-of-scope list.
- **The `InstrumentPropertyPage::xField` input restriction** is digits-only (no minus sign), matching the validator's rule that `x >= 1`. The validator provides the final safety net regardless.

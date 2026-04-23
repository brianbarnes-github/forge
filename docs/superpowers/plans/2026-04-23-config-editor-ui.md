# Config-editor UI (`converter_ui`) implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a JUCE-GUI binary `converter_ui` that lets a tester drop a MIDI, edit the auto-generated Config in a master-detail editor, run the conversion in-process, and see structured Diagnostics + the generated ABC text in a side pane. Configs save in JSON, TOML, or XML.

**Architecture:** A new GUI executable next to the existing `converter` and `converter_tests`, all linking the same `converter_core` static library. Two enabling refactors land first: (a) extract `synthesiseConfig` and `runPipeline` from `Source/Main.cpp` into a new `Source/Core/Pipeline.{h,cpp}` so both binaries call the same code, and (b) add a `ConfigWriter` next to the existing `ConfigLoader` so configs can be saved in all three formats. The UI itself is a single window with a draggable splitter, editor on the left (master-detail over the Config), diagnostics + ABC preview on the right.

**Tech Stack:** C++17, JUCE (`juce_gui_basics`, `juce_gui_extra` for the GUI binary; `juce_core` everywhere; `juce_audio_basics`, `juce_audio_formats` already in Core), Catch2, CMake + Ninja. The vendored `toml++` header is reused for TOML writing.

**Spec reference:** `docs/superpowers/specs/2026-04-23-config-editor-ui-design.md`

## Workspace setup

Before executing this plan, the implementer should create a worktree:

```bash
cd /home/brian/sandbox/converter-cli
git worktree add .worktrees/config-editor-ui -b feat/config-editor-ui
cd .worktrees/config-editor-ui
git submodule update --init --recursive
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build
```

Expected baseline: `121/121` tests passing.

All paths in this plan are relative to the worktree root (`/home/brian/sandbox/converter-cli/.worktrees/config-editor-ui`).

## File structure

### New files (in execution order)

| Path | Purpose |
|---|---|
| `Source/Core/Pipeline.h` | Declares extracted `synthesiseConfig` + `runPipeline` |
| `Source/Core/Pipeline.cpp` | Implementations lifted from `Main.cpp` |
| `Tests/Pipeline_tests.cpp` | Pin-test for `synthesiseConfig` post-extraction |
| `Source/Core/ConfigWriter.h` | Declares `writeConfigToFile` / `writeConfigToString` for the three formats |
| `Source/Core/ConfigWriter.cpp` | JSON / TOML / XML writers |
| `Tests/ConfigWriter_tests.cpp` | Round-trip tests per format |
| `Source/UI/UiMain.cpp` | JUCE app entry point |
| `Source/UI/MainWindow.h` / `.cpp` | Top-level window, menu bar, splitter, drag-drop target |
| `Source/UI/EditorPane.h` / `.cpp` | Left pane container; owns the in-memory `Config` and raw `Song` |
| `Source/UI/GlobalSettingsView.h` / `.cpp` | Top-of-editor form for top-level Config fields |
| `Source/UI/InstrumentsTable.h` / `.cpp` | `juce::TableListBoxModel` for the instruments list |
| `Source/UI/InstrumentDetailForm.h` / `.cpp` | Detail form for the selected instrument |
| `Source/UI/DiagnosticsPane.h` / `.cpp` | Right pane container |
| `Source/UI/DiagnosticListView.h` / `.cpp` | `juce::TableListBox` of `Diagnostic` rows |
| `Source/UI/AbcPreviewView.h` / `.cpp` | Read-only `juce::TextEditor` for ABC + status line |

### Modified files

| Path | Change |
|---|---|
| `Source/Main.cpp` | Use `Source/Core/Pipeline.h`; remove duplicated `synthesiseConfig` + `runPipeline` definitions |
| `CMakeLists.txt` | Add new Core sources; add `juce_add_gui_app(converter_ui …)` target wiring |
| `Tests/CMakeLists.txt` | Register `Pipeline_tests.cpp` and `ConfigWriter_tests.cpp` |
| `CLAUDE.md` | Add a paragraph describing the new UI binary and its build dependencies |

---

## Phase A — Foundations (TDD-friendly)

### Task 1: Extract `synthesiseConfig` and `runPipeline` into Core

**Files:**
- Create: `Source/Core/Pipeline.h`
- Create: `Source/Core/Pipeline.cpp`
- Create: `Tests/Pipeline_tests.cpp`
- Modify: `Source/Main.cpp`
- Modify: `CMakeLists.txt`
- Modify: `Tests/CMakeLists.txt`

The current `Source/Main.cpp` defines both helpers in its anonymous namespace. The UI needs both, so they move to Core. `synthesiseConfig`'s signature loses its dependency on `lotro::CliOptions` so the UI doesn't have to include CLI headers; the CLI passes the same data via the new parameter list.

- [ ] **Step 1: Read the current Main.cpp to understand the existing helpers**

```bash
sed -n '/synthesiseConfig\|runPipeline/,/^    }$/p' Source/Main.cpp | head -80
```

Confirm the bodies of `synthesiseConfig (CliOptions, Song)` and `runPipeline (Song&, Diagnostics&)`.

- [ ] **Step 2: Write the failing pin-test**

Create `Tests/Pipeline_tests.cpp`:

```cpp
#include "Core/Pipeline.h"
#include "Core/Song.h"
#include "Core/LotroInstrument.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    lotro::Note note (int pitch, int startTick, int dur, int vel = 100)
    {
        lotro::Note n;
        n.pitch = pitch; n.startTick = startTick;
        n.durationTicks = dur; n.velocity = vel;
        return n;
    }

    lotro::Song threeTrackRaw()
    {
        lotro::Song s;
        s.ticksPerQuarter = 480;
        s.tempoMap.push_back ({ 0, 120.0 });
        s.meterMap.push_back ({ 0, 4, 4 });

        for (int i = 0; i < 3; ++i)
        {
            lotro::Track t;
            t.name = "MidiTrack" + std::to_string (i);
            t.notes.push_back (note (60 + i, 0, 480));
            s.tracks.push_back (t);
        }
        return s;
    }
}

TEST_CASE ("pipeline: synthesiseConfig produces one instrument per raw track", "[pipeline][synth]")
{
    const auto raw = threeTrackRaw();
    const auto cfg = lotro::synthesiseConfig (raw, "in.mid", "out.abc",
                                              std::nullopt, 0, {});
    CHECK (cfg.input  == "in.mid");
    CHECK (cfg.output == std::optional<std::string>{ "out.abc" });
    CHECK (cfg.transpose == 0);
    REQUIRE (cfg.instruments.size() == 3);
    for (int i = 0; i < 3; ++i)
    {
        CHECK (cfg.instruments[(size_t) i].x       == i + 1);
        CHECK (cfg.instruments[(size_t) i].sources == std::vector<int>{ i });
        // Auto-pick will land on something — assert it's a valid LOTRO name.
        lotro::LotroInstrument parsed;
        CHECK (lotro::parseName (cfg.instruments[(size_t) i].name, parsed).empty());
    }
}

TEST_CASE ("pipeline: synthesiseConfig honours instrumentOverrides", "[pipeline][synth]")
{
    const auto raw = threeTrackRaw();
    std::map<int, lotro::LotroInstrument> overrides;
    overrides[1] = lotro::LotroInstrument::Theorbo;
    const auto cfg = lotro::synthesiseConfig (raw, "in.mid", "",
                                              std::nullopt, 0, overrides);
    REQUIRE (cfg.instruments.size() == 3);
    CHECK (cfg.instruments[1].name == "Theorbo");
}

TEST_CASE ("pipeline: synthesiseConfig propagates tempo and transpose", "[pipeline][synth]")
{
    const auto raw = threeTrackRaw();
    const auto cfg = lotro::synthesiseConfig (raw, "in.mid", "",
                                              std::optional<double>{ 140.0 }, -5, {});
    CHECK (cfg.tempo     == std::optional<double>{ 140.0 });
    CHECK (cfg.transpose == -5);
}
```

- [ ] **Step 3: Register `Pipeline_tests.cpp` in `Tests/CMakeLists.txt`**

Add (alphabetically, after `MidiImporter_tests.cpp`):

```cmake
    Pipeline_tests.cpp
```

- [ ] **Step 4: Verify the test fails to link**

```bash
cmake --build build 2>&1 | tail -10
```

Expected: link errors mentioning `lotro::synthesiseConfig`.

- [ ] **Step 5: Write `Source/Core/Pipeline.h`**

```cpp
#pragma once

#include "Config.h"
#include "Diagnostics.h"
#include "LotroInstrument.h"
#include "Song.h"

#include <map>
#include <optional>
#include <string>

namespace lotro
{

// Builds a one-instrument-per-MIDI-track Config from a freshly imported Song.
// Used by the no-config CLI path and by the UI when a MIDI is loaded without
// an existing config. Auto-picks each instrument with pickInstrumentForTrack
// unless an override is provided in `instrumentOverrides` (keyed by raw MIDI
// track index).
Config synthesiseConfig (const Song&                                  raw,
                         const std::string&                            inputPath,
                         const std::string&                            outputPath,
                         std::optional<double>                         tempo,
                         int                                           transpose,
                         const std::map<int, LotroInstrument>&         instrumentOverrides);

// Runs the per-track constraint pipeline (Range → Chord → Duration → Tempo →
// Collision → Dynamic) on every enabled track of `song`, then calls
// applyTempoCollapseToSongMaps once on the song. Diagnostics get the
// per-track index back-filled where the constraint pass left it as -1.
void runPipeline (Song& song, Diagnostics& diagnostics);

} // namespace lotro
```

- [ ] **Step 6: Write `Source/Core/Pipeline.cpp`**

```cpp
#include "Pipeline.h"

#include "AutoInstrument.h"
#include "Constraints/ChordConstraint.h"
#include "Constraints/CollisionGuard.h"
#include "Constraints/DurationConstraint.h"
#include "Constraints/DynamicMapper.h"
#include "Constraints/RangeConstraint.h"
#include "Constraints/TempoCollapse.h"

namespace lotro
{

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
        inst.sources = { (int) i };

        const auto picked = pickInstrumentForTrack (raw.tracks[i]);
        inst.name = std::string (displayName (picked));

        const auto overrideIt = instrumentOverrides.find ((int) i);
        if (overrideIt != instrumentOverrides.end())
            inst.name = std::string (displayName (overrideIt->second));

        cfg.instruments.push_back (inst);
    }
    return cfg;
}

void runPipeline (Song& song, Diagnostics& diagnostics)
{
    for (size_t trackIdx = 0; trackIdx < song.tracks.size(); ++trackIdx)
    {
        auto& track = song.tracks[trackIdx];
        if (! track.enabled) continue;

        const size_t before = diagnostics.size();
        applyRangeConstraint    (track, diagnostics);
        applyChordConstraint    (track, diagnostics);
        applyDurationConstraint (track, song, diagnostics);
        applyTempoCollapse      (track, song, diagnostics);
        applyCollisionGuard     (track, diagnostics);
        applyDynamicMapper      (track, diagnostics);

        for (size_t i = before; i < diagnostics.size(); ++i)
            if (diagnostics[i].trackIndex < 0)
                diagnostics[i].trackIndex = (int) trackIdx;
    }

    applyTempoCollapseToSongMaps (song);
}

} // namespace lotro
```

- [ ] **Step 7: Register `Source/Core/Pipeline.cpp` in `CMakeLists.txt`**

In `add_library(converter_core STATIC ...)`, add (alphabetically, after `MidiImporter.cpp` and before `Constraints/...`):

```cmake
    Source/Core/Pipeline.cpp
```

- [ ] **Step 8: Update `Source/Main.cpp` to call the extracted helpers**

Open `Source/Main.cpp`. Add at the top (alongside the other Core includes):

```cpp
#include "Core/Pipeline.h"
```

Find and DELETE the inline `synthesiseConfig` definition inside the anonymous namespace. Find and DELETE the inline `runPipeline` definition inside the anonymous namespace.

Find the call site that currently builds the synthesised config (in the `else` branch where no `--config` was given):

```cpp
        else
        {
            cfg = synthesiseConfig (opts, song);
        }
```

Replace with:

```cpp
        else
        {
            cfg = lotro::synthesiseConfig (
                song,
                opts.inputFile.getFullPathName().toStdString(),
                opts.outputFile != juce::File()
                    ? opts.outputFile.getFullPathName().toStdString()
                    : std::string{},
                opts.tempoOverride,
                opts.transposeSemitones,
                opts.instrumentOverrides);
        }
```

Find the call to `runPipeline (assembled, diagnostics);` (the in-namespace one) and change it to `lotro::runPipeline (assembled, diagnostics);`.

- [ ] **Step 9: Build and run the test plus the existing suite**

```bash
cmake --build build 2>&1 | tail -10
ctest --test-dir build 2>&1 | tail -3
```

Expected: clean build, **124/124** passing (121 existing + 3 new in `Pipeline_tests.cpp`). The existing `EndToEnd_tests.cpp` and `EndToEndConfig_tests.cpp` are the safety net for the no-config behaviour — those must still pass.

- [ ] **Step 10: Commit**

```bash
git add Source/Core/Pipeline.h Source/Core/Pipeline.cpp \
        Source/Main.cpp \
        Tests/Pipeline_tests.cpp \
        CMakeLists.txt Tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
refactor(core): extract synthesiseConfig and runPipeline into Pipeline.h

Both helpers were inline in Source/Main.cpp. The new converter_ui
binary needs them too, so they move to Source/Core/Pipeline.{h,cpp}.
synthesiseConfig's signature loses its CliOptions dependency in favour
of a parameter list of the data fields it actually needs — the CLI
adapts in one expression at the call site. Pin-tested by the new
Pipeline_tests.cpp; existing EndToEnd suites verify no-config CLI
behaviour is bit-for-bit unchanged.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: ConfigWriter — JSON

**Files:**
- Create: `Source/Core/ConfigWriter.h`
- Create: `Source/Core/ConfigWriter.cpp`
- Create: `Tests/ConfigWriter_tests.cpp`
- Modify: `CMakeLists.txt`
- Modify: `Tests/CMakeLists.txt`

- [ ] **Step 1: Write `Source/Core/ConfigWriter.h`**

```cpp
#pragma once

#include "Config.h"
#include "ConfigLoader.h"     // for ConfigFormat enum

#include <string>
#include <string_view>

namespace lotro
{

// Serialises a Config to a string in the requested format. ConfigFormat::Auto
// is treated as Json. Returns empty error string on success; on failure,
// `out` is left in an unspecified state and the returned string describes
// the error.
std::string writeConfigToString (ConfigFormat       format,
                                 const Config&      config,
                                 std::string&       out);

// Convenience: writes to file. Same return contract as writeConfigToString.
std::string writeConfigToFile (const std::string& path,
                               ConfigFormat       format,
                               const Config&      config);

} // namespace lotro
```

- [ ] **Step 2: Write the failing JSON round-trip test**

Create `Tests/ConfigWriter_tests.cpp`:

```cpp
#include "Core/ConfigWriter.h"
#include "Core/ConfigLoader.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
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
            i.x                  = 1;
            i.name               = "LuteOfAges";
            i.label              = std::string ("Lead");
            i.sources            = { 0, 2 };
            i.transposeSemitones = -12;
            i.volumePercent      = 110;
            c.instruments.push_back (i);
        }
        {
            lotro::ConfigInstrument i;
            i.x       = 3;
            i.name    = "Drums";
            i.sources = { 9 };
            i.drumMap = std::string ("kit.json");
            c.instruments.push_back (i);
        }
        return c;
    }

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
            CHECK (ai.x                  == bi.x);
            CHECK (ai.name               == bi.name);
            CHECK (ai.label              == bi.label);
            CHECK (ai.sources            == bi.sources);
            CHECK (ai.transposeSemitones == bi.transposeSemitones);
            CHECK (ai.volumePercent      == bi.volumePercent);
            CHECK (ai.drumMap            == bi.drumMap);
        }
    }
}

TEST_CASE ("config-writer: JSON round-trip preserves all fields", "[config-writer][json]")
{
    const auto original = richConfig();
    std::string text;
    REQUIRE (lotro::writeConfigToString (lotro::ConfigFormat::Json, original, text).empty());

    lotro::Config parsed;
    REQUIRE (lotro::loadConfigFromString (text, lotro::ConfigFormat::Json, parsed).empty());

    checkConfigsEqual (parsed, original);
}
```

- [ ] **Step 3: Register the new files**

In `CMakeLists.txt`, inside `add_library(converter_core STATIC ...)`, add (alphabetical, between `ConfigLoader.cpp` and `DrumMap.cpp` — should be `ConfigWriter.cpp` after `ConfigLoader.cpp`):

```cmake
    Source/Core/ConfigWriter.cpp
```

In `Tests/CMakeLists.txt`, add (alphabetical, after `ConfigPipeline_tests.cpp`):

```cmake
    ConfigWriter_tests.cpp
```

- [ ] **Step 4: Verify link failure**

```bash
cmake --build build 2>&1 | tail -10
```

Expected: link errors about `writeConfigToString` / `writeConfigToFile`.

- [ ] **Step 5: Implement the JSON writer (and stubs for TOML/XML)**

Create `Source/Core/ConfigWriter.cpp`:

```cpp
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
```

- [ ] **Step 6: Build and run the JSON test**

```bash
cmake --build build 2>&1 | tail -5
ctest --test-dir build -R "config-writer.*json" 2>&1 | tail -10
```

Expected: 1 JSON test passing.

- [ ] **Step 7: Full regression check**

```bash
ctest --test-dir build 2>&1 | tail -3
```

Expected: **125/125** (124 from Task 1 + 1 new JSON round-trip).

- [ ] **Step 8: Commit**

```bash
git add Source/Core/ConfigWriter.h Source/Core/ConfigWriter.cpp \
        Tests/ConfigWriter_tests.cpp \
        CMakeLists.txt Tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(config): JSON writer for Config (round-trip with loader)

ConfigWriter.h declares writeConfigToFile and writeConfigToString.
The JSON implementation builds a juce::DynamicObject and uses
juce::JSON::toString. TOML and XML branches return descriptive
"not yet implemented" errors and will be filled in by the next
two tasks. Round-trip test pins the field-for-field invariant.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: ConfigWriter — TOML

**Files:**
- Modify: `Source/Core/ConfigWriter.cpp`
- Modify: `Tests/ConfigWriter_tests.cpp`

- [ ] **Step 1: Write the failing TOML round-trip test**

Open `Tests/ConfigWriter_tests.cpp` and append (after the JSON test):

```cpp
TEST_CASE ("config-writer: TOML round-trip preserves all fields", "[config-writer][toml]")
{
    const auto original = richConfig();
    std::string text;
    REQUIRE (lotro::writeConfigToString (lotro::ConfigFormat::Toml, original, text).empty());

    lotro::Config parsed;
    REQUIRE (lotro::loadConfigFromString (text, lotro::ConfigFormat::Toml, parsed).empty());

    checkConfigsEqual (parsed, original);
}
```

- [ ] **Step 2: Verify the test fails**

```bash
cmake --build build && ctest --test-dir build -R "config-writer.*toml" 2>&1 | tail -10
```

Expected: failure with "TOML writer not yet implemented".

- [ ] **Step 3: Implement the TOML writer**

Open `Source/Core/ConfigWriter.cpp`. Add this helper at the top of the anonymous namespace (after `toJsonVar`):

```cpp
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
        if (cfg.output.has_value())      s += "output = "      + escapeToml (*cfg.output)      + "\n";
        if (cfg.title.has_value())       s += "title = "       + escapeToml (*cfg.title)       + "\n";
        if (cfg.transcriber.has_value()) s += "transcriber = " + escapeToml (*cfg.transcriber) + "\n";
        if (cfg.tempo.has_value())       s += "tempo = "       + std::to_string ((int) std::lround (*cfg.tempo)) + "\n";
        if (cfg.transpose != 0)          s += "transpose = "   + std::to_string (cfg.transpose) + "\n";

        for (const auto& inst : cfg.instruments)
        {
            s += "\n[[instruments]]\n";
            s += "x = "    + std::to_string (inst.x) + "\n";
            s += "name = " + escapeToml (inst.name) + "\n";
            if (inst.label.has_value())
                s += "label = " + escapeToml (*inst.label) + "\n";

            s += "sources = [";
            for (size_t i = 0; i < inst.sources.size(); ++i)
            {
                if (i > 0) s += ", ";
                s += std::to_string (inst.sources[i]);
            }
            s += "]\n";

            if (inst.transposeSemitones != 0)
                s += "transposeSemitones = " + std::to_string (inst.transposeSemitones) + "\n";
            if (inst.volumePercent != 100)
                s += "volumePercent = " + std::to_string (inst.volumePercent) + "\n";
            if (inst.drumMap.has_value())
                s += "drumMap = " + escapeToml (*inst.drumMap) + "\n";
        }

        out = std::move (s);
        return {};
    }
```

Note: `tempo` is written as an integer because the loader's TOML parser path uses `int64_t` for whole numbers (the loader code falls back to `value<double>()` which also accepts integer values). Writing `140` and reading it back as `140.0` round-trips through the `std::optional<double>` field exactly.

Update the dispatcher in `writeConfigToString`:

```cpp
        case ConfigFormat::Toml: return writeToml (cfg, out);
```

- [ ] **Step 4: Build and run the TOML test**

```bash
cmake --build build && ctest --test-dir build -R "config-writer.*toml" 2>&1 | tail -10
```

Expected: 1 TOML test passing. If a field mismatch shows up, the most common causes are (a) `tempo` being written as a fractional value when the loader expects integer-like syntax, or (b) `escapeToml` missing a backslash. Compare the produced TOML side-by-side with the loader test fixtures.

- [ ] **Step 5: Full regression**

```bash
ctest --test-dir build 2>&1 | tail -3
```

Expected: **126/126**.

- [ ] **Step 6: Commit**

```bash
git add Source/Core/ConfigWriter.cpp Tests/ConfigWriter_tests.cpp
git commit -m "$(cat <<'EOF'
feat(config): TOML writer for Config

Hand-rolled string formatter (the schema is small enough that going
through toml++'s serialisation API would add more complexity than it
saves). Writes top-level scalars then one [[instruments]] table per
ConfigInstrument. Optional fields are omitted when at default values
(empty optional, transpose 0, volumePercent 100). Round-trips with
the existing TOML loader.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: ConfigWriter — XML

**Files:**
- Modify: `Source/Core/ConfigWriter.cpp`
- Modify: `Tests/ConfigWriter_tests.cpp`

- [ ] **Step 1: Write the failing XML round-trip test**

Append to `Tests/ConfigWriter_tests.cpp`:

```cpp
TEST_CASE ("config-writer: XML round-trip preserves all fields", "[config-writer][xml]")
{
    const auto original = richConfig();
    std::string text;
    REQUIRE (lotro::writeConfigToString (lotro::ConfigFormat::Xml, original, text).empty());

    lotro::Config parsed;
    REQUIRE (lotro::loadConfigFromString (text, lotro::ConfigFormat::Xml, parsed).empty());

    checkConfigsEqual (parsed, original);
}
```

- [ ] **Step 2: Verify the test fails**

```bash
cmake --build build && ctest --test-dir build -R "config-writer.*xml" 2>&1 | tail -10
```

Expected: failure with "XML writer not yet implemented".

- [ ] **Step 3: Implement the XML writer**

In `Source/Core/ConfigWriter.cpp`, add to the anonymous namespace (after `writeToml`):

```cpp
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
            top.createNewChildElement ("tempo")
               ->addTextElement (juce::String ((int) std::lround (*cfg.tempo)));
        if (cfg.transpose != 0)
            top.createNewChildElement ("transpose")
               ->addTextElement (juce::String (cfg.transpose));

        auto* instrumentsElem = top.createNewChildElement ("instruments");
        for (const auto& inst : cfg.instruments)
        {
            auto* iElem = instrumentsElem->createNewChildElement ("instrument");
            iElem->setAttribute ("x", inst.x);
            iElem->setAttribute ("name", juce::String (inst.name));
            if (inst.label.has_value())
                iElem->setAttribute ("label", juce::String (*inst.label));

            auto* sources = iElem->createNewChildElement ("sources");
            for (int s : inst.sources)
                sources->createNewChildElement ("source")->addTextElement (juce::String (s));

            if (inst.transposeSemitones != 0)
                iElem->createNewChildElement ("transposeSemitones")
                     ->addTextElement (juce::String (inst.transposeSemitones));
            if (inst.volumePercent != 100)
                iElem->createNewChildElement ("volumePercent")
                     ->addTextElement (juce::String (inst.volumePercent));
            if (inst.drumMap.has_value())
                iElem->createNewChildElement ("drumMap")
                     ->addTextElement (juce::String (*inst.drumMap));
        }

        out = top.toString().toStdString();
        return {};
    }
```

Update the dispatcher:

```cpp
        case ConfigFormat::Xml:  return writeXml (cfg, out);
```

- [ ] **Step 4: Run the XML test**

```bash
cmake --build build && ctest --test-dir build -R "config-writer.*xml" 2>&1 | tail -10
```

Expected: 1 XML test passing. The XML loader's pre-parse `looksWellFormed` check should accept JUCE's `XmlElement::toString()` output without issue.

- [ ] **Step 5: Full regression**

```bash
ctest --test-dir build 2>&1 | tail -3
```

Expected: **127/127**.

- [ ] **Step 6: Commit**

```bash
git add Source/Core/ConfigWriter.cpp Tests/ConfigWriter_tests.cpp
git commit -m "$(cat <<'EOF'
feat(config): XML writer for Config

Builds a juce::XmlElement tree mirroring the schema the XML loader
expects (root <config>, <instruments> wrapper, <instrument> elements
with x/name/label as attributes and sources as nested <source>
children). Round-trips with the existing XML loader.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Phase B — UI scaffolding (manual smoke)

GUI tests are mostly manual smoke. Each task ends with a build + a launch step where you visually verify the change.

### Task 5: CMake `converter_ui` target with hello-world window

**Files:**
- Create: `Source/UI/UiMain.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Install Linux GUI dev packages (one-time)**

If on a fresh WSL or Linux box, the JUCE GUI modules need:

```bash
sudo apt-get install -y libfreetype6-dev libfontconfig1-dev \
    libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev \
    libasound2-dev
```

If they are already installed, skip. The CLI build does not need any of these — they're for the new GUI target.

- [ ] **Step 2: Write `Source/UI/UiMain.cpp` (minimal hello window)**

```cpp
#include <juce_gui_basics/juce_gui_basics.h>

namespace lotro
{

class HelloWindow : public juce::DocumentWindow
{
public:
    HelloWindow()
        : juce::DocumentWindow ("LOTRO ABC Converter UI",
                                juce::Colours::lightgrey,
                                juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setResizable (true, true);
        centreWithSize (800, 600);
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

class UiApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "LotroAbcConverterUi"; }
    const juce::String getApplicationVersion() override    { return "0.1"; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String&) override         { window = std::make_unique<HelloWindow>(); }
    void shutdown() override                               { window.reset(); }
    void systemRequestedQuit() override                    { quit(); }

private:
    std::unique_ptr<HelloWindow> window;
};

} // namespace lotro

START_JUCE_APPLICATION (lotro::UiApp)
```

- [ ] **Step 3: Add the `converter_ui` target to `CMakeLists.txt`**

After the `converter` target's `target_link_libraries(...)` block, append:

```cmake
# ---------------------------------------------------------------------------
# converter_ui — JUCE GUI editor for converter config files
# ---------------------------------------------------------------------------
juce_add_gui_app(converter_ui
    PRODUCT_NAME "LotroAbcConverterUi"
    COMPANY_NAME "LotroAbcConverter"
    VERSION ${PROJECT_VERSION})

juce_generate_juce_header(converter_ui)

target_sources(converter_ui PRIVATE
    Source/UI/UiMain.cpp)

target_compile_definitions(converter_ui PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:converter_ui,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:converter_ui,JUCE_VERSION>"
    JUCE_USE_DARK_SPLASH_SCREEN=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(converter_ui PRIVATE
    converter_core
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_lto_flags)
```

- [ ] **Step 4: Build and launch**

```bash
cmake --build build 2>&1 | tail -10
build/converter_ui_artefacts/Debug/converter_ui &
```

Expected: an 800×600 window titled "LOTRO ABC Converter UI" appears, containing nothing. Close it.

If the window doesn't appear and there are X11 errors in stderr, the apt packages from Step 1 are missing or the WSL environment doesn't have a display server.

- [ ] **Step 5: Confirm the existing tests still pass**

```bash
ctest --test-dir build 2>&1 | tail -3
```

Expected: **127/127**.

- [ ] **Step 6: Commit**

```bash
git add Source/UI/UiMain.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(ui): scaffold converter_ui JUCE GUI app target

New executable target alongside converter and converter_tests, linking
converter_core plus juce_gui_basics and juce_gui_extra. Single
hello-world window proves the toolchain wiring; subsequent tasks fill
in the actual editor + diagnostics components.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 6: `MainWindow` skeleton with menu bar and splitter

**Files:**
- Create: `Source/UI/MainWindow.h`
- Create: `Source/UI/MainWindow.cpp`
- Create: `Source/UI/EditorPane.h` (stub)
- Create: `Source/UI/EditorPane.cpp` (stub)
- Create: `Source/UI/DiagnosticsPane.h` (stub)
- Create: `Source/UI/DiagnosticsPane.cpp` (stub)
- Modify: `Source/UI/UiMain.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the EditorPane stub**

`Source/UI/EditorPane.h`:

```cpp
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace lotro
{

class EditorPane : public juce::Component
{
public:
    EditorPane();
    ~EditorPane() override = default;

    void paint (juce::Graphics& g) override;
};

} // namespace lotro
```

`Source/UI/EditorPane.cpp`:

```cpp
#include "EditorPane.h"

namespace lotro
{

EditorPane::EditorPane()  = default;

void EditorPane::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::white);
    g.setColour (juce::Colours::darkgrey);
    g.setFont (16.0f);
    g.drawText ("Editor pane (stub)", getLocalBounds(), juce::Justification::centred);
}

} // namespace lotro
```

- [ ] **Step 2: Write the DiagnosticsPane stub**

`Source/UI/DiagnosticsPane.h`:

```cpp
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace lotro
{

class DiagnosticsPane : public juce::Component
{
public:
    DiagnosticsPane();
    ~DiagnosticsPane() override = default;

    void paint (juce::Graphics& g) override;
};

} // namespace lotro
```

`Source/UI/DiagnosticsPane.cpp`:

```cpp
#include "DiagnosticsPane.h"

namespace lotro
{

DiagnosticsPane::DiagnosticsPane() = default;

void DiagnosticsPane::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::whitesmoke);
    g.setColour (juce::Colours::darkgrey);
    g.setFont (16.0f);
    g.drawText ("Diagnostics pane (stub)", getLocalBounds(), juce::Justification::centred);
}

} // namespace lotro
```

- [ ] **Step 3: Write `Source/UI/MainWindow.h`**

```cpp
#pragma once

#include "EditorPane.h"
#include "DiagnosticsPane.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace lotro
{

class MainWindow : public juce::DocumentWindow,
                   public juce::MenuBarModel
{
public:
    MainWindow();
    ~MainWindow() override;

    void closeButtonPressed() override;

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu   getMenuForIndex (int topLevelMenuIndex,
                                       const juce::String& menuName) override;
    void              menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

private:
    enum CommandId
    {
        FileOpenMidi    = 1,
        FileOpenConfig,
        FileSaveConfig,
        FileSaveAsJson,
        FileSaveAsToml,
        FileSaveAsXml,
        FileQuit,
        EditAddInstrument,
        EditRemoveSelected
    };

    class Body;
    std::unique_ptr<Body> body;
    std::unique_ptr<juce::MenuBarComponent> menuBar;
};

} // namespace lotro
```

- [ ] **Step 4: Write `Source/UI/MainWindow.cpp`**

```cpp
#include "MainWindow.h"

namespace lotro
{

class MainWindow::Body : public juce::Component
{
public:
    Body()
    {
        addAndMakeVisible (editor);
        addAndMakeVisible (diagnostics);

        splitter.setComponents (&editor, &diagnostics);
        addAndMakeVisible (splitter);
    }

    void resized() override
    {
        splitter.setBounds (getLocalBounds());
    }

private:
    class Splitter : public juce::Component
    {
    public:
        void setComponents (juce::Component* l, juce::Component* r)
        {
            left = l; right = r;
        }

        void resized() override
        {
            const auto area = getLocalBounds();
            const int barWidth = 6;
            const int leftWidth = (int) std::lround (area.getWidth() * leftFraction);
            if (left)  left->setBounds  (area.withWidth (leftWidth));
            bar.setBounds (area.withX (leftWidth).withWidth (barWidth));
            if (right) right->setBounds (area.withTrimmedLeft (leftWidth + barWidth));
            addAndMakeVisible (bar);
        }

    private:
        class Bar : public juce::Component
        {
        public:
            void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::grey); }
            void mouseDrag (const juce::MouseEvent& e) override
            {
                if (auto* parent = getParentComponent())
                {
                    auto* split = dynamic_cast<Splitter*> (parent);
                    if (split == nullptr) return;
                    const float w = (float) parent->getWidth();
                    if (w <= 0.0f) return;
                    const float newFraction = juce::jlimit (0.15f, 0.85f,
                        (e.getEventRelativeTo (parent).x) / w);
                    split->leftFraction = newFraction;
                    parent->resized();
                }
            }
            void mouseEnter (const juce::MouseEvent&) override
            {
                setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
            }
        };

        juce::Component* left  = nullptr;
        juce::Component* right = nullptr;
        Bar              bar;
        float            leftFraction = 0.55f;
        friend class Bar;
    };

    EditorPane      editor;
    DiagnosticsPane diagnostics;
    Splitter        splitter;
};

MainWindow::MainWindow()
    : juce::DocumentWindow ("LOTRO ABC Converter UI",
                            juce::Colours::lightgrey,
                            juce::DocumentWindow::allButtons),
      body (std::make_unique<Body>()),
      menuBar (std::make_unique<juce::MenuBarComponent> (this))
{
    setUsingNativeTitleBar (true);
    setResizable (true, true);
    centreWithSize (1000, 700);

    auto host = std::make_unique<juce::Component>();
    host->addAndMakeVisible (*menuBar);
    host->addAndMakeVisible (*body);
    auto* hostRaw = host.release();

    setContentOwned (hostRaw, false);

    hostRaw->setSize (1000, 700);
    menuBar->setBounds (0, 0, hostRaw->getWidth(), 24);
    body->setBounds (0, 24, hostRaw->getWidth(), hostRaw->getHeight() - 24);

    setVisible (true);
}

MainWindow::~MainWindow() = default;

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

juce::StringArray MainWindow::getMenuBarNames()
{
    return { "File", "Edit" };
}

juce::PopupMenu MainWindow::getMenuForIndex (int topLevelMenuIndex, const juce::String&)
{
    juce::PopupMenu m;
    if (topLevelMenuIndex == 0)
    {
        m.addItem (FileOpenMidi,    "Open MIDI...",    true, false);
        m.addItem (FileOpenConfig,  "Open Config...",  true, false);
        m.addSeparator();
        m.addItem (FileSaveConfig,  "Save Config",     false, false);
        juce::PopupMenu saveAs;
        saveAs.addItem (FileSaveAsJson, "JSON (.json)", true, false);
        saveAs.addItem (FileSaveAsToml, "TOML (.toml)", true, false);
        saveAs.addItem (FileSaveAsXml,  "XML (.xml)",   true, false);
        m.addSubMenu ("Save Config As", saveAs);
        m.addSeparator();
        m.addItem (FileQuit, "Quit");
    }
    else if (topLevelMenuIndex == 1)
    {
        m.addItem (EditAddInstrument,    "Add Instrument",    false, false);
        m.addItem (EditRemoveSelected,   "Remove Selected",   false, false);
    }
    return m;
}

void MainWindow::menuItemSelected (int menuItemID, int)
{
    if (menuItemID == FileQuit)
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    // Other items are placeholders until later tasks wire them up.
}

} // namespace lotro
```

- [ ] **Step 5: Replace UiMain.cpp's hello-window with MainWindow**

```cpp
#include "MainWindow.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace lotro
{

class UiApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "LotroAbcConverterUi"; }
    const juce::String getApplicationVersion() override    { return "0.1"; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String&) override         { window = std::make_unique<MainWindow>(); }
    void shutdown() override                               { window.reset(); }
    void systemRequestedQuit() override                    { quit(); }

private:
    std::unique_ptr<MainWindow> window;
};

} // namespace lotro

START_JUCE_APPLICATION (lotro::UiApp)
```

- [ ] **Step 6: Add the new sources to CMakeLists.txt**

In the `target_sources(converter_ui PRIVATE …)` block:

```cmake
target_sources(converter_ui PRIVATE
    Source/UI/UiMain.cpp
    Source/UI/MainWindow.cpp
    Source/UI/EditorPane.cpp
    Source/UI/DiagnosticsPane.cpp)
```

- [ ] **Step 7: Build and launch**

```bash
cmake --build build 2>&1 | tail -10
build/converter_ui_artefacts/Debug/converter_ui &
```

Expected:
- Window with menu bar (File, Edit) at the top.
- File menu: Open MIDI... / Open Config... / Save Config (greyed) / Save Config As ▸ JSON/TOML/XML / Quit.
- Edit menu: Add Instrument (greyed), Remove Selected (greyed).
- Body split into two panes; left says "Editor pane (stub)", right says "Diagnostics pane (stub)".
- Drag the divider to resize the panes.
- File → Quit closes the window.

- [ ] **Step 8: Commit**

```bash
git add Source/UI/MainWindow.h Source/UI/MainWindow.cpp \
        Source/UI/EditorPane.h Source/UI/EditorPane.cpp \
        Source/UI/DiagnosticsPane.h Source/UI/DiagnosticsPane.cpp \
        Source/UI/UiMain.cpp \
        CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(ui): MainWindow with menu bar and resizable splitter

Adds the top-level window shell: File / Edit menus (most items
greyed out until later tasks wire them up), draggable splitter
between EditorPane and DiagnosticsPane stubs. Quit works.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Phase C — Editor pane

### Task 7: `GlobalSettingsView` — top-of-editor form for top-level Config fields

**Files:**
- Create: `Source/UI/GlobalSettingsView.h`
- Create: `Source/UI/GlobalSettingsView.cpp`
- Modify: `Source/UI/EditorPane.h` / `.cpp`
- Modify: `CMakeLists.txt`

The EditorPane is going to own a `Config` and a `Song`. GlobalSettingsView is the topmost sub-view, editing the Config's top-level fields (input/output paths read-only for now since Open MIDI sets them, title, transcriber, tempo, transpose, drum-map default).

- [ ] **Step 1: Add `Config` and `Song` ownership to EditorPane**

Open `Source/UI/EditorPane.h`. Replace the body with:

```cpp
#pragma once

#include "Core/Config.h"
#include "Core/Song.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace lotro
{

class GlobalSettingsView;

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
    Config                              config;
    Song                                raw;
    std::unique_ptr<GlobalSettingsView> globalView;
};

} // namespace lotro
```

`Source/UI/EditorPane.cpp`:

```cpp
#include "EditorPane.h"
#include "GlobalSettingsView.h"

namespace lotro
{

EditorPane::EditorPane()
    : globalView (std::make_unique<GlobalSettingsView> (config,
                                                        [this] { if (onConfigChanged) onConfigChanged(); }))
{
    addAndMakeVisible (*globalView);
}

EditorPane::~EditorPane() = default;

void EditorPane::loadFromMidi (Song newRaw, Config newCfg)
{
    raw    = std::move (newRaw);
    config = std::move (newCfg);
    globalView->refresh();
    repaint();
    if (onConfigChanged) onConfigChanged();
}

void EditorPane::resized()
{
    auto area = getLocalBounds().reduced (8);
    globalView->setBounds (area.removeFromTop (220));
    // Subsequent sub-views (instruments table, detail form, run button)
    // get added in later tasks; they fill the remainder.
}

void EditorPane::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::white);
}

} // namespace lotro
```

- [ ] **Step 2: Write GlobalSettingsView**

`Source/UI/GlobalSettingsView.h`:

```cpp
#pragma once

#include "Core/Config.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace lotro
{

class GlobalSettingsView : public juce::Component,
                           private juce::TextEditor::Listener
{
public:
    GlobalSettingsView (Config& configRef, std::function<void()> onChange);

    void refresh();

    void resized() override;

private:
    void textEditorTextChanged (juce::TextEditor&) override;

    Config&                       config;
    std::function<void()>         notifyChange;

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

`Source/UI/GlobalSettingsView.cpp`:

```cpp
#include "GlobalSettingsView.h"

namespace lotro
{

GlobalSettingsView::GlobalSettingsView (Config& cfgRef, std::function<void()> onChange)
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
        if (numeric)
            f.setInputRestrictions (16, "-0123456789.");
        f.addListener (this);
    };

    setUpLabel (inputLabel);
    setUpLabel (inputValue);
    setUpLabel (outputLabel);
    setUpLabel (outputValue);
    setUpLabel (titleLabel);
    setUpField (titleField, false);
    setUpLabel (transcriberLabel);
    setUpField (transcriberField, false);
    setUpLabel (tempoLabel);
    setUpField (tempoField, true);
    setUpLabel (transposeLabel);
    setUpField (transposeField, true);

    refresh();
}

void GlobalSettingsView::refresh()
{
    inputValue.setText  (juce::String (config.input),                       juce::dontSendNotification);
    outputValue.setText (juce::String (config.output.value_or (std::string{})), juce::dontSendNotification);
    titleField.setText  (juce::String (config.title.value_or (std::string{})),  juce::dontSendNotification);
    transcriberField.setText (juce::String (config.transcriber.value_or (std::string{})),
                              juce::dontSendNotification);
    tempoField.setText  (config.tempo.has_value() ? juce::String (*config.tempo) : juce::String(),
                          juce::dontSendNotification);
    transposeField.setText (juce::String (config.transpose), juce::dontSendNotification);
}

void GlobalSettingsView::resized()
{
    auto area = getLocalBounds().reduced (4);
    const int rowH = 26;
    const int labelW = 140;

    auto layoutRow = [&] (juce::Label& label, juce::Component& field)
    {
        auto row = area.removeFromTop (rowH);
        label.setBounds (row.removeFromLeft (labelW));
        field.setBounds (row);
        area.removeFromTop (4);
    };

    layoutRow (inputLabel,       inputValue);
    layoutRow (outputLabel,      outputValue);
    layoutRow (titleLabel,       titleField);
    layoutRow (transcriberLabel, transcriberField);
    layoutRow (tempoLabel,       tempoField);
    layoutRow (transposeLabel,   transposeField);
}

void GlobalSettingsView::textEditorTextChanged (juce::TextEditor& ed)
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

- [ ] **Step 3: Register the new sources**

In CMakeLists.txt, add to `target_sources(converter_ui …)`:

```cmake
    Source/UI/GlobalSettingsView.cpp
```

- [ ] **Step 4: Build and launch**

```bash
cmake --build build 2>&1 | tail -10
build/converter_ui_artefacts/Debug/converter_ui &
```

Expected: editor pane now shows six labelled rows (Input MIDI / Output ABC blank, Title / Transcriber / Tempo (BPM) / Global transpose). Typing in any field doesn't error. Diagnostics pane stub still shows.

- [ ] **Step 5: Commit**

```bash
git add Source/UI/GlobalSettingsView.h Source/UI/GlobalSettingsView.cpp \
        Source/UI/EditorPane.h Source/UI/EditorPane.cpp \
        CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(ui): EditorPane owns Config + Song; add GlobalSettingsView

EditorPane now holds the in-memory Config and raw Song. GlobalSettingsView
renders six rows for the top-level fields (input/output read-only labels
plus title, transcriber, tempo, transpose). Edits push back into Config
and fire the onConfigChanged callback for future dirty-bit tracking.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 8: `InstrumentsTable` — table model + Add/Remove buttons

**Files:**
- Create: `Source/UI/InstrumentsTable.h` / `.cpp`
- Modify: `Source/UI/EditorPane.h` / `.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write InstrumentsTable**

`Source/UI/InstrumentsTable.h`:

```cpp
#pragma once

#include "Core/Config.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace lotro
{

class InstrumentsTable : public juce::Component,
                         private juce::TableListBoxModel
{
public:
    InstrumentsTable (Config& configRef,
                      std::function<void(int)> onSelectionChanged,
                      std::function<void()>    onConfigMutated);

    void refresh();
    int  getSelectedInstrumentIndex() const;

    void resized() override;

private:
    // TableListBoxModel
    int  getNumRows() override;
    void paintRowBackground (juce::Graphics&, int rowNumber, int, int, bool rowIsSelected) override;
    void paintCell (juce::Graphics&, int rowNumber, int columnId, int width, int height,
                    bool rowIsSelected) override;
    void selectedRowsChanged (int lastRowSelected) override;

    void addInstrument();
    void removeSelectedInstrument();

    Config&                  config;
    std::function<void(int)> notifySelection;
    std::function<void()>    notifyMutation;
    juce::TableListBox       table;
    juce::TextButton         addButton    { "+ Add" };
    juce::TextButton         removeButton { "- Remove" };
};

} // namespace lotro
```

`Source/UI/InstrumentsTable.cpp`:

```cpp
#include "InstrumentsTable.h"

namespace lotro
{

InstrumentsTable::InstrumentsTable (Config&                   cfgRef,
                                    std::function<void(int)>  onSelChanged,
                                    std::function<void()>     onMutated)
    : config (cfgRef),
      notifySelection (std::move (onSelChanged)),
      notifyMutation  (std::move (onMutated))
{
    table.setModel (this);
    table.getHeader().addColumn ("X",       1, 40);
    table.getHeader().addColumn ("Name",    2, 140);
    table.getHeader().addColumn ("Label",   3, 140);
    table.getHeader().addColumn ("Sources", 4, 100);
    addAndMakeVisible (table);

    addAndMakeVisible (addButton);
    addAndMakeVisible (removeButton);
    addButton.onClick    = [this] { addInstrument(); };
    removeButton.onClick = [this] { removeSelectedInstrument(); };
}

void InstrumentsTable::refresh()
{
    table.updateContent();
    table.repaint();
}

int InstrumentsTable::getSelectedInstrumentIndex() const
{
    return table.getSelectedRow();
}

int InstrumentsTable::getNumRows()
{
    return (int) config.instruments.size();
}

void InstrumentsTable::paintRowBackground (juce::Graphics& g, int, int, int, bool rowIsSelected)
{
    g.fillAll (rowIsSelected ? juce::Colours::lightblue : juce::Colours::white);
}

void InstrumentsTable::paintCell (juce::Graphics& g, int rowNumber, int columnId,
                                  int width, int height, bool)
{
    if (rowNumber < 0 || rowNumber >= (int) config.instruments.size()) return;
    const auto& inst = config.instruments[(size_t) rowNumber];
    g.setColour (juce::Colours::black);
    g.setFont (14.0f);
    juce::String text;
    switch (columnId)
    {
        case 1: text = juce::String (inst.x); break;
        case 2: text = juce::String (inst.name); break;
        case 3: text = juce::String (inst.label.value_or (std::string{})); break;
        case 4:
        {
            juce::StringArray parts;
            for (int s : inst.sources) parts.add (juce::String (s));
            text = parts.joinIntoString (", ");
            break;
        }
    }
    g.drawText (text, 4, 0, width - 8, height, juce::Justification::centredLeft);
}

void InstrumentsTable::selectedRowsChanged (int lastRowSelected)
{
    if (notifySelection) notifySelection (lastRowSelected);
}

void InstrumentsTable::addInstrument()
{
    ConfigInstrument fresh;
    int nextX = 1;
    for (const auto& inst : config.instruments)
        nextX = std::max (nextX, inst.x + 1);
    fresh.x       = nextX;
    fresh.name    = "LuteOfAges";
    fresh.sources = {};
    config.instruments.push_back (fresh);
    if (notifyMutation) notifyMutation();
    refresh();
    table.selectRow ((int) config.instruments.size() - 1);
}

void InstrumentsTable::removeSelectedInstrument()
{
    const int row = table.getSelectedRow();
    if (row < 0 || row >= (int) config.instruments.size()) return;
    config.instruments.erase (config.instruments.begin() + row);
    if (notifyMutation) notifyMutation();
    refresh();
    if (notifySelection) notifySelection (-1);
}

void InstrumentsTable::resized()
{
    auto area = getLocalBounds();
    auto buttons = area.removeFromBottom (28);
    addButton.setBounds    (buttons.removeFromLeft (80).reduced (2));
    removeButton.setBounds (buttons.removeFromLeft (100).reduced (2));
    table.setBounds (area);
}

} // namespace lotro
```

- [ ] **Step 2: Wire InstrumentsTable into EditorPane**

Edit `Source/UI/EditorPane.h`. Add forward declaration and member:

```cpp
class GlobalSettingsView;
class InstrumentsTable;

class EditorPane : public juce::Component
{
public:
    // ... unchanged ...

private:
    Config                              config;
    Song                                raw;
    std::unique_ptr<GlobalSettingsView> globalView;
    std::unique_ptr<InstrumentsTable>   instrumentsTable;
};
```

Edit `Source/UI/EditorPane.cpp`:

```cpp
#include "EditorPane.h"
#include "GlobalSettingsView.h"
#include "InstrumentsTable.h"

namespace lotro
{

EditorPane::EditorPane()
    : globalView (std::make_unique<GlobalSettingsView> (config,
                                                        [this] { if (onConfigChanged) onConfigChanged(); })),
      instrumentsTable (std::make_unique<InstrumentsTable> (
          config,
          /*onSelectionChanged*/ [] (int) {},
          /*onConfigMutated*/    [this] { if (onConfigChanged) onConfigChanged(); }))
{
    addAndMakeVisible (*globalView);
    addAndMakeVisible (*instrumentsTable);
}

EditorPane::~EditorPane() = default;

void EditorPane::loadFromMidi (Song newRaw, Config newCfg)
{
    raw    = std::move (newRaw);
    config = std::move (newCfg);
    globalView->refresh();
    instrumentsTable->refresh();
    repaint();
    if (onConfigChanged) onConfigChanged();
}

void EditorPane::resized()
{
    auto area = getLocalBounds().reduced (8);
    globalView->setBounds (area.removeFromTop (220));
    area.removeFromTop (8);
    instrumentsTable->setBounds (area.removeFromTop (240));
}

void EditorPane::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::white);
}

} // namespace lotro
```

- [ ] **Step 3: Add InstrumentsTable.cpp to CMakeLists.txt**

```cmake
    Source/UI/InstrumentsTable.cpp
```

- [ ] **Step 4: Build and launch**

```bash
cmake --build build && build/converter_ui_artefacts/Debug/converter_ui &
```

Expected: editor pane now shows the Globals on top and an empty 4-column table (X / Name / Label / Sources) below. Click `+ Add` — a row appears with x=1, name=LuteOfAges. Click `+ Add` again — row with x=2 appears. Select a row, click `- Remove` — that row disappears.

- [ ] **Step 5: Commit**

```bash
git add Source/UI/InstrumentsTable.h Source/UI/InstrumentsTable.cpp \
        Source/UI/EditorPane.h Source/UI/EditorPane.cpp \
        CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(ui): InstrumentsTable with Add and Remove buttons

Master view of the master-detail editor. Renders the four columns
spec'd (X, Name, Label, Sources) over the EditorPane's in-memory
Config. Add picks the next free x; Remove drops the selected row.
Mutations fire onConfigMutated for future dirty-bit tracking.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 9: `InstrumentDetailForm` + selection wiring

**Files:**
- Create: `Source/UI/InstrumentDetailForm.h` / `.cpp`
- Modify: `Source/UI/EditorPane.h` / `.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write InstrumentDetailForm**

`Source/UI/InstrumentDetailForm.h`:

```cpp
#pragma once

#include "Core/Config.h"
#include "Core/Song.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace lotro
{

class InstrumentDetailForm : public juce::Component,
                             private juce::TextEditor::Listener,
                             private juce::ComboBox::Listener,
                             private juce::Button::Listener
{
public:
    InstrumentDetailForm (Config& configRef,
                          const Song& rawRef,
                          std::function<void()> onMutated);

    // Sets which instrument index is being edited; -1 means "no selection".
    void editInstrument (int instrumentIndex);

    // Called when the underlying Config or Song changes externally
    // (e.g. on Open MIDI). Re-syncs the controls to current state.
    void refresh();

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    void textEditorTextChanged (juce::TextEditor&) override;
    void comboBoxChanged       (juce::ComboBox*) override;
    void buttonClicked         (juce::Button*) override;

    void rebuildSourceCheckboxes();
    void pushFromControlsToConfig();

    Config&                                  config;
    const Song&                              raw;
    std::function<void()>                    notifyMutation;
    int                                      currentIndex = -1;

    juce::Label    nameLabel    { {}, "name:" };
    juce::ComboBox nameCombo;
    juce::Label    labelLabel   { {}, "label:" };
    juce::TextEditor labelField;
    juce::Label    transposeLabel { {}, "transpose semitones:" };
    juce::TextEditor transposeField;
    juce::Label    volumeLabel  { {}, "volume %:" };
    juce::TextEditor volumeField;
    juce::Label    drumMapLabel { {}, "drum-map (Drums only):" };
    juce::TextEditor drumMapField;
    juce::Label    sourcesLabel { {}, "sources:" };
    juce::OwnedArray<juce::ToggleButton> sourceChecks;
};

} // namespace lotro
```

`Source/UI/InstrumentDetailForm.cpp`:

```cpp
#include "InstrumentDetailForm.h"
#include "Core/LotroInstrument.h"

namespace lotro
{

InstrumentDetailForm::InstrumentDetailForm (Config& cfgRef, const Song& rawRef,
                                            std::function<void()> onMutated)
    : config (cfgRef), raw (rawRef), notifyMutation (std::move (onMutated))
{
    auto setUpLabel = [this] (juce::Label& l) { addAndMakeVisible (l); };
    auto setUpField = [this] (juce::TextEditor& f, bool numeric)
    {
        addAndMakeVisible (f);
        f.setMultiLine (false);
        f.setBorder (juce::BorderSize<int> (1));
        if (numeric) f.setInputRestrictions (8, "-0123456789");
        f.addListener (this);
    };

    setUpLabel (nameLabel);
    addAndMakeVisible (nameCombo);
    int id = 1;
    for (const auto name : allInstrumentNames())
        nameCombo.addItem (juce::String (std::string (name)), id++);
    nameCombo.addListener (this);

    setUpLabel (labelLabel);
    setUpField (labelField, false);
    setUpLabel (transposeLabel);
    setUpField (transposeField, true);
    setUpLabel (volumeLabel);
    setUpField (volumeField, true);
    setUpLabel (drumMapLabel);
    setUpField (drumMapField, false);
    setUpLabel (sourcesLabel);
}

void InstrumentDetailForm::editInstrument (int instrumentIndex)
{
    currentIndex = instrumentIndex;
    refresh();
}

void InstrumentDetailForm::refresh()
{
    const bool valid = currentIndex >= 0 && currentIndex < (int) config.instruments.size();
    const bool enabled = valid;

    nameCombo.setEnabled (enabled);
    labelField.setEnabled (enabled);
    transposeField.setEnabled (enabled);
    volumeField.setEnabled (enabled);

    if (! valid)
    {
        nameCombo.setSelectedId (0, juce::dontSendNotification);
        labelField.setText      ({}, juce::dontSendNotification);
        transposeField.setText  ({}, juce::dontSendNotification);
        volumeField.setText     ({}, juce::dontSendNotification);
        drumMapField.setText    ({}, juce::dontSendNotification);
        drumMapField.setEnabled (false);
        sourceChecks.clear();
        repaint();
        return;
    }

    const auto& inst = config.instruments[(size_t) currentIndex];

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

    labelField.setText      (juce::String (inst.label.value_or (std::string{})), juce::dontSendNotification);
    transposeField.setText  (juce::String (inst.transposeSemitones), juce::dontSendNotification);
    volumeField.setText     (juce::String (inst.volumePercent), juce::dontSendNotification);
    drumMapField.setText    (juce::String (inst.drumMap.value_or (std::string{})), juce::dontSendNotification);

    LotroInstrument parsed = LotroInstrument::LuteOfAges;
    parseName (inst.name, parsed);
    drumMapField.setEnabled (parsed == LotroInstrument::Drums);

    rebuildSourceCheckboxes();
    resized();
    repaint();
}

void InstrumentDetailForm::rebuildSourceCheckboxes()
{
    sourceChecks.clear();
    if (currentIndex < 0) return;
    const auto& inst = config.instruments[(size_t) currentIndex];

    for (size_t i = 0; i < raw.tracks.size(); ++i)
    {
        const auto& track = raw.tracks[i];
        juce::String label = juce::String ((int) i) + ": " + juce::String (track.name)
            + " (chan " + juce::String (track.sourceMidiChannel)
            + ", " + juce::String ((int) track.notes.size()) + " notes)";
        auto* cb = sourceChecks.add (new juce::ToggleButton (label));
        addAndMakeVisible (cb);
        cb->setToggleState (
            std::find (inst.sources.begin(), inst.sources.end(), (int) i) != inst.sources.end(),
            juce::dontSendNotification);
        cb->addListener (this);
    }
}

void InstrumentDetailForm::pushFromControlsToConfig()
{
    if (currentIndex < 0 || currentIndex >= (int) config.instruments.size()) return;
    auto& inst = config.instruments[(size_t) currentIndex];

    if (auto idx = nameCombo.getSelectedId(); idx > 0)
    {
        const auto names = allInstrumentNames();
        if ((size_t) idx <= names.size())
            inst.name = std::string (names[(size_t) idx - 1]);
    }

    {
        const auto s = labelField.getText().toStdString();
        inst.label = s.empty() ? std::optional<std::string>{} : std::optional<std::string>{ s };
    }
    {
        const auto t = transposeField.getText();
        inst.transposeSemitones = t.isEmpty() ? 0 : t.getIntValue();
    }
    {
        const auto v = volumeField.getText();
        inst.volumePercent = v.isEmpty() ? 100 : v.getIntValue();
    }
    {
        const auto s = drumMapField.getText().toStdString();
        inst.drumMap = s.empty() ? std::optional<std::string>{} : std::optional<std::string>{ s };
    }
    {
        std::vector<int> picked;
        for (int i = 0; i < sourceChecks.size(); ++i)
            if (sourceChecks[i]->getToggleState())
                picked.push_back (i);
        inst.sources = std::move (picked);
    }

    LotroInstrument parsed = LotroInstrument::LuteOfAges;
    parseName (inst.name, parsed);
    drumMapField.setEnabled (parsed == LotroInstrument::Drums);

    if (notifyMutation) notifyMutation();
}

void InstrumentDetailForm::textEditorTextChanged (juce::TextEditor&) { pushFromControlsToConfig(); }
void InstrumentDetailForm::comboBoxChanged       (juce::ComboBox*)    { pushFromControlsToConfig(); }
void InstrumentDetailForm::buttonClicked         (juce::Button*)      { pushFromControlsToConfig(); }

void InstrumentDetailForm::resized()
{
    auto area = getLocalBounds().reduced (4);
    const int rowH = 26;
    const int labelW = 180;

    auto layoutRow = [&] (juce::Label& l, juce::Component& f)
    {
        auto row = area.removeFromTop (rowH);
        l.setBounds (row.removeFromLeft (labelW));
        f.setBounds (row);
        area.removeFromTop (4);
    };

    layoutRow (nameLabel,      nameCombo);
    layoutRow (labelLabel,     labelField);
    layoutRow (transposeLabel, transposeField);
    layoutRow (volumeLabel,    volumeField);
    layoutRow (drumMapLabel,   drumMapField);

    sourcesLabel.setBounds (area.removeFromTop (rowH));
    area.removeFromTop (4);
    for (auto* cb : sourceChecks)
        cb->setBounds (area.removeFromTop (rowH));
}

void InstrumentDetailForm::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::whitesmoke);
}

} // namespace lotro
```

- [ ] **Step 2: Wire it into EditorPane and connect to InstrumentsTable selection**

Edit `Source/UI/EditorPane.h`. Add forward and member:

```cpp
class InstrumentDetailForm;

class EditorPane : public juce::Component
{
    // ...
private:
    Config                                config;
    Song                                  raw;
    std::unique_ptr<GlobalSettingsView>   globalView;
    std::unique_ptr<InstrumentsTable>     instrumentsTable;
    std::unique_ptr<InstrumentDetailForm> detailForm;
};
```

`Source/UI/EditorPane.cpp`:

```cpp
#include "EditorPane.h"
#include "GlobalSettingsView.h"
#include "InstrumentsTable.h"
#include "InstrumentDetailForm.h"

namespace lotro
{

EditorPane::EditorPane()
{
    globalView = std::make_unique<GlobalSettingsView> (
        config, [this] { if (onConfigChanged) onConfigChanged(); });

    detailForm = std::make_unique<InstrumentDetailForm> (
        config, raw, [this] { if (onConfigChanged) onConfigChanged();
                              if (instrumentsTable) instrumentsTable->refresh(); });

    instrumentsTable = std::make_unique<InstrumentsTable> (
        config,
        /*onSelectionChanged*/ [this] (int row) { if (detailForm) detailForm->editInstrument (row); },
        /*onConfigMutated*/    [this]            { if (onConfigChanged) onConfigChanged();
                                                   if (detailForm) detailForm->refresh(); });

    addAndMakeVisible (*globalView);
    addAndMakeVisible (*instrumentsTable);
    addAndMakeVisible (*detailForm);
}

EditorPane::~EditorPane() = default;

void EditorPane::loadFromMidi (Song newRaw, Config newCfg)
{
    raw    = std::move (newRaw);
    config = std::move (newCfg);
    globalView->refresh();
    instrumentsTable->refresh();
    detailForm->editInstrument (-1);
    repaint();
    if (onConfigChanged) onConfigChanged();
}

void EditorPane::resized()
{
    auto area = getLocalBounds().reduced (8);
    globalView->setBounds (area.removeFromTop (220));
    area.removeFromTop (8);
    instrumentsTable->setBounds (area.removeFromTop (200));
    area.removeFromTop (8);
    detailForm->setBounds (area);
}

void EditorPane::paint (juce::Graphics& g) { g.fillAll (juce::Colours::white); }

} // namespace lotro
```

- [ ] **Step 3: Add to CMakeLists.txt**

```cmake
    Source/UI/InstrumentDetailForm.cpp
```

- [ ] **Step 4: Build and launch**

```bash
cmake --build build && build/converter_ui_artefacts/Debug/converter_ui &
```

Expected: detail form below the table. With no instrument selected, all controls are disabled. Add an instrument; click on it; the detail form enables and shows the values. Edit the label or change the dropdown — the table row updates to match. The drum-map field disables unless the instrument's name is "Drums". Sources area is empty (no MIDI loaded yet — Task 10 fixes that).

- [ ] **Step 5: Commit**

```bash
git add Source/UI/InstrumentDetailForm.h Source/UI/InstrumentDetailForm.cpp \
        Source/UI/EditorPane.h Source/UI/EditorPane.cpp \
        CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(ui): InstrumentDetailForm + selection wiring

Detail half of the master-detail editor. Selection from
InstrumentsTable drives which instrument the form edits; edits push
back into the Config and refresh the table. Drum-map field disables
unless the instrument's name is Drums. Sources area renders one
checkbox per loaded MIDI track; without a MIDI, the area is empty —
that's wired up in the next task.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 10: `File → Open MIDI` and the auto-synthesised Config

**Files:**
- Modify: `Source/UI/MainWindow.h` / `.cpp`

The MainWindow's File → Open MIDI menu item needs to open a `juce::FileChooser`, parse the selected file via `MidiImporter::importMidi`, build a Config via `synthesiseConfig`, and hand both to `EditorPane::loadFromMidi`.

- [ ] **Step 1: Make MainWindow hold the EditorPane reference**

The Body class inside MainWindow.cpp owns the EditorPane. We need MainWindow to access it. Refactor: expose the EditorPane on Body and on MainWindow.

Edit `Source/UI/MainWindow.h`. Add forward declaration and accessor:

```cpp
namespace lotro
{

class EditorPane;
class DiagnosticsPane;

class MainWindow : public juce::DocumentWindow,
                   public juce::MenuBarModel,
                   public juce::FileDragAndDropTarget
{
public:
    MainWindow();
    ~MainWindow() override;

    void closeButtonPressed() override;

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu   getMenuForIndex (int topLevelMenuIndex,
                                       const juce::String& menuName) override;
    void              menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

    // FileDragAndDropTarget
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped           (const juce::StringArray& files, int x, int y) override;

private:
    enum CommandId
    {
        FileOpenMidi    = 1,
        FileOpenConfig,
        FileSaveConfig,
        FileSaveAsJson,
        FileSaveAsToml,
        FileSaveAsXml,
        FileQuit,
        EditAddInstrument,
        EditRemoveSelected
    };

    class Body;
    std::unique_ptr<Body> body;
    std::unique_ptr<juce::MenuBarComponent> menuBar;
    std::unique_ptr<juce::FileChooser>      fileChooser;

    void openMidiViaDialog();
    void openMidiFromPath (const juce::File& file);
};

} // namespace lotro
```

- [ ] **Step 2: Implement Open MIDI in MainWindow.cpp**

Replace the existing `MainWindow.cpp` with:

```cpp
#include "MainWindow.h"
#include "EditorPane.h"
#include "DiagnosticsPane.h"

#include "Core/MidiImporter.h"
#include "Core/Pipeline.h"

#include <fstream>

namespace lotro
{

class MainWindow::Body : public juce::Component
{
public:
    Body()
    {
        addAndMakeVisible (editor);
        addAndMakeVisible (diagnostics);
        splitter.setComponents (&editor, &diagnostics);
        addAndMakeVisible (splitter);
    }

    void resized() override { splitter.setBounds (getLocalBounds()); }

    EditorPane&      getEditor()      { return editor; }
    DiagnosticsPane& getDiagnostics() { return diagnostics; }

private:
    class Splitter : public juce::Component
    {
    public:
        void setComponents (juce::Component* l, juce::Component* r) { left = l; right = r; }

        void resized() override
        {
            const auto area = getLocalBounds();
            const int barWidth = 6;
            const int leftWidth = (int) std::lround (area.getWidth() * leftFraction);
            if (left)  left->setBounds  (area.withWidth (leftWidth));
            bar.setBounds (area.withX (leftWidth).withWidth (barWidth));
            if (right) right->setBounds (area.withTrimmedLeft (leftWidth + barWidth));
            addAndMakeVisible (bar);
        }

    private:
        class Bar : public juce::Component
        {
        public:
            void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::grey); }
            void mouseDrag (const juce::MouseEvent& e) override
            {
                if (auto* parent = getParentComponent())
                {
                    auto* split = dynamic_cast<Splitter*> (parent);
                    if (split == nullptr) return;
                    const float w = (float) parent->getWidth();
                    if (w <= 0.0f) return;
                    split->leftFraction = juce::jlimit (0.15f, 0.85f,
                        e.getEventRelativeTo (parent).x / w);
                    parent->resized();
                }
            }
            void mouseEnter (const juce::MouseEvent&) override
            {
                setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
            }
        };

        juce::Component* left  = nullptr;
        juce::Component* right = nullptr;
        Bar              bar;
        float            leftFraction = 0.55f;
        friend class Bar;
    };

    EditorPane      editor;
    DiagnosticsPane diagnostics;
    Splitter        splitter;
};

MainWindow::MainWindow()
    : juce::DocumentWindow ("LOTRO ABC Converter UI",
                            juce::Colours::lightgrey,
                            juce::DocumentWindow::allButtons),
      body (std::make_unique<Body>()),
      menuBar (std::make_unique<juce::MenuBarComponent> (this))
{
    setUsingNativeTitleBar (true);
    setResizable (true, true);
    centreWithSize (1000, 700);

    auto host = std::make_unique<juce::Component>();
    host->addAndMakeVisible (*menuBar);
    host->addAndMakeVisible (*body);
    auto* hostRaw = host.release();
    setContentOwned (hostRaw, false);
    hostRaw->setSize (1000, 700);
    menuBar->setBounds (0, 0, hostRaw->getWidth(), 24);
    body->setBounds (0, 24, hostRaw->getWidth(), hostRaw->getHeight() - 24);

    setVisible (true);
}

MainWindow::~MainWindow() = default;

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

juce::StringArray MainWindow::getMenuBarNames() { return { "File", "Edit" }; }

juce::PopupMenu MainWindow::getMenuForIndex (int topLevelMenuIndex, const juce::String&)
{
    juce::PopupMenu m;
    if (topLevelMenuIndex == 0)
    {
        m.addItem (FileOpenMidi,    "Open MIDI...",    true, false);
        m.addItem (FileOpenConfig,  "Open Config...",  true, false);
        m.addSeparator();
        m.addItem (FileSaveConfig,  "Save Config",     false, false);
        juce::PopupMenu saveAs;
        saveAs.addItem (FileSaveAsJson, "JSON (.json)", true, false);
        saveAs.addItem (FileSaveAsToml, "TOML (.toml)", true, false);
        saveAs.addItem (FileSaveAsXml,  "XML (.xml)",   true, false);
        m.addSubMenu ("Save Config As", saveAs);
        m.addSeparator();
        m.addItem (FileQuit, "Quit");
    }
    else if (topLevelMenuIndex == 1)
    {
        m.addItem (EditAddInstrument,    "Add Instrument",    false, false);
        m.addItem (EditRemoveSelected,   "Remove Selected",   false, false);
    }
    return m;
}

void MainWindow::menuItemSelected (int menuItemID, int)
{
    switch (menuItemID)
    {
        case FileOpenMidi:  openMidiViaDialog();                                           return;
        case FileQuit:      juce::JUCEApplication::getInstance()->systemRequestedQuit();   return;
        default:                                                                            return;
    }
}

bool MainWindow::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        const auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".mid" || ext == ".midi") return true;
    }
    return false;
}

void MainWindow::filesDropped (const juce::StringArray& files, int, int)
{
    for (const auto& f : files)
    {
        const auto file = juce::File (f);
        const auto ext = file.getFileExtension().toLowerCase();
        if (ext == ".mid" || ext == ".midi")
        {
            openMidiFromPath (file);
            return;
        }
    }
}

void MainWindow::openMidiViaDialog()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Choose a MIDI file", juce::File(), "*.mid;*.midi");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == juce::File()) return;
            openMidiFromPath (file);
        });
}

void MainWindow::openMidiFromPath (const juce::File& file)
{
    std::ifstream stream (file.getFullPathName().toStdString(), std::ios::binary);
    if (! stream)
    {
        juce::NativeMessageBox::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon,
            "Open MIDI failed", "Could not read: " + file.getFullPathName());
        return;
    }

    Diagnostics importDiags;
    Song raw;
    try
    {
        raw = importMidi (stream, file.getFileNameWithoutExtension().toStdString(), importDiags);
    }
    catch (const std::exception& e)
    {
        juce::NativeMessageBox::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon,
            "Open MIDI failed", juce::String (e.what()));
        return;
    }

    auto cfg = synthesiseConfig (raw,
        file.getFullPathName().toStdString(),
        file.withFileExtension (".abc").getFullPathName().toStdString(),
        std::nullopt, 0, {});

    body->getEditor().loadFromMidi (std::move (raw), std::move (cfg));
}

} // namespace lotro
```

- [ ] **Step 3: Build and launch**

```bash
cmake --build build && build/converter_ui_artefacts/Debug/converter_ui &
```

Expected:
- File → Open MIDI… opens a file chooser. Pick `midi/Barnes Brothers Band - Pull The Wires.mid`.
- After selecting: globals populate (input path, output path), instruments table fills with 10+ rows (one per MIDI track), each row's auto-picked instrument visible.
- Click a row: detail form populates. Sources section shows checkboxes for every MIDI track in the file with names and note counts.
- Drag-drop the same MIDI on the window: same behaviour as Open MIDI.

- [ ] **Step 4: Commit**

```bash
git add Source/UI/MainWindow.h Source/UI/MainWindow.cpp
git commit -m "$(cat <<'EOF'
feat(ui): Open MIDI menu + drag-drop populate the editor

File → Open MIDI launches a juce::FileChooser; on selection the file
is parsed via MidiImporter, a starter Config is synthesised, and both
flow into EditorPane::loadFromMidi. Drag-drop of .mid/.midi files
hits the same path. Failures pop a NativeMessageBox.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Phase D — Diagnostics pane

### Task 11: `DiagnosticListView` and `AbcPreviewView`

**Files:**
- Create: `Source/UI/DiagnosticListView.h` / `.cpp`
- Create: `Source/UI/AbcPreviewView.h` / `.cpp`
- Modify: `Source/UI/DiagnosticsPane.h` / `.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write DiagnosticListView**

`Source/UI/DiagnosticListView.h`:

```cpp
#pragma once

#include "Core/Diagnostics.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace lotro
{

class DiagnosticListView : public juce::Component,
                           private juce::TableListBoxModel
{
public:
    DiagnosticListView();

    void setDiagnostics (Diagnostics newDiags);

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    int  getNumRows() override;
    void paintRowBackground (juce::Graphics&, int, int, int, bool) override;
    void paintCell (juce::Graphics&, int rowNumber, int columnId,
                    int width, int height, bool) override;

    Diagnostics        diagnostics;
    juce::TableListBox table;
};

} // namespace lotro
```

`Source/UI/DiagnosticListView.cpp`:

```cpp
#include "DiagnosticListView.h"

namespace lotro
{

DiagnosticListView::DiagnosticListView()
{
    table.setModel (this);
    table.getHeader().addColumn ("Severity", 1, 80);
    table.getHeader().addColumn ("Source",   2, 130);
    table.getHeader().addColumn ("Tick",     3, 70);
    table.getHeader().addColumn ("Pitch",    4, 60);
    table.getHeader().addColumn ("Track",    5, 60);
    table.getHeader().addColumn ("Message",  6, 400);
    addAndMakeVisible (table);
}

void DiagnosticListView::setDiagnostics (Diagnostics newDiags)
{
    diagnostics = std::move (newDiags);
    table.updateContent();
    table.repaint();
}

int DiagnosticListView::getNumRows() { return (int) diagnostics.size(); }

void DiagnosticListView::paintRowBackground (juce::Graphics& g, int, int, int, bool selected)
{
    g.fillAll (selected ? juce::Colours::lightblue : juce::Colours::white);
}

void DiagnosticListView::paintCell (juce::Graphics& g, int rowNumber, int columnId,
                                    int width, int height, bool)
{
    if (rowNumber < 0 || rowNumber >= (int) diagnostics.size()) return;
    const auto& d = diagnostics[(size_t) rowNumber];
    g.setColour (juce::Colours::black);
    g.setFont (13.0f);

    auto severityToString = [] (Severity s) -> juce::String
    {
        switch (s) { case Severity::Info: return "Info";
                     case Severity::Warning: return "Warning";
                     case Severity::Error: return "Error"; }
        return "?";
    };

    auto severityColour = [] (Severity s) -> juce::Colour
    {
        switch (s) { case Severity::Info: return juce::Colours::cornflowerblue;
                     case Severity::Warning: return juce::Colours::orange;
                     case Severity::Error: return juce::Colours::red; }
        return juce::Colours::grey;
    };

    juce::String text;
    switch (columnId)
    {
        case 1:
            g.setColour (severityColour (d.severity));
            g.fillEllipse ((float) (4), (float) (height / 2 - 4), 8.0f, 8.0f);
            g.setColour (juce::Colours::black);
            text = severityToString (d.severity);
            g.drawText (text, 18, 0, width - 22, height, juce::Justification::centredLeft);
            return;
        case 2: text = juce::String (d.source); break;
        case 3: text = d.tick  >= 0 ? juce::String (d.tick)  : juce::String ("--"); break;
        case 4: text = d.pitch >= 0 ? juce::String (d.pitch) : juce::String ("--"); break;
        case 5: text = d.trackIndex >= 0 ? juce::String (d.trackIndex) : juce::String ("--"); break;
        case 6: text = juce::String (d.message); break;
    }
    g.drawText (text, 4, 0, width - 8, height, juce::Justification::centredLeft);
}

void DiagnosticListView::resized() { table.setBounds (getLocalBounds()); }

void DiagnosticListView::paint (juce::Graphics& g)
{
    if (diagnostics.empty())
    {
        g.fillAll (juce::Colours::white);
        g.setColour (juce::Colours::darkgrey);
        g.setFont (14.0f);
        g.drawText ("No diagnostics from the last run.",
                    getLocalBounds(), juce::Justification::centred);
    }
}

} // namespace lotro
```

- [ ] **Step 2: Write AbcPreviewView**

`Source/UI/AbcPreviewView.h`:

```cpp
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <string>

namespace lotro
{

class AbcPreviewView : public juce::Component
{
public:
    AbcPreviewView();

    void setAbc (const std::string& abc);

    void resized() override;

private:
    void updateStatusLine (const std::string& abc);

    juce::TextEditor editor;
    juce::Label      statusLine;
};

} // namespace lotro
```

`Source/UI/AbcPreviewView.cpp`:

```cpp
#include "AbcPreviewView.h"

namespace lotro
{

AbcPreviewView::AbcPreviewView()
{
    editor.setMultiLine (true);
    editor.setReadOnly (true);
    editor.setReturnKeyStartsNewLine (true);
    editor.setScrollbarsShown (true);
    editor.setCaretVisible (false);
    editor.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    addAndMakeVisible (editor);

    addAndMakeVisible (statusLine);
    statusLine.setColour (juce::Label::textColourId, juce::Colours::darkgrey);
    statusLine.setFont (12.0f);
    statusLine.setText ("(no output)", juce::dontSendNotification);
}

void AbcPreviewView::setAbc (const std::string& abc)
{
    editor.setText (juce::String (abc), false);
    updateStatusLine (abc);
}

void AbcPreviewView::updateStatusLine (const std::string& abc)
{
    auto count = [&] (const std::string& needle)
    {
        int n = 0;
        size_t p = 0;
        while ((p = abc.find (needle, p)) != std::string::npos) { ++n; p += needle.size(); }
        return n;
    };
    const int parts = count ("X:");
    const int bars  = count ("% bar");
    juce::String s = juce::String ((int) abc.size()) + " bytes  ·  "
                   + juce::String (bars)  + " bars  ·  "
                   + juce::String (parts) + " parts";
    statusLine.setText (s, juce::dontSendNotification);
}

void AbcPreviewView::resized()
{
    auto area = getLocalBounds();
    statusLine.setBounds (area.removeFromBottom (20));
    editor.setBounds (area);
}

} // namespace lotro
```

- [ ] **Step 3: Wire DiagnosticsPane**

`Source/UI/DiagnosticsPane.h`:

```cpp
#pragma once

#include "DiagnosticListView.h"
#include "AbcPreviewView.h"

#include "Core/Diagnostics.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <string>

namespace lotro
{

class DiagnosticsPane : public juce::Component
{
public:
    DiagnosticsPane();
    ~DiagnosticsPane() override;

    void show (Diagnostics diagnostics, std::string abc);

    void resized() override;

private:
    std::unique_ptr<DiagnosticListView> diagList;
    std::unique_ptr<AbcPreviewView>     abcView;
};

} // namespace lotro
```

`Source/UI/DiagnosticsPane.cpp`:

```cpp
#include "DiagnosticsPane.h"

namespace lotro
{

DiagnosticsPane::DiagnosticsPane()
    : diagList (std::make_unique<DiagnosticListView>()),
      abcView  (std::make_unique<AbcPreviewView>())
{
    addAndMakeVisible (*diagList);
    addAndMakeVisible (*abcView);
}

DiagnosticsPane::~DiagnosticsPane() = default;

void DiagnosticsPane::show (Diagnostics diagnostics, std::string abc)
{
    diagList->setDiagnostics (std::move (diagnostics));
    abcView->setAbc (abc);
}

void DiagnosticsPane::resized()
{
    auto area = getLocalBounds().reduced (4);
    auto top = area.removeFromTop (area.getHeight() / 2);
    diagList->setBounds (top);
    area.removeFromTop (4);
    abcView->setBounds (area);
}

} // namespace lotro
```

- [ ] **Step 4: Add new sources to CMakeLists.txt**

```cmake
    Source/UI/DiagnosticListView.cpp
    Source/UI/AbcPreviewView.cpp
```

- [ ] **Step 5: Build and launch**

```bash
cmake --build build && build/converter_ui_artefacts/Debug/converter_ui &
```

Expected: right pane shows two stacked sub-areas. The top displays "No diagnostics from the last run." (centered, gray). The bottom shows an empty monospaced text editor with the "(no output)" status line at the bottom.

- [ ] **Step 6: Commit**

```bash
git add Source/UI/DiagnosticListView.h Source/UI/DiagnosticListView.cpp \
        Source/UI/AbcPreviewView.h Source/UI/AbcPreviewView.cpp \
        Source/UI/DiagnosticsPane.h Source/UI/DiagnosticsPane.cpp \
        CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(ui): DiagnosticListView and AbcPreviewView

Right pane now contains two sub-views: a TableListBox of Diagnostic
rows (severity dot, source, tick, pitch, track, message) and a
read-only monospaced ABC viewer with a status line showing bytes,
bar count, and part count. DiagnosticsPane.show(diagnostics, abc)
replaces both views' contents on each Run.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 12: Run Converter button + in-process pipeline

**Files:**
- Modify: `Source/UI/EditorPane.h` / `.cpp`
- Modify: `Source/UI/MainWindow.h` / `.cpp`

- [ ] **Step 1: Add Run button to EditorPane and a callback for MainWindow**

Edit `Source/UI/EditorPane.h`. Add:

```cpp
public:
    // ... existing methods ...
    std::function<void()> onRunRequested;

private:
    // ... existing members ...
    juce::TextButton runButton { "Run Converter" };
```

Edit `Source/UI/EditorPane.cpp`. Inside the constructor, after the sub-views are created:

```cpp
    addAndMakeVisible (runButton);
    runButton.onClick = [this] { if (onRunRequested) onRunRequested(); };
```

Update `resized()` to lay out the Run button at the bottom of the editor pane:

```cpp
void EditorPane::resized()
{
    auto area = getLocalBounds().reduced (8);
    globalView->setBounds (area.removeFromTop (220));
    area.removeFromTop (8);
    instrumentsTable->setBounds (area.removeFromTop (200));
    area.removeFromTop (8);
    runButton.setBounds (area.removeFromBottom (32));
    area.removeFromBottom (8);
    detailForm->setBounds (area);
}
```

- [ ] **Step 2: Wire Run in MainWindow**

In `Source/UI/MainWindow.cpp` constructor body, after `setVisible (true);`, add:

```cpp
    body->getEditor().onRunRequested = [this] { runConversion(); };
```

In `Source/UI/MainWindow.h`, add a private declaration:

```cpp
    void runConversion();
```

In `Source/UI/MainWindow.cpp`, add the method:

```cpp
#include "Core/InstrumentAssembly.h"
#include "Core/AbcWriter.h"
#include "Core/Pipeline.h"

void MainWindow::runConversion()
{
    auto& editor = body->getEditor();
    const auto& cfg = editor.getConfig();
    const auto& raw = editor.getRawSong();

    Diagnostics diagnostics;

    const auto validErr = validateConfig (cfg, (int) raw.tracks.size());
    if (! validErr.empty())
    {
        Diagnostic err;
        err.severity = Severity::Error;
        err.source   = "Config";
        err.message  = validErr;
        diagnostics.push_back (err);
        body->getDiagnostics().show (std::move (diagnostics), {});
        return;
    }

    Song assembled;
    std::string abc;
    try
    {
        assembled = assembleInstruments (raw, cfg, diagnostics);
        runPipeline (assembled, diagnostics);
        abc = writeAbc (assembled);
    }
    catch (const std::exception& e)
    {
        Diagnostic err;
        err.severity = Severity::Error;
        err.source   = "Pipeline";
        err.message  = e.what();
        diagnostics.push_back (err);
        body->getDiagnostics().show (std::move (diagnostics), {});
        return;
    }

    body->getDiagnostics().show (std::move (diagnostics), std::move (abc));
}
```

Also add a `validateConfig` include (it's in `Core/Config.h` already; if missing, include it).

- [ ] **Step 3: Build and launch**

```bash
cmake --build build && build/converter_ui_artefacts/Debug/converter_ui &
```

Expected:
- Editor pane has a "Run Converter" button at the bottom.
- Open `midi/Barnes Brothers Band - Pull The Wires.mid`.
- Click Run Converter.
- Diagnostics list populates (mostly Info entries about unreferenced tracks if you removed any from the table; might be empty on a default config).
- ABC preview fills with the converted output, status line shows real bytes/bars/parts.
- Tweak the volumePercent on an instrument, click Run again — diagnostics list and ABC preview replace.

- [ ] **Step 4: Commit**

```bash
git add Source/UI/EditorPane.h Source/UI/EditorPane.cpp \
        Source/UI/MainWindow.h Source/UI/MainWindow.cpp
git commit -m "$(cat <<'EOF'
feat(ui): Run Converter button drives the in-process pipeline

EditorPane gains a Run Converter button that invokes a MainWindow
callback. The callback runs validateConfig → assembleInstruments →
runPipeline → writeAbc directly on the in-memory Config and Song,
then hands the Diagnostic vector + ABC text to DiagnosticsPane.show.
Validation errors and pipeline exceptions are surfaced as single
Error diagnostic rows (no crash on bad input).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Phase E — Save / Open Config

### Task 13: File → Save Config As (JSON / TOML / XML)

**Files:**
- Modify: `Source/UI/MainWindow.h` / `.cpp`

- [ ] **Step 1: Add saveConfigAs to MainWindow**

In `Source/UI/MainWindow.h`, declare:

```cpp
    void saveConfigAs (ConfigFormat format);
```

(Add `#include "Core/ConfigLoader.h"` so the `ConfigFormat` enum is visible.)

In `Source/UI/MainWindow.cpp`, add include:

```cpp
#include "Core/ConfigWriter.h"
```

Then implement:

```cpp
void MainWindow::saveConfigAs (ConfigFormat format)
{
    const auto& cfg = body->getEditor().getConfig();

    juce::String ext = (format == ConfigFormat::Json) ? ".json"
                     : (format == ConfigFormat::Toml) ? ".toml"
                     :                                    ".xml";

    fileChooser = std::make_unique<juce::FileChooser> (
        "Save Config", juce::File(), "*" + ext);

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                            | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, format, ext, &cfg] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File()) return;
            if (! file.getFileName().endsWithIgnoreCase (ext))
                file = file.withFileExtension (ext);

            const auto err = writeConfigToFile (file.getFullPathName().toStdString(),
                                                format, cfg);
            if (! err.empty())
            {
                juce::NativeMessageBox::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon,
                    "Save failed", juce::String (err));
            }
        });
}
```

Wire menu items in `menuItemSelected`:

```cpp
        case FileSaveAsJson: saveConfigAs (ConfigFormat::Json); return;
        case FileSaveAsToml: saveConfigAs (ConfigFormat::Toml); return;
        case FileSaveAsXml:  saveConfigAs (ConfigFormat::Xml);  return;
```

- [ ] **Step 2: Build and launch**

```bash
cmake --build build && build/converter_ui_artefacts/Debug/converter_ui &
```

Expected: open a MIDI, then File → Save Config As → JSON. Choose a path. Check the saved file:

```bash
cat /tmp/test-config.json | head -30
```

Repeat for TOML and XML.

- [ ] **Step 3: Commit**

```bash
git add Source/UI/MainWindow.h Source/UI/MainWindow.cpp
git commit -m "$(cat <<'EOF'
feat(ui): File → Save Config As menu items (JSON / TOML / XML)

Each Save As submenu item opens a juce::FileChooser pre-filtered to
the chosen extension, and writes via ConfigWriter::writeConfigToFile.
Format is determined by the menu item, not the typed extension. Any
write failure pops a NativeMessageBox.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 14: File → Open Config

**Files:**
- Modify: `Source/UI/MainWindow.h` / `.cpp`

- [ ] **Step 1: Add openConfigViaDialog**

In `Source/UI/MainWindow.h`, declare:

```cpp
    void openConfigViaDialog();
    void openConfigFromPath (const juce::File& file);
```

In `Source/UI/MainWindow.cpp`, implement:

```cpp
void MainWindow::openConfigViaDialog()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Choose a Config", juce::File(), "*.json;*.toml;*.xml");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == juce::File()) return;
            openConfigFromPath (file);
        });
}

void MainWindow::openConfigFromPath (const juce::File& file)
{
    Config cfg;
    const auto loadErr = loadConfigFromFile (file.getFullPathName().toStdString(),
                                             ConfigFormat::Auto, cfg);
    if (! loadErr.empty())
    {
        juce::NativeMessageBox::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon,
            "Open Config failed", juce::String (loadErr));
        return;
    }

    if (cfg.input.empty())
    {
        juce::NativeMessageBox::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon,
            "Open Config failed",
            "Config has no 'input' field — cannot load the referenced MIDI.");
        return;
    }

    const auto configDir  = file.getParentDirectory();
    const auto midiFile   = configDir.getChildFile (juce::String (cfg.input));

    std::ifstream stream (midiFile.getFullPathName().toStdString(), std::ios::binary);
    if (! stream)
    {
        juce::NativeMessageBox::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon,
            "Open Config failed",
            "Could not read MIDI referenced by the config: " + midiFile.getFullPathName());
        return;
    }

    Diagnostics importDiags;
    Song raw;
    try
    {
        raw = importMidi (stream, midiFile.getFileNameWithoutExtension().toStdString(), importDiags);
    }
    catch (const std::exception& e)
    {
        juce::NativeMessageBox::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon,
            "Open Config failed", juce::String (e.what()));
        return;
    }

    body->getEditor().loadFromMidi (std::move (raw), std::move (cfg));
}
```

Wire it in `menuItemSelected`:

```cpp
        case FileOpenConfig: openConfigViaDialog(); return;
```

Extend `isInterestedInFileDrag` and `filesDropped` to accept config files too:

```cpp
bool MainWindow::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        const auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".mid" || ext == ".midi") return true;
        if (ext == ".json" || ext == ".toml" || ext == ".xml") return true;
    }
    return false;
}

void MainWindow::filesDropped (const juce::StringArray& files, int, int)
{
    for (const auto& f : files)
    {
        const auto file = juce::File (f);
        const auto ext = file.getFileExtension().toLowerCase();
        if (ext == ".mid" || ext == ".midi") { openMidiFromPath (file);   return; }
        if (ext == ".json" || ext == ".toml" || ext == ".xml")
                                              { openConfigFromPath (file); return; }
    }
}
```

- [ ] **Step 2: Build and launch**

```bash
cmake --build build && build/converter_ui_artefacts/Debug/converter_ui &
```

Expected: File → Open Config… opens a chooser. Pick the JSON config you saved in Task 13. UI loads the referenced MIDI and the editor populates exactly as if you had opened the MIDI directly. Drag-drop a `.json` config — same thing.

- [ ] **Step 3: Commit**

```bash
git add Source/UI/MainWindow.h Source/UI/MainWindow.cpp
git commit -m "$(cat <<'EOF'
feat(ui): File → Open Config + drag-drop for config files

Open Config launches a FileChooser filtered to .json/.toml/.xml, loads
via ConfigLoader (auto-format), and follows the cfg.input path
relative to the config file's directory to load the MIDI. The editor
then populates as if the MIDI had been opened directly. Drag-drop is
extended to accept config files alongside MIDIs.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Phase F — Polish

### Task 15: Empty-state hint and Run-button gating

**Files:**
- Modify: `Source/UI/EditorPane.cpp`

- [ ] **Step 1: Disable Run when no MIDI is loaded; show a hint**

In `Source/UI/EditorPane.cpp`'s constructor, after creating sub-views, set the initial Run state:

```cpp
    runButton.setEnabled (false);
```

Edit `loadFromMidi` to enable Run after a MIDI is loaded:

```cpp
void EditorPane::loadFromMidi (Song newRaw, Config newCfg)
{
    raw    = std::move (newRaw);
    config = std::move (newCfg);
    globalView->refresh();
    instrumentsTable->refresh();
    detailForm->editInstrument (-1);
    runButton.setEnabled (! raw.tracks.empty());
    repaint();
    if (onConfigChanged) onConfigChanged();
}
```

Edit `paint` to draw an empty-state hint when there are no instruments AND no raw tracks:

```cpp
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
```

- [ ] **Step 2: Build and verify**

```bash
cmake --build build && build/converter_ui_artefacts/Debug/converter_ui &
```

Expected: on launch, the editor pane shows a centered "Drop a MIDI file here..." hint behind the (empty) sub-views, and Run Converter is disabled. Open a MIDI: hint disappears, Run enables.

- [ ] **Step 3: Commit**

```bash
git add Source/UI/EditorPane.cpp
git commit -m "$(cat <<'EOF'
feat(ui): empty-state hint and Run-button gating

Run Converter starts disabled and enables after a MIDI is loaded.
EditorPane paints an "open a MIDI" hint behind the empty sub-views
on launch.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Phase G — Documentation

### Task 16: Update CLAUDE.md

**Files:**
- Modify: `CLAUDE.md`

- [ ] **Step 1: Add a UI section to the Architecture description**

Open `CLAUDE.md`. Find the `### Build targets (CMakeLists.txt)` block and replace its diagram with:

```
converter_core   (static library)  ─────────────► converter_tests (Catch2 exe)
       ▲                                          ▲
       │ depends on                               │
       ├──────────────────────────────────────────┤
       │                                          │
converter (CLI exe)                       converter_ui (JUCE GUI exe)
```

In the `### Source layout` block, after the existing `Cli/` entry, add:

```
├── UI/                          JUCE GUI app — converter_ui binary.
│   ├── UiMain.cpp               JUCE app entry point
│   ├── MainWindow.{h,cpp}       Window, menus, splitter, drag-drop
│   ├── EditorPane.{h,cpp}       Left pane container; owns Config + Song
│   ├── GlobalSettingsView.{h,cpp}    Top-of-editor form
│   ├── InstrumentsTable.{h,cpp}      Master view of instruments
│   ├── InstrumentDetailForm.{h,cpp}  Detail view for selected instrument
│   ├── DiagnosticsPane.{h,cpp}       Right pane container
│   ├── DiagnosticListView.{h,cpp}    Diagnostic table
│   └── AbcPreviewView.{h,cpp}        Read-only ABC text + status line
```

In the `## Target platform and toolchain` section, after the existing JUCE bullet, add:

```
- The `converter_ui` GUI binary additionally links `juce_gui_basics`
  and `juce_gui_extra`. Linux/WSL build needs system packages
  `libfreetype6-dev`, `libfontconfig1-dev`, `libx11-dev`,
  `libxrandr-dev`, `libxinerama-dev`, `libxcursor-dev`,
  `libasound2-dev`. The CLI build does not need these.
```

In the `## Build / test commands` section, mention the UI binary path after the `Converter binary:` line:

```
UI binary: `build/converter_ui_artefacts/Debug/converter_ui`.
```

Bump the test count in `## Testing notes` from `**121/121**` to the new total. After all phases land, this should be **127/127** (121 + 3 from Pipeline_tests + 3 from ConfigWriter_tests).

- [ ] **Step 2: Commit**

```bash
git add CLAUDE.md
git commit -m "$(cat <<'EOF'
docs: document the converter_ui binary in CLAUDE.md

Architecture diagram now lists converter_ui alongside converter and
converter_tests. Source layout adds the UI/ tree. Toolchain section
calls out the GUI dev packages required only for the UI build. Test
count bumped to 127/127.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Self-review notes

**Spec coverage:**

- Goals (testing-cohort tool, MIDI-first workflow, in-process Run, three save formats) → Tasks 5-15.
- Non-goals (no animations, no auto-update, no DAW features) → respected throughout.
- Architecture (split window, master-detail, in-process pipeline) → Tasks 6, 8-9, 12.
- Components (MainWindow, EditorPane, GlobalSettingsView, InstrumentsTable, InstrumentDetailForm, DiagnosticsPane, DiagnosticListView, AbcPreviewView) → Tasks 6-12.
- File operations (Open MIDI, Open Config, Save As, drag-drop) → Tasks 10, 13, 14.
- ConfigWriter for the three formats → Tasks 2-4.
- Empty state + Run gating → Task 15.
- Pipeline.h refactor → Task 1.
- CLAUDE.md sync → Task 16.

**Deferred per spec:**

- Dirty-bit + unsaved-changes warning (mentioned in spec, intentionally out of v0.1 scope per the testing-cohort calibration). If the user reports they actually need it, it lands as a follow-on task.
- Diagnostic filtering, run history, "save output ABC" file menu — all explicitly out of scope.

**Placeholder scan:** Each step contains the actual code, file paths, and commands. No "TBD" / "TODO" / "implement later".

**Type consistency:** `Config`, `ConfigInstrument`, `ConfigFormat`, `Song`, `Track`, `Note`, `Diagnostic`, `Diagnostics`, `Severity`, `LotroInstrument` — all referenced consistently across tasks. `synthesiseConfig` and `runPipeline` signatures match between Tasks 1 (declaration) and Tasks 10/12 (call sites). `writeConfigToString` / `writeConfigToFile` signatures match between Task 2 (declaration), Tasks 3-4 (use), and Task 13 (call site).

**Risks:**

- **Task 6's nested Splitter friend-class structure is fiddly.** If JUCE behaves unexpectedly (component-parent-cycle issues), simplify by replacing the bespoke splitter with `juce::StretchableLayoutManager` or `juce::ComponentBoundsConstrainer` patterns from JUCE examples.
- **Task 10's MIDI re-import on Run.** The `runConversion` callback uses the cached `EditorPane.raw` Song. If the user changes the MIDI file on disk between Open and Run, the run still uses the in-memory copy. That's intentional (matches CLI behaviour); flag if anyone reports confusion.
- **JUCE GUI on WSL** depends on a working X11 display server (WSLg). If `build/converter_ui_artefacts/Debug/converter_ui` errors with "no display" on first launch, the tester needs to confirm WSLg is configured. Out of scope for this plan.
- **Task 1 changes `synthesiseConfig`'s signature.** The `EndToEndConfig_tests.cpp` test does NOT call `synthesiseConfig` directly (it builds a Config inline), so it won't catch a regression in the extracted version. The new `Pipeline_tests.cpp` and the existing `EndToEnd_tests.cpp` (which DOES exercise the no-config path through Main.cpp) together cover the change.

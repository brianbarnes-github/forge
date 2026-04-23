# Config-driven conversion implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `--config PATH` mode to the converter CLI that drives per-instrument track selection, merging, labelling, volume scaling, transpose, drum-map override, and `X:` index assignment, supporting JSON / TOML / XML formats. Existing no-config CLI behaviour preserved bit-for-bit.

**Architecture:** New `Config` data model + validator + format-agnostic loader. An `InstrumentAssembly` pass converts a raw MIDI `Song` plus a `Config` into a post-merge `Song` whose `Track`s already have per-instrument transpose and volume applied. Existing constraint pipeline and `AbcWriter` operate on the assembled `Song` unchanged, except `AbcWriter` now emits parts in `x`-ascending order and consumes optional per-`Track` drum maps.

**Tech Stack:** C++17, JUCE (`juce::JSON`, `juce::XmlDocument`), Catch2, CMake, Ninja. TOML via vendored single-header `toml++`. All new public Core headers stay JUCE-free.

**Spec reference:** `docs/superpowers/specs/2026-04-23-config-driven-conversion-design.md`

---

## File plan

### New files (Phase 1)

| Path | Responsibility |
|---|---|
| `Source/Core/Config.h` | `Config`, `ConfigInstrument` POD structs; validator declaration |
| `Source/Core/Config.cpp` | `validateConfig(config, midiTrackCount) → optional<string>` |
| `Source/Core/ConfigLoader.h` | `ConfigFormat` enum, `loadConfigFromFile(path, format) → variant<Config, string>` |
| `Source/Core/ConfigLoader.cpp` | Format dispatch; Phase-1 JSON parser using `juce::JSON` |
| `Source/Core/InstrumentAssembly.h` | `assembleInstruments(raw, config, diagnostics) → Song` declaration |
| `Source/Core/InstrumentAssembly.cpp` | Merging, per-instrument transpose+volume, drum-map attachment, silently-dropped-track diagnostics |
| `Tests/Config_tests.cpp` | Validation-rule unit tests |
| `Tests/ConfigLoader_tests.cpp` | JSON round-trip + malformed-input tests |
| `Tests/InstrumentAssembly_tests.cpp` | Merge / transpose / volume / drumMap / drop-track tests |
| `Tests/ConfigPipeline_tests.cpp` | End-to-end fixture-driven tests |

### New files (Phase 2)

| Path | Responsibility |
|---|---|
| `Source/ThirdParty/tomlplusplus/toml.hpp` | Vendored `toml++` single-header (drop-in) |
| `Tests/ConfigLoaderXml_tests.cpp` | XML format round-trip |
| `Tests/ConfigLoaderToml_tests.cpp` | TOML format round-trip |

### Modified files

| Path | Change |
|---|---|
| `Source/Core/Track.h` | Add `int x = 0;` (0 = auto-assign); add `std::optional<DrumMap> drumMap;` |
| `Source/Cli/CliOptions.h` | Add `juce::File configFile;` and `juce::String configFormat;` |
| `Source/Cli/CliOptions.cpp` | Parse `--config`, `--config-format`; update usage text |
| `Source/Main.cpp` | Branch on config presence; add synthetic-config path; route through `assembleInstruments` |
| `Source/Core/AbcWriter.cpp` | Iterate tracks in `x`-ascending order; consume `track.drumMap` if set else `song.drumMap`; use `track.name` for `T:` suffix (already does; assembler populates it with label) |
| `CMakeLists.txt` | Add `Config.cpp`, `ConfigLoader.cpp`, `InstrumentAssembly.cpp` to `converter_core` sources |
| `Tests/CMakeLists.txt` | Add new test files |

---

## Phase 1 — JSON config drives the conversion end to end

### Task 1: Add `Track::x` and `Track::drumMap` fields

**Files:**
- Modify: `Source/Core/Track.h`
- Test: none yet (trivial struct change; covered by later tasks' integration tests)

- [ ] **Step 1: Add fields to Track struct**

Edit `Source/Core/Track.h` — inside the `struct Track` body, after `int sourceMidiChannel = 0;`:

```cpp
#include "DrumMap.h"
#include <optional>
```

Add the include at the top of the file if not already present (needed for `DrumMap` and `std::optional`). Then add the two fields:

```cpp
    // ABC X: index. 0 means "auto-assign in emission order" (no-config or
    // legacy path); nonzero means the user picked this specific value via
    // Config. Writer sorts tracks ascending by x before emission.
    int x = 0;

    // Optional per-track drum-map override. Populated by InstrumentAssembly
    // for Drums instruments that have a drumMap path in their ConfigInstrument.
    // Empty means "fall back to Song::drumMap at emission time."
    std::optional<DrumMap> drumMap;
```

- [ ] **Step 2: Build to verify headers still compile**

Run: `cmake --build build 2>&1 | tail -5`
Expected: clean build, 0 errors.

- [ ] **Step 3: Run full test suite to verify no regression**

Run: `ctest --test-dir build 2>&1 | tail -3`
Expected: `76/76` tests pass.

- [ ] **Step 4: Commit**

```bash
git add Source/Core/Track.h
git commit -m "feat(track): add optional x index and per-track drum map"
```

---

### Task 2: Define `Config` and `ConfigInstrument` structs

**Files:**
- Create: `Source/Core/Config.h`

- [ ] **Step 1: Write Config.h**

Create `Source/Core/Config.h`:

```cpp
#pragma once

#include "LotroInstrument.h"

#include <optional>
#include <string>
#include <vector>

namespace lotro
{

struct ConfigInstrument
{
    int                                   x = 0;
    std::string                           name;               // LOTRO enum identifier, e.g. "LuteOfAges"
    std::optional<std::string>            label;              // T: header suffix; fallback in assembler
    std::vector<int>                      sources;            // MIDI track indices (0-based)
    int                                   transposeSemitones = 0;
    int                                   volumePercent      = 100;
    std::optional<std::string>            drumMap;            // path to drum-map JSON; only valid when name == "Drums"
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

} // namespace lotro
```

- [ ] **Step 2: Register new source file with CMake**

Edit `CMakeLists.txt` — inside `add_library(converter_core STATIC ...)`, add `Source/Core/Config.cpp` alongside other sources (order doesn't matter for CMake; put it after `DrumMap.cpp` alphabetically):

```cmake
    Source/Core/Config.cpp
```

The `.cpp` file doesn't exist yet — Task 3 creates it. CMake will complain until then. That's fine; we build in Task 3.

- [ ] **Step 3: Commit**

Header-only change plus CMake prep. Build verification happens in Task 3 once the `.cpp` exists.

```bash
git add Source/Core/Config.h CMakeLists.txt
git commit -m "feat(config): add Config and ConfigInstrument POD structs"
```

---

### Task 3: Config validator (one rule per test)

**Files:**
- Create: `Source/Core/Config.cpp`
- Create: `Tests/Config_tests.cpp`
- Modify: `Source/Core/Config.h` (add validator declaration)
- Modify: `Tests/CMakeLists.txt` (register new test file)

- [ ] **Step 1: Add validator declaration to Config.h**

Append to `Source/Core/Config.h` inside the `namespace lotro` block, after the struct definitions:

```cpp
// Validates a Config against a MIDI file with `midiTrackCount` tracks.
// Returns an empty string on success; otherwise a human-readable error
// message suitable for stderr. All rules are checked eagerly — the
// message reports the first failure.
std::string validateConfig (const Config& config, int midiTrackCount);
```

- [ ] **Step 2: Write failing tests**

Create `Tests/Config_tests.cpp`:

```cpp
#include "Core/Config.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
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
}

TEST_CASE ("config: minimal valid config passes", "[config][validate]")
{
    auto config = minimalValid();
    CHECK (lotro::validateConfig (config, /*midiTrackCount=*/1).empty());
}

TEST_CASE ("config: empty instruments array fails", "[config][validate]")
{
    lotro::Config c;
    c.input = "song.mid";
    const auto err = lotro::validateConfig (c, 1);
    CHECK_FALSE (err.empty());
    CHECK (err.find ("instruments") != std::string::npos);
}

TEST_CASE ("config: empty input path fails", "[config][validate]")
{
    auto config = minimalValid();
    config.input.clear();
    const auto err = lotro::validateConfig (config, 1);
    CHECK_FALSE (err.empty());
    CHECK (err.find ("input") != std::string::npos);
}

TEST_CASE ("config: duplicate x values fail", "[config][validate]")
{
    auto config = minimalValid();
    lotro::ConfigInstrument dup;
    dup.x       = 1;                // same as the first
    dup.name    = "Harp";
    dup.sources = { 1 };
    config.instruments.push_back (dup);
    const auto err = lotro::validateConfig (config, 2);
    CHECK_FALSE (err.empty());
    CHECK (err.find ("duplicate") != std::string::npos);
}

TEST_CASE ("config: x < 1 fails", "[config][validate]")
{
    auto config = minimalValid();
    config.instruments[0].x = 0;
    const auto err = lotro::validateConfig (config, 1);
    CHECK_FALSE (err.empty());
    CHECK (err.find ("x") != std::string::npos);
}

TEST_CASE ("config: unknown instrument name fails", "[config][validate]")
{
    auto config = minimalValid();
    config.instruments[0].name = "NotARealInstrument";
    const auto err = lotro::validateConfig (config, 1);
    CHECK_FALSE (err.empty());
    CHECK (err.find ("NotARealInstrument") != std::string::npos);
}

TEST_CASE ("config: empty sources fails", "[config][validate]")
{
    auto config = minimalValid();
    config.instruments[0].sources.clear();
    const auto err = lotro::validateConfig (config, 1);
    CHECK_FALSE (err.empty());
    CHECK (err.find ("sources") != std::string::npos);
}

TEST_CASE ("config: source index negative fails", "[config][validate]")
{
    auto config = minimalValid();
    config.instruments[0].sources = { -1 };
    const auto err = lotro::validateConfig (config, 1);
    CHECK_FALSE (err.empty());
    CHECK (err.find ("source") != std::string::npos);
}

TEST_CASE ("config: source index out of range fails", "[config][validate]")
{
    auto config = minimalValid();
    config.instruments[0].sources = { 5 };           // midiTrackCount is 1
    const auto err = lotro::validateConfig (config, 1);
    CHECK_FALSE (err.empty());
    CHECK (err.find ("source") != std::string::npos);
}

TEST_CASE ("config: non-positive volumePercent fails", "[config][validate]")
{
    auto config = minimalValid();
    config.instruments[0].volumePercent = 0;
    const auto err = lotro::validateConfig (config, 1);
    CHECK_FALSE (err.empty());
    CHECK (err.find ("volumePercent") != std::string::npos);
}

TEST_CASE ("config: drumMap on non-Drums instrument fails", "[config][validate]")
{
    auto config = minimalValid();
    config.instruments[0].drumMap = "kit.json";
    const auto err = lotro::validateConfig (config, 1);
    CHECK_FALSE (err.empty());
    CHECK (err.find ("drumMap") != std::string::npos);
}

TEST_CASE ("config: drumMap on Drums instrument passes", "[config][validate]")
{
    auto config = minimalValid (/*x=*/1, /*name=*/"Drums");
    config.instruments[0].drumMap = "kit.json";
    CHECK (lotro::validateConfig (config, 1).empty());
}

TEST_CASE ("config: gaps in x are allowed", "[config][validate]")
{
    lotro::Config c;
    c.input = "song.mid";
    for (int x : {1, 3, 7})
    {
        lotro::ConfigInstrument inst;
        inst.x       = x;
        inst.name    = "LuteOfAges";
        inst.sources = { 0 };
        c.instruments.push_back (inst);
    }
    CHECK (lotro::validateConfig (c, 1).empty());
}
```

- [ ] **Step 3: Register test file in Tests/CMakeLists.txt**

Edit `Tests/CMakeLists.txt` — inside `add_executable(converter_tests ...)`, add alphabetically:

```cmake
    Config_tests.cpp
```

- [ ] **Step 4: Run tests to confirm they fail (validateConfig not implemented)**

Run: `cmake --build build 2>&1 | tail -10`
Expected: link errors about `validateConfig` being undefined.

- [ ] **Step 5: Implement validator**

Create `Source/Core/Config.cpp`:

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

        for (int src : inst.sources)
        {
            if (src < 0)
                return where + "source index " + std::to_string (src) + " is negative";
            if (src >= midiTrackCount)
                return where + "source index " + std::to_string (src)
                     + " exceeds MIDI track count (" + std::to_string (midiTrackCount) + ")";
        }

        if (inst.volumePercent <= 0)
            return where + "'volumePercent' must be positive (got "
                 + std::to_string (inst.volumePercent) + ")";

        if (inst.drumMap.has_value() && parsed != LotroInstrument::Drums)
            return where + "'drumMap' is only valid on name == \"Drums\"";
    }

    return {};
}

} // namespace lotro
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build build && ctest --test-dir build -R "config:" 2>&1 | tail -20`
Expected: all 12 config tests pass.

- [ ] **Step 7: Run full suite to confirm no regression**

Run: `ctest --test-dir build 2>&1 | tail -3`
Expected: `88/88` passing (was 76, +12 config tests).

- [ ] **Step 8: Commit**

```bash
git add Source/Core/Config.cpp Tests/Config_tests.cpp Tests/CMakeLists.txt
git commit -m "feat(config): validate Config against MIDI track count"
```

---

### Task 4: JSON config loader

**Files:**
- Create: `Source/Core/ConfigLoader.h`
- Create: `Source/Core/ConfigLoader.cpp`
- Create: `Tests/ConfigLoader_tests.cpp`
- Modify: `CMakeLists.txt`
- Modify: `Tests/CMakeLists.txt`

- [ ] **Step 1: Write ConfigLoader.h**

Create `Source/Core/ConfigLoader.h`:

```cpp
#pragma once

#include "Config.h"

#include <string>
#include <string_view>

namespace lotro
{

enum class ConfigFormat
{
    Auto,       // detect by file extension
    Json,
    Toml,       // Phase 2
    Xml         // Phase 2
};

// Loads and parses a config file. Validation is NOT performed here;
// call validateConfig() after a successful load.
// Returns empty error string on success. On failure, `config` is left
// in an unspecified state and the returned string describes the error.
std::string loadConfigFromFile (const std::string& path,
                                ConfigFormat       format,
                                Config&            config);

// Parse a config from a text buffer rather than a file. Used by tests
// and (potentially) future stdin support.
std::string loadConfigFromString (std::string_view text,
                                  ConfigFormat     format,
                                  Config&          config);

} // namespace lotro
```

- [ ] **Step 2: Write failing JSON tests**

Create `Tests/ConfigLoader_tests.cpp`:

```cpp
#include "Core/ConfigLoader.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE ("config-loader: JSON minimal config parses", "[config-loader][json]")
{
    const std::string text = R"({
        "input": "song.mid",
        "instruments": [
            { "x": 1, "name": "LuteOfAges", "sources": [0] }
        ]
    })";

    lotro::Config config;
    const auto err = lotro::loadConfigFromString (text, lotro::ConfigFormat::Json, config);
    REQUIRE (err.empty());
    CHECK (config.input == "song.mid");
    REQUIRE (config.instruments.size() == 1);
    CHECK (config.instruments[0].x       == 1);
    CHECK (config.instruments[0].name    == "LuteOfAges");
    CHECK (config.instruments[0].sources == std::vector<int>{ 0 });
}

TEST_CASE ("config-loader: JSON full config round-trips all fields", "[config-loader][json]")
{
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
                "sources": [0, 2],
                "transposeSemitones": -12,
                "volumePercent": 110
            },
            {
                "x": 3,
                "name": "Drums",
                "sources": [9],
                "drumMap": "kit.json"
            }
        ]
    })";

    lotro::Config config;
    const auto err = lotro::loadConfigFromString (text, lotro::ConfigFormat::Json, config);
    REQUIRE (err.empty());

    CHECK (config.input       == "in.mid");
    CHECK (config.output      == std::optional<std::string>{ "out.abc" });
    CHECK (config.title       == std::optional<std::string>{ "A Song" });
    CHECK (config.transcriber == std::optional<std::string>{ "Brian" });
    CHECK (config.tempo       == std::optional<double>{ 140.0 });
    CHECK (config.transpose   == -2);

    REQUIRE (config.instruments.size() == 2);

    const auto& lead = config.instruments[0];
    CHECK (lead.x                  == 1);
    CHECK (lead.name               == "LuteOfAges");
    CHECK (lead.label              == std::optional<std::string>{ "Lead" });
    CHECK (lead.sources            == std::vector<int>{ 0, 2 });
    CHECK (lead.transposeSemitones == -12);
    CHECK (lead.volumePercent      == 110);
    CHECK_FALSE (lead.drumMap.has_value());

    const auto& drums = config.instruments[1];
    CHECK (drums.x       == 3);
    CHECK (drums.name    == "Drums");
    CHECK (drums.sources == std::vector<int>{ 9 });
    CHECK (drums.drumMap == std::optional<std::string>{ "kit.json" });
}

TEST_CASE ("config-loader: JSON malformed input returns error", "[config-loader][json]")
{
    const std::string text = "{ \"input\": \"a.mid\",  ";  // truncated
    lotro::Config config;
    const auto err = lotro::loadConfigFromString (text, lotro::ConfigFormat::Json, config);
    CHECK_FALSE (err.empty());
}

TEST_CASE ("config-loader: JSON missing 'instruments' produces empty array (validation rejects)", "[config-loader][json]")
{
    const std::string text = R"({ "input": "song.mid" })";
    lotro::Config config;
    const auto err = lotro::loadConfigFromString (text, lotro::ConfigFormat::Json, config);
    // Loader is permissive; it's validateConfig's job to reject. This test
    // pins the permissive-loader contract.
    CHECK (err.empty());
    CHECK (config.instruments.empty());
}

TEST_CASE ("config-loader: auto format detection picks JSON from .json extension", "[config-loader][json]")
{
    // Written to /tmp so we don't clutter the tree. Uses write-then-read.
    const auto tmpPath = std::string ("/tmp/lotro-config-loader-test.json");
    {
        std::ofstream out (tmpPath);
        out << R"({
            "input": "song.mid",
            "instruments": [ { "x": 1, "name": "Harp", "sources": [0] } ]
        })";
    }

    lotro::Config config;
    const auto err = lotro::loadConfigFromFile (tmpPath, lotro::ConfigFormat::Auto, config);
    REQUIRE (err.empty());
    CHECK (config.instruments.size() == 1);
    CHECK (config.instruments[0].name == "Harp");

    std::remove (tmpPath.c_str());
}
```

Add necessary includes at top of file:

```cpp
#include <cstdio>
#include <fstream>
#include <string>
```

- [ ] **Step 3: Register ConfigLoader.cpp + test file**

Edit `CMakeLists.txt` — add to `converter_core` sources, alphabetical:

```cmake
    Source/Core/ConfigLoader.cpp
```

Edit `Tests/CMakeLists.txt` — add:

```cmake
    ConfigLoader_tests.cpp
```

- [ ] **Step 4: Run tests; expect link errors**

Run: `cmake --build build 2>&1 | tail -10`
Expected: link errors about `loadConfigFromFile`, `loadConfigFromString`.

- [ ] **Step 5: Implement JSON loader**

Create `Source/Core/ConfigLoader.cpp`:

```cpp
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
        format = ConfigFormat::Json;      // stdin / string input defaults to JSON

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
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build build && ctest --test-dir build -R "config-loader:" 2>&1 | tail -15`
Expected: all 5 loader tests pass.

- [ ] **Step 7: Full suite regression check**

Run: `ctest --test-dir build 2>&1 | tail -3`
Expected: `93/93` passing (88 + 5 new).

- [ ] **Step 8: Commit**

```bash
git add Source/Core/ConfigLoader.h Source/Core/ConfigLoader.cpp \
        Tests/ConfigLoader_tests.cpp \
        CMakeLists.txt Tests/CMakeLists.txt
git commit -m "feat(config): JSON loader parsing Config from file or string"
```

---

### Task 5: Instrument assembly — merge, transpose, volume scaling

**Files:**
- Create: `Source/Core/InstrumentAssembly.h`
- Create: `Source/Core/InstrumentAssembly.cpp`
- Create: `Tests/InstrumentAssembly_tests.cpp`
- Modify: `CMakeLists.txt`
- Modify: `Tests/CMakeLists.txt`

- [ ] **Step 1: Write InstrumentAssembly.h**

Create `Source/Core/InstrumentAssembly.h`:

```cpp
#pragma once

#include "Config.h"
#include "Diagnostics.h"
#include "Song.h"

namespace lotro
{

// Takes a raw Song just imported from a MIDI file plus a validated Config
// and produces a new Song whose `tracks` are the merged virtual tracks —
// one Track per ConfigInstrument, with notes gathered from every source,
// per-instrument transpose and volume already applied, x and name and
// drumMap populated from the Config.
//
// Any MIDI track not referenced by any instrument's `sources` emits an
// info-level Diagnostic and is dropped.
//
// The returned Song copies tempoMap, meterMap, title, transcriber,
// ticksPerQuarter, and drumMap (used as the song-level default for Drums
// instruments without their own drumMap) from `raw` and applies the
// Config's overrides where present.
Song assembleInstruments (const Song&         raw,
                          const Config&       config,
                          Diagnostics&        diagnostics);

} // namespace lotro
```

- [ ] **Step 2: Write failing tests**

Create `Tests/InstrumentAssembly_tests.cpp`:

```cpp
#include "Core/InstrumentAssembly.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

namespace
{
    lotro::Note makeNote (int pitch, int startTick, int dur, int velocity)
    {
        lotro::Note n;
        n.pitch            = pitch;
        n.startTick        = startTick;
        n.durationTicks    = dur;
        n.velocity         = velocity;
        n.sourceTrackIndex = 0;
        n.sourceEventIndex = 0;
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
            t.notes.push_back (makeNote (60 + i, 0, 480, 100));
            s.tracks.push_back (t);
        }
        return s;
    }
}

TEST_CASE ("assembly: one instrument per track, no merging", "[assembly]")
{
    const auto raw = threeTrackRaw();

    lotro::Config cfg;
    cfg.input = "x.mid";
    for (int i = 0; i < 3; ++i)
    {
        lotro::ConfigInstrument inst;
        inst.x       = i + 1;
        inst.name    = "LuteOfAges";
        inst.sources = { i };
        cfg.instruments.push_back (inst);
    }

    lotro::Diagnostics diag;
    const auto assembled = lotro::assembleInstruments (raw, cfg, diag);

    REQUIRE (assembled.tracks.size() == 3);
    for (int i = 0; i < 3; ++i)
    {
        CHECK (assembled.tracks[i].x == i + 1);
        CHECK (assembled.tracks[i].notes.size() == 1);
        CHECK (assembled.tracks[i].notes[0].pitch == 60 + i);
    }
}

TEST_CASE ("assembly: two sources merge into one instrument", "[assembly]")
{
    const auto raw = threeTrackRaw();

    lotro::Config cfg;
    cfg.input = "x.mid";
    lotro::ConfigInstrument inst;
    inst.x       = 1;
    inst.name    = "LuteOfAges";
    inst.sources = { 0, 2 };
    cfg.instruments.push_back (inst);

    lotro::Diagnostics diag;
    const auto assembled = lotro::assembleInstruments (raw, cfg, diag);

    REQUIRE (assembled.tracks.size() == 1);
    const auto& t = assembled.tracks[0];
    CHECK (t.notes.size() == 2);

    // Notes are sorted by startTick after merge. Both start at tick 0 here,
    // so order is insertion order. Check the set of pitches is correct.
    std::vector<int> pitches;
    for (const auto& n : t.notes) pitches.push_back (n.pitch);
    std::sort (pitches.begin(), pitches.end());
    CHECK (pitches == std::vector<int>{ 60, 62 });
}

TEST_CASE ("assembly: unreferenced MIDI track emits diagnostic and is dropped", "[assembly]")
{
    const auto raw = threeTrackRaw();

    lotro::Config cfg;
    cfg.input = "x.mid";
    lotro::ConfigInstrument inst;
    inst.x       = 1;
    inst.name    = "LuteOfAges";
    inst.sources = { 0 };           // Tracks 1 and 2 go unreferenced
    cfg.instruments.push_back (inst);

    lotro::Diagnostics diag;
    const auto assembled = lotro::assembleInstruments (raw, cfg, diag);

    CHECK (assembled.tracks.size() == 1);
    int droppedCount = 0;
    for (const auto& d : diag)
        if (d.source == "InstrumentAssembly" && d.severity == lotro::Severity::Info)
            ++droppedCount;
    CHECK (droppedCount == 2);      // tracks 1 and 2
}

TEST_CASE ("assembly: per-instrument transposeSemitones shifts every note", "[assembly]")
{
    const auto raw = threeTrackRaw();

    lotro::Config cfg;
    cfg.input = "x.mid";
    lotro::ConfigInstrument inst;
    inst.x                  = 1;
    inst.name               = "LuteOfAges";
    inst.sources            = { 0 };       // pitch 60
    inst.transposeSemitones = -12;
    cfg.instruments.push_back (inst);

    lotro::Diagnostics diag;
    const auto assembled = lotro::assembleInstruments (raw, cfg, diag);

    REQUIRE (assembled.tracks.size() == 1);
    CHECK (assembled.tracks[0].notes[0].pitch == 48);
}

TEST_CASE ("assembly: global transpose adds to per-instrument transpose", "[assembly]")
{
    const auto raw = threeTrackRaw();

    lotro::Config cfg;
    cfg.input     = "x.mid";
    cfg.transpose = -5;
    lotro::ConfigInstrument inst;
    inst.x                  = 1;
    inst.name               = "LuteOfAges";
    inst.sources            = { 0 };       // pitch 60
    inst.transposeSemitones = -7;
    cfg.instruments.push_back (inst);

    lotro::Diagnostics diag;
    const auto assembled = lotro::assembleInstruments (raw, cfg, diag);

    // 60 - 7 - 5 == 48
    CHECK (assembled.tracks[0].notes[0].pitch == 48);
}

TEST_CASE ("assembly: volumePercent scales velocity multiplicatively", "[assembly]")
{
    const auto raw = threeTrackRaw();

    lotro::Config cfg;
    cfg.input = "x.mid";
    lotro::ConfigInstrument inst;
    inst.x             = 1;
    inst.name          = "LuteOfAges";
    inst.sources       = { 0 };          // velocity 100
    inst.volumePercent = 80;
    cfg.instruments.push_back (inst);

    lotro::Diagnostics diag;
    const auto assembled = lotro::assembleInstruments (raw, cfg, diag);

    CHECK (assembled.tracks[0].notes[0].velocity == 80);
}

TEST_CASE ("assembly: volumePercent 200 on velocity 100 clamps to 127 and emits Diagnostic", "[assembly]")
{
    const auto raw = threeTrackRaw();

    lotro::Config cfg;
    cfg.input = "x.mid";
    lotro::ConfigInstrument inst;
    inst.x             = 1;
    inst.name          = "LuteOfAges";
    inst.sources       = { 0 };          // velocity 100
    inst.volumePercent = 200;
    cfg.instruments.push_back (inst);

    lotro::Diagnostics diag;
    const auto assembled = lotro::assembleInstruments (raw, cfg, diag);

    CHECK (assembled.tracks[0].notes[0].velocity == 127);

    bool sawClampDiag = false;
    for (const auto& d : diag)
        if (d.source == "VolumeScale") { sawClampDiag = true; break; }
    CHECK (sawClampDiag);
}

TEST_CASE ("assembly: label populates track.name; defaults to first source's MIDI name", "[assembly]")
{
    const auto raw = threeTrackRaw();

    lotro::Config cfg;
    cfg.input = "x.mid";

    lotro::ConfigInstrument withLabel;
    withLabel.x       = 1;
    withLabel.name    = "LuteOfAges";
    withLabel.label   = std::string ("Lead");
    withLabel.sources = { 0 };
    cfg.instruments.push_back (withLabel);

    lotro::ConfigInstrument noLabel;
    noLabel.x       = 2;
    noLabel.name    = "Harp";
    noLabel.sources = { 1 };
    cfg.instruments.push_back (noLabel);

    lotro::Diagnostics diag;
    const auto assembled = lotro::assembleInstruments (raw, cfg, diag);

    CHECK (assembled.tracks[0].name == "Lead");
    CHECK (assembled.tracks[1].name == "MidiTrack1");
}

TEST_CASE ("assembly: Drums instrument with drumMap populates track.drumMap", "[assembly]")
{
    // Note: this test only pins that track.drumMap is populated non-empty
    // when inst.drumMap is a path; it doesn't verify what's inside the
    // DrumMap because that's the drum-map loader's responsibility.
    const auto raw = threeTrackRaw();

    lotro::Config cfg;
    cfg.input = "x.mid";
    lotro::ConfigInstrument inst;
    inst.x       = 1;
    inst.name    = "Drums";
    inst.sources = { 0 };
    inst.drumMap = std::string ("drum_map.json");        // must exist at repo root
    cfg.instruments.push_back (inst);

    lotro::Diagnostics diag;
    const auto assembled = lotro::assembleInstruments (raw, cfg, diag);

    REQUIRE (assembled.tracks.size() == 1);
    CHECK (assembled.tracks[0].drumMap.has_value());
    CHECK (assembled.tracks[0].drumMap->size() > 0);
}
```

- [ ] **Step 3: Register new files in CMake**

Edit `CMakeLists.txt` — in `converter_core` sources:

```cmake
    Source/Core/InstrumentAssembly.cpp
```

Edit `Tests/CMakeLists.txt`:

```cmake
    InstrumentAssembly_tests.cpp
```

- [ ] **Step 4: Verify tests fail to link**

Run: `cmake --build build 2>&1 | tail -10`
Expected: link errors about `assembleInstruments`.

- [ ] **Step 5: Implement assembly**

Create `Source/Core/InstrumentAssembly.cpp`:

```cpp
#include "InstrumentAssembly.h"

#include "Cli/DrumMapLoader.h"        // loadDrumMapFromFile
#include "LotroInstrument.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>

namespace lotro
{

namespace
{
    int clampVelocity (double scaled) noexcept
    {
        const long v = std::lround (scaled);
        if (v < 1)   return 1;
        if (v > 127) return 127;
        return (int) v;
    }

    void emitUnreferencedDiags (const Song&         raw,
                                const Config&       config,
                                Diagnostics&        diagnostics)
    {
        std::set<int> referenced;
        for (const auto& inst : config.instruments)
            for (int s : inst.sources)
                referenced.insert (s);

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
}

Song assembleInstruments (const Song&         raw,
                          const Config&       config,
                          Diagnostics&        diagnostics)
{
    Song out;
    out.ticksPerQuarter = raw.ticksPerQuarter;
    out.tempoMap        = raw.tempoMap;
    out.meterMap        = raw.meterMap;
    out.title           = config.title.value_or (raw.title);
    if (config.transcriber.has_value())
        out.transcriber = *config.transcriber;
    else
        out.transcriber = raw.transcriber;
    out.drumMap         = raw.drumMap;

    if (config.tempo.has_value() && ! out.tempoMap.empty())
        out.tempoMap.front().bpm = *config.tempo;

    emitUnreferencedDiags (raw, config, diagnostics);

    for (const auto& inst : config.instruments)
    {
        Track t;
        t.x = inst.x;
        LotroInstrument parsed = LotroInstrument::LuteOfAges;
        parseName (inst.name, parsed);               // pre-validated; safe to ignore error
        t.instrument = parsed;

        // Label: explicit → first source's MIDI track name → LOTRO enum name
        if (inst.label.has_value())
        {
            t.name = *inst.label;
        }
        else if (! inst.sources.empty()
                 && inst.sources.front() < (int) raw.tracks.size()
                 && ! raw.tracks[(size_t) inst.sources.front()].name.empty())
        {
            t.name = raw.tracks[(size_t) inst.sources.front()].name;
        }
        else
        {
            t.name = inst.name;
        }

        const int totalTranspose = inst.transposeSemitones + config.transpose;
        const double volumeScale = (double) inst.volumePercent / 100.0;

        for (int src : inst.sources)
        {
            if (src < 0 || src >= (int) raw.tracks.size()) continue;
            const auto& srcTrack = raw.tracks[(size_t) src];

            if (srcTrack.sourceMidiChannel == 10)
                t.sourceMidiChannel = 10;

            for (const auto& srcNote : srcTrack.notes)
            {
                Note n = srcNote;
                n.pitch = srcNote.pitch + totalTranspose;

                if (std::abs (volumeScale - 1.0) > 1e-9)
                {
                    const int before = srcNote.velocity;
                    const int scaled = clampVelocity ((double) before * volumeScale);
                    if ((scaled == 1 && before > 0) || (scaled == 127 && before < 127
                        && (double) before * volumeScale > 127.0))
                    {
                        Diagnostic d;
                        d.severity         = Severity::Warning;
                        d.source           = "VolumeScale";
                        d.message          = "velocity clamped during volume scale on '" + t.name + "'";
                        d.tick             = srcNote.startTick;
                        d.pitch            = srcNote.pitch;
                        d.sourceTrackIndex = srcNote.sourceTrackIndex;
                        d.sourceEventIndex = srcNote.sourceEventIndex;
                        diagnostics.push_back (std::move (d));
                    }
                    n.velocity = scaled;
                }
            }

            // Append source's notes (already transformed) to the combined list.
            for (auto& srcNote : srcTrack.notes)
            {
                Note n = srcNote;
                n.pitch    = srcNote.pitch + totalTranspose;
                n.velocity = std::abs (volumeScale - 1.0) > 1e-9
                               ? clampVelocity ((double) srcNote.velocity * volumeScale)
                               : srcNote.velocity;
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

    return out;
}

} // namespace lotro
```

**NOTE:** the code above has a bug — the diagnostic emission loop does not modify the note, then the second loop modifies and pushes. Rewrite the two loops as one:

```cpp
        for (int src : inst.sources)
        {
            if (src < 0 || src >= (int) raw.tracks.size()) continue;
            const auto& srcTrack = raw.tracks[(size_t) src];

            if (srcTrack.sourceMidiChannel == 10)
                t.sourceMidiChannel = 10;

            for (const auto& srcNote : srcTrack.notes)
            {
                Note n = srcNote;
                n.pitch = srcNote.pitch + totalTranspose;

                if (std::abs (volumeScale - 1.0) > 1e-9)
                {
                    const double scaledD = (double) srcNote.velocity * volumeScale;
                    const int    scaled  = clampVelocity (scaledD);
                    const bool   clampedHigh = scaledD > 127.0;
                    const bool   clampedLow  = scaledD < 1.0;

                    if (clampedHigh || clampedLow)
                    {
                        Diagnostic d;
                        d.severity         = Severity::Warning;
                        d.source           = "VolumeScale";
                        d.message          = "velocity clamped during volume scale on '" + t.name + "'";
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
```

Replace the two separate loops with this single one. Keep everything else unchanged.

- [ ] **Step 6: Run tests to verify**

Run: `cmake --build build && ctest --test-dir build -R "assembly:" 2>&1 | tail -15`
Expected: all 9 assembly tests pass.

- [ ] **Step 7: Full regression check**

Run: `ctest --test-dir build 2>&1 | tail -3`
Expected: `102/102` (93 + 9).

- [ ] **Step 8: Commit**

```bash
git add Source/Core/InstrumentAssembly.h Source/Core/InstrumentAssembly.cpp \
        Tests/InstrumentAssembly_tests.cpp \
        CMakeLists.txt Tests/CMakeLists.txt
git commit -m "feat(assembly): merge sources, apply transpose + volume per instrument"
```

---

### Task 6: AbcWriter emits parts in x-ascending order; uses per-track drum map

**Files:**
- Modify: `Source/Core/AbcWriter.cpp`
- Add test: extend `Tests/ConfigPipeline_tests.cpp` (created in Task 9) or a new small test file

For this task we'll add a focused unit test in a new small file, since ConfigPipeline lives later.

- Create: `Tests/AbcWriterConfig_tests.cpp`
- Modify: `Tests/CMakeLists.txt`

- [ ] **Step 1: Write failing tests**

Create `Tests/AbcWriterConfig_tests.cpp`:

```cpp
#include "Core/AbcWriter.h"
#include "Core/Song.h"

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
}

TEST_CASE ("abc-writer: parts emit in ascending x order regardless of vector order", "[abc-writer][config]")
{
    lotro::Song song;
    song.title            = "x-order";
    song.ticksPerQuarter  = 480;
    song.meterMap.push_back ({ 0, 4, 4 });
    song.tempoMap.push_back ({ 0, 120.0 });

    // Declaration order 3, 1, 2 — writer must sort ascending.
    for (int x : {3, 1, 2})
    {
        lotro::Track t;
        t.x    = x;
        t.name = "part" + std::to_string (x);
        t.instrument = lotro::LotroInstrument::LuteOfAges;
        t.notes.push_back (note (60, 0, 480));
        song.tracks.push_back (t);
    }

    const auto abc = lotro::writeAbc (song);

    const auto x1 = abc.find ("X:1");
    const auto x2 = abc.find ("X:2");
    const auto x3 = abc.find ("X:3");
    REQUIRE (x1 != std::string::npos);
    REQUIRE (x2 != std::string::npos);
    REQUIRE (x3 != std::string::npos);
    CHECK (x1 < x2);
    CHECK (x2 < x3);
}

TEST_CASE ("abc-writer: gaps in x are preserved as-written", "[abc-writer][config]")
{
    lotro::Song song;
    song.ticksPerQuarter = 480;
    song.meterMap.push_back ({ 0, 4, 4 });
    song.tempoMap.push_back ({ 0, 120.0 });

    for (int x : {1, 3, 7})
    {
        lotro::Track t;
        t.x    = x;
        t.name = "p" + std::to_string (x);
        t.instrument = lotro::LotroInstrument::LuteOfAges;
        t.notes.push_back (note (60, 0, 480));
        song.tracks.push_back (t);
    }

    const auto abc = lotro::writeAbc (song);
    CHECK (abc.find ("X:1") != std::string::npos);
    CHECK (abc.find ("X:2") == std::string::npos);
    CHECK (abc.find ("X:3") != std::string::npos);
    CHECK (abc.find ("X:7") != std::string::npos);
}

TEST_CASE ("abc-writer: track with x == 0 falls back to sequential auto-index", "[abc-writer][config]")
{
    lotro::Song song;
    song.ticksPerQuarter = 480;
    song.meterMap.push_back ({ 0, 4, 4 });
    song.tempoMap.push_back ({ 0, 120.0 });

    for (int i = 0; i < 2; ++i)
    {
        lotro::Track t;
        // x stays 0 — legacy / no-config behaviour
        t.name = "track" + std::to_string (i);
        t.instrument = lotro::LotroInstrument::LuteOfAges;
        t.notes.push_back (note (60, 0, 480));
        song.tracks.push_back (t);
    }

    const auto abc = lotro::writeAbc (song);
    CHECK (abc.find ("X:1") != std::string::npos);
    CHECK (abc.find ("X:2") != std::string::npos);
}

TEST_CASE ("abc-writer: per-track drumMap overrides song.drumMap", "[abc-writer][config]")
{
    lotro::Song song;
    song.ticksPerQuarter = 480;
    song.meterMap.push_back ({ 0, 4, 4 });
    song.tempoMap.push_back ({ 0, 120.0 });
    song.drumMap = lotro::defaultDrumMap();

    lotro::Track t;
    t.x    = 1;
    t.name = "kit";
    t.instrument = lotro::LotroInstrument::Drums;

    // Per-track drum map with only one mapping: GM 38 -> "Z" (distinctive
    // letter unlikely to appear from defaultDrumMap, so we can assert on it).
    lotro::DrumMap perTrack;
    perTrack.set (38, "Z");
    t.drumMap = perTrack;

    auto n = note (38, 0, 480);
    n.isDrum = true;
    t.notes.push_back (n);

    song.tracks.push_back (t);

    const auto abc = lotro::writeAbc (song);
    CHECK (abc.find (" Z") != std::string::npos);
}
```

Register in `Tests/CMakeLists.txt`:

```cmake
    AbcWriterConfig_tests.cpp
```

- [ ] **Step 2: Run tests to confirm failures**

Run: `cmake --build build && ctest --test-dir build -R "abc-writer:" 2>&1 | tail -15`
Expected: at least the x-ordering and per-track-drumMap tests fail (auto-index one might pass depending on current behaviour).

- [ ] **Step 3: Modify `AbcWriter::writeAbc` to sort by x and honour per-track drum map**

Open `Source/Core/AbcWriter.cpp`. Find the current `writeAbc` (near the bottom):

```cpp
std::string writeAbc (const Song& song)
{
    std::string out;
    out += "% Generated by LotroAbcConverter v0.1\n";
    out += "% Source: " + song.title + "\n\n";

    int partIndex = 1;
    for (const auto& track : song.tracks)
    {
        if (! track.enabled || track.notes.empty())
            continue;

        out += emitHeader (song, track, partIndex);
        out += emitBody (song, track);
        out += '\n';
        ++partIndex;
    }

    return out;
}
```

Replace with:

```cpp
std::string writeAbc (const Song& song)
{
    std::string out;
    out += "% Generated by LotroAbcConverter v0.1\n";
    out += "% Source: " + song.title + "\n\n";

    // Emit in ascending x order for tracks with an explicit x value; tracks
    // with x == 0 fall back to their position in song.tracks, numbered
    // sequentially starting after all explicit-x tracks.
    std::vector<const Track*> ordered;
    ordered.reserve (song.tracks.size());
    for (const auto& t : song.tracks)
        if (t.enabled && ! t.notes.empty())
            ordered.push_back (&t);

    std::stable_sort (ordered.begin(), ordered.end(),
                      [] (const Track* a, const Track* b)
                      {
                          // 0 < nonzero; among nonzeros, ascending. Among
                          // zeros, preserve original vector order via stable sort.
                          if (a->x == 0 && b->x == 0) return false;
                          if (a->x == 0) return false;        // auto-index goes after explicit
                          if (b->x == 0) return true;
                          return a->x < b->x;
                      });

    int autoIndex = 0;
    for (const Track* pt : ordered)
        if (pt->x > 0) autoIndex = std::max (autoIndex, pt->x);

    for (const Track* pt : ordered)
    {
        const int x = (pt->x > 0) ? pt->x : ++autoIndex;
        out += emitHeader (song, *pt, x);
        out += emitBody (song, *pt);
        out += '\n';
    }

    return out;
}
```

Next, find the `emitNotePart` function (near the top of the file). Its signature takes `const DrumMap& drumMap`. In `buildCluster`, it's called with `song.drumMap`. We want to call it with the per-track drum map when the track has one.

Find this line in `buildCluster`:

```cpp
            const auto np = emitNotePart (n, isDrum, drumMap);
```

and its parameter in the signature:

```cpp
    ClusterEmission buildCluster (const NoteGroup& group,
                                  const Track&     track,
                                  int              advanceNeeded,
                                  int              ticksPerQuarter,
                                  const DrumMap&   drumMap)
```

The caller in `emitBody` passes `song.drumMap`. Change the caller to pick per-track drum map if present:

Find in `emitBody`:

```cpp
            const auto emission = buildCluster (group, track, advanceForChord,
                                                song.ticksPerQuarter, song.drumMap);
```

Replace with:

```cpp
            const DrumMap& effectiveMap = track.drumMap.has_value() ? *track.drumMap : song.drumMap;
            const auto emission = buildCluster (group, track, advanceForChord,
                                                song.ticksPerQuarter, effectiveMap);
```

- [ ] **Step 4: Verify tests pass**

Run: `cmake --build build && ctest --test-dir build -R "abc-writer:" 2>&1 | tail -15`
Expected: all 4 abc-writer tests pass.

- [ ] **Step 5: Full regression check**

Run: `ctest --test-dir build 2>&1 | tail -3`
Expected: `106/106` (102 + 4).

- [ ] **Step 6: Commit**

```bash
git add Source/Core/AbcWriter.cpp Tests/AbcWriterConfig_tests.cpp Tests/CMakeLists.txt
git commit -m "feat(abc-writer): emit x-ordered parts and honour per-track drum map"
```

---

### Task 7: CLI `--config` and `--config-format` flags

**Files:**
- Modify: `Source/Cli/CliOptions.h`
- Modify: `Source/Cli/CliOptions.cpp`

- [ ] **Step 1: Extend CliOptions struct**

Edit `Source/Cli/CliOptions.h`. Inside `struct CliOptions`, add:

```cpp
    juce::File                         configFile;         // --config PATH (optional)
    juce::String                       configFormat;       // "json" | "toml" | "xml" | "" = auto
```

- [ ] **Step 2: Extend parseCli and usage text**

Edit `Source/Cli/CliOptions.cpp`.

Update `usageText()`:

```cpp
juce::String usageText()
{
    return
        "converter [OPTIONS] INPUT.mid [OUTPUT.abc]\n"
        "\n"
        "  --config PATH         Load a config file (JSON/TOML/XML)\n"
        "  --config-format FMT   Override format detection (json|toml|xml)\n"
        "  --instrument N=NAME   Assign instrument to track N (repeatable; config-less mode only)\n"
        "  --tempo BPM           Override detected main tempo\n"
        "  --transpose N         Global semitone transpose (pre range-clamp)\n"
        "  --drum-map PATH       Load drum-map JSON file, merged onto defaults\n"
        "  --list-tracks         Print track table and exit\n"
        "  --list-instruments    Print valid instrument NAME values and exit\n"
        "  -v, --verbose         Log constraint warnings to stderr\n"
        "  -h, --help            Print this help and exit\n";
}
```

In `parseCli`, after the `--drum-map` handler, add:

```cpp
        if (arg == "--config")
        {
            const auto value = takeValue (rawArgs, i, arg, result.error);
            if (result.error.isNotEmpty()) return result;
            opts.configFile = juce::File::getCurrentWorkingDirectory().getChildFile (value);
            continue;
        }

        if (arg == "--config-format")
        {
            const auto value = takeValue (rawArgs, i, arg, result.error);
            if (result.error.isNotEmpty()) return result;
            const auto lower = value.toLowerCase();
            if (lower != "json" && lower != "toml" && lower != "xml")
            {
                result.error = "--config-format must be json, toml, or xml (got '" + value + "')";
                return result;
            }
            opts.configFormat = lower;
            continue;
        }
```

- [ ] **Step 3: Build and full regression**

Run: `cmake --build build && ctest --test-dir build 2>&1 | tail -3`
Expected: `106/106` still passing (no new tests yet; just CLI surface).

- [ ] **Step 4: Commit**

```bash
git add Source/Cli/CliOptions.h Source/Cli/CliOptions.cpp
git commit -m "feat(cli): add --config and --config-format flags"
```

---

### Task 8: Synthesize Config from CLI for no-config backward path

**Files:**
- Modify: `Source/Main.cpp`

This task refactors `Main.cpp` so it always runs through `assembleInstruments`. For no-config runs, it builds a trivial `Config` matching today's behaviour.

- [ ] **Step 1: Understand current Main.cpp flow**

Open `Source/Main.cpp` and confirm the current flow: import MIDI → auto-instrument pick → applyOverrides (tempo / transpose / instrument) → runPipeline → writeAbc. We'll inject `assembleInstruments` between "import MIDI" and the pipeline loop.

- [ ] **Step 2: Add synthesis helper**

Add a new function just above `main()` in `Source/Main.cpp` (inside the anonymous namespace with the other helpers):

```cpp
    lotro::Config synthesiseConfig (const lotro::CliOptions& opts,
                                    const lotro::Song&       raw)
    {
        lotro::Config cfg;
        cfg.input = opts.inputFile.getFullPathName().toStdString();
        if (opts.outputFile != juce::File())
            cfg.output = opts.outputFile.getFullPathName().toStdString();
        if (opts.tempoOverride.has_value())
            cfg.tempo = *opts.tempoOverride;
        cfg.transpose = opts.transposeSemitones;

        for (size_t i = 0; i < raw.tracks.size(); ++i)
        {
            lotro::ConfigInstrument inst;
            inst.x       = (int) (i + 1);
            inst.sources = { (int) i };

            const auto picked = lotro::pickInstrumentForTrack (raw.tracks[i]);
            inst.name = std::string (lotro::displayName (picked));

            // Apply --instrument N=NAME overrides
            auto overrideIt = opts.instrumentOverrides.find ((int) i);
            if (overrideIt != opts.instrumentOverrides.end())
                inst.name = std::string (lotro::displayName (overrideIt->second));

            cfg.instruments.push_back (inst);
        }
        return cfg;
    }
```

Add include at the top:

```cpp
#include "Core/Config.h"
#include "Core/InstrumentAssembly.h"
```

- [ ] **Step 3: Rewire main() to always assemble**

Find the block in `main()` starting at:

```cpp
        // Auto-pick an instrument per track based on pitch range. Explicit
        // --instrument flags run next and override the picked default.
        for (auto& track : song.tracks)
            track.instrument = lotro::pickInstrumentForTrack (track);

        applyOverrides (song, opts);
        runPipeline (song, diagnostics);
```

Replace with:

```cpp
        lotro::Config cfg;
        if (opts.configFile != juce::File())
        {
            // Phase 1: only JSON is implemented. Other formats land in Phase 2.
            const auto format = opts.configFormat.isEmpty()
                ? lotro::ConfigFormat::Auto
                : (opts.configFormat == "json" ? lotro::ConfigFormat::Json
                 : opts.configFormat == "toml" ? lotro::ConfigFormat::Toml
                 : lotro::ConfigFormat::Xml);

            const auto loadErr = lotro::loadConfigFromFile (
                opts.configFile.getFullPathName().toStdString(),
                format,
                cfg);
            if (! loadErr.empty())
            {
                std::cerr << "config error: " << loadErr << "\n";
                return 2;
            }

            // CLI overrides (tempo is replace; transpose is additive)
            if (opts.tempoOverride.has_value())
                cfg.tempo = *opts.tempoOverride;
            if (opts.transposeSemitones != 0)
                cfg.transpose += opts.transposeSemitones;

            const auto validErr = lotro::validateConfig (cfg, (int) song.tracks.size());
            if (! validErr.empty())
            {
                std::cerr << "config error: " << validErr << "\n";
                return 2;
            }
        }
        else
        {
            cfg = synthesiseConfig (opts, song);
        }

        auto assembled = lotro::assembleInstruments (song, cfg, diagnostics);

        runPipeline (assembled, diagnostics);
```

Now change the lines below that still reference `song` after the pipeline:

```cpp
        const auto abc = lotro::writeAbc (song);
```

to:

```cpp
        const auto abc = lotro::writeAbc (assembled);
```

And after that, where `opts.outputFile.replaceWithText` is called, it still uses `opts.outputFile` — leave that alone.

Also — `applyOverrides` is now redundant since synthesiseConfig + assemble handle the same knobs. Delete the `applyOverrides` function entirely, and remove the `applyOverrides (song, opts);` line if it lingers. Double-check that no other call site uses it.

- [ ] **Step 4: Build and run full suite**

Run: `cmake --build build 2>&1 | tail -5`
Expected: clean build.

Run: `ctest --test-dir build 2>&1 | tail -3`
Expected: `106/106` still passing — the no-config path must produce identical output.

- [ ] **Step 5: Verify end-to-end on real MIDI**

Run: `build/converter_artefacts/Debug/converter "midi/Barnes Brothers Band - Pull The Wires.mid" /tmp/bbb.abc 2>&1 | tail -2`
Then: `diff <(head -50 /tmp/bbb.abc) <(head -50 /tmp/bbb-before.abc) 2>&1 || true`

(If you don't have a saved "before" output, skip this check — the EndToEnd test already pins structural validity.)

- [ ] **Step 6: Commit**

```bash
git add Source/Main.cpp
git commit -m "feat(cli): route through Config/assembleInstruments; preserve no-config behaviour"
```

---

### Task 9: End-to-end pipeline tests with a JSON config

**Files:**
- Create: `Tests/ConfigPipeline_tests.cpp`
- Modify: `Tests/CMakeLists.txt`

- [ ] **Step 1: Write tests**

Create `Tests/ConfigPipeline_tests.cpp`:

```cpp
#include "Core/AbcWriter.h"
#include "Core/ConfigLoader.h"
#include "Core/InstrumentAssembly.h"
#include "Core/Song.h"
#include "Core/Constraints/ChordConstraint.h"
#include "Core/Constraints/CollisionGuard.h"
#include "Core/Constraints/DurationConstraint.h"
#include "Core/Constraints/DynamicMapper.h"
#include "Core/Constraints/RangeConstraint.h"
#include "Core/Constraints/TempoCollapse.h"

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

    lotro::Song rawSong (int ppq = 480)
    {
        lotro::Song s;
        s.ticksPerQuarter = ppq;
        s.tempoMap.push_back ({ 0, 120.0 });
        s.meterMap.push_back ({ 0, 4, 4 });
        return s;
    }

    void runPipeline (lotro::Song& song, lotro::Diagnostics& diag)
    {
        for (auto& t : song.tracks)
        {
            if (! t.enabled) continue;
            lotro::applyRangeConstraint    (t, diag);
            lotro::applyChordConstraint    (t, diag);
            lotro::applyDurationConstraint (t, song, diag);
            lotro::applyTempoCollapse      (t, song, diag);
            lotro::applyCollisionGuard     (t, diag);
            lotro::applyDynamicMapper      (t, diag);
        }
        lotro::applyTempoCollapseToSongMaps (song);
    }

    bool contains (const std::string& hay, const std::string& needle)
    {
        return hay.find (needle) != std::string::npos;
    }
}

TEST_CASE ("config-pipeline: two MIDI sources merge into one instrument; chord forms at shared tick", "[config-pipeline]")
{
    auto raw = rawSong();
    lotro::Track t0, t1;
    t0.name = "lead"; t0.notes.push_back (note (60, 0, 480));
    t1.name = "harmony"; t1.notes.push_back (note (64, 0, 480));
    raw.tracks.push_back (t0);
    raw.tracks.push_back (t1);

    lotro::Config cfg;
    cfg.input = "x.mid";
    lotro::ConfigInstrument inst;
    inst.x       = 1;
    inst.name    = "LuteOfAges";
    inst.sources = { 0, 1 };
    cfg.instruments.push_back (inst);

    lotro::Diagnostics diag;
    auto song = lotro::assembleInstruments (raw, cfg, diag);
    runPipeline (song, diag);
    const auto abc = lotro::writeAbc (song);

    // A chord at tick 0 with two distinct pitches:
    CHECK (contains (abc, "[C"));
    CHECK (contains (abc, "E"));      // middle E maps to uppercase E
    CHECK (contains (abc, "X:1"));
}

TEST_CASE ("config-pipeline: per-instrument transpose shifts the emitted pitch", "[config-pipeline]")
{
    auto raw = rawSong();
    lotro::Track t;
    t.name = "src"; t.notes.push_back (note (60, 0, 480));       // middle C
    raw.tracks.push_back (t);

    lotro::Config cfg;
    cfg.input = "x.mid";
    lotro::ConfigInstrument inst;
    inst.x                  = 1;
    inst.name               = "LuteOfAges";
    inst.sources            = { 0 };
    inst.transposeSemitones = 12;             // bump up an octave -> pitch 72 -> 'c'
    cfg.instruments.push_back (inst);

    lotro::Diagnostics diag;
    auto song = lotro::assembleInstruments (raw, cfg, diag);
    runPipeline (song, diag);
    const auto abc = lotro::writeAbc (song);

    CHECK (contains (abc, "c"));      // lowercase c == pitch 72
}

TEST_CASE ("config-pipeline: volume scale down lands in the quieter dynamic bucket", "[config-pipeline]")
{
    auto raw = rawSong();
    lotro::Track t;
    t.name = "src"; t.notes.push_back (note (60, 0, 480, /*vel=*/100));     // mf
    raw.tracks.push_back (t);

    lotro::Config cfg;
    cfg.input = "x.mid";
    lotro::ConfigInstrument inst;
    inst.x             = 1;
    inst.name          = "LuteOfAges";
    inst.sources       = { 0 };
    inst.volumePercent = 30;                // 100 * 0.3 = 30 -> pp bucket
    cfg.instruments.push_back (inst);

    lotro::Diagnostics diag;
    auto song = lotro::assembleInstruments (raw, cfg, diag);
    runPipeline (song, diag);
    const auto abc = lotro::writeAbc (song);

    CHECK (contains (abc, "+pp+"));
    CHECK_FALSE (contains (abc, "+mf+"));
}

TEST_CASE ("config-pipeline: three instruments emit in x-ascending order", "[config-pipeline]")
{
    auto raw = rawSong();
    for (int i = 0; i < 3; ++i)
    {
        lotro::Track t;
        t.name = "src" + std::to_string (i);
        t.notes.push_back (note (60 + i, 0, 480));
        raw.tracks.push_back (t);
    }

    lotro::Config cfg;
    cfg.input = "x.mid";
    // Declaration order 7, 1, 3 -> expected emission order 1, 3, 7
    int xs[]        = { 7, 1, 3 };
    int sources[]   = { 0, 1, 2 };
    for (size_t i = 0; i < 3; ++i)
    {
        lotro::ConfigInstrument inst;
        inst.x       = xs[i];
        inst.name    = "LuteOfAges";
        inst.sources = { sources[i] };
        cfg.instruments.push_back (inst);
    }

    lotro::Diagnostics diag;
    auto song = lotro::assembleInstruments (raw, cfg, diag);
    runPipeline (song, diag);
    const auto abc = lotro::writeAbc (song);

    const auto x1 = abc.find ("X:1");
    const auto x3 = abc.find ("X:3");
    const auto x7 = abc.find ("X:7");
    REQUIRE (x1 != std::string::npos);
    REQUIRE (x3 != std::string::npos);
    REQUIRE (x7 != std::string::npos);
    CHECK (x1 < x3);
    CHECK (x3 < x7);
}

TEST_CASE ("config-pipeline: config label becomes T: suffix", "[config-pipeline]")
{
    auto raw = rawSong();
    lotro::Track t;
    t.name = "Boring MIDI Name";
    t.notes.push_back (note (60, 0, 480));
    raw.tracks.push_back (t);

    lotro::Config cfg;
    cfg.input = "x.mid";
    cfg.title = std::string ("Test Song");
    lotro::ConfigInstrument inst;
    inst.x       = 1;
    inst.name    = "LuteOfAges";
    inst.label   = std::string ("Lead Lute");
    inst.sources = { 0 };
    cfg.instruments.push_back (inst);

    lotro::Diagnostics diag;
    auto song = lotro::assembleInstruments (raw, cfg, diag);
    runPipeline (song, diag);
    const auto abc = lotro::writeAbc (song);

    CHECK (contains (abc, "T:Test Song - Lead Lute"));
    CHECK_FALSE (contains (abc, "Boring MIDI Name"));
}

TEST_CASE ("config-pipeline: full JSON round-trip through loader + assembly + writer", "[config-pipeline][json]")
{
    auto raw = rawSong();
    lotro::Track t;
    t.name = "src"; t.notes.push_back (note (60, 0, 480));
    raw.tracks.push_back (t);

    const std::string json = R"({
        "input": "x.mid",
        "title": "RoundTrip",
        "instruments": [
            { "x": 1, "name": "Harp", "label": "Harp Part", "sources": [0] }
        ]
    })";

    lotro::Config cfg;
    REQUIRE (lotro::loadConfigFromString (json, lotro::ConfigFormat::Json, cfg).empty());

    lotro::Diagnostics diag;
    auto song = lotro::assembleInstruments (raw, cfg, diag);
    runPipeline (song, diag);
    const auto abc = lotro::writeAbc (song);

    CHECK (contains (abc, "T:RoundTrip - Harp Part"));
    CHECK (contains (abc, "X:1"));
}
```

Register in `Tests/CMakeLists.txt`:

```cmake
    ConfigPipeline_tests.cpp
```

- [ ] **Step 2: Run tests**

Run: `cmake --build build && ctest --test-dir build -R "config-pipeline:" 2>&1 | tail -15`
Expected: all 6 config-pipeline tests pass.

- [ ] **Step 3: Full regression**

Run: `ctest --test-dir build 2>&1 | tail -3`
Expected: `112/112` (106 + 6).

- [ ] **Step 4: Commit**

```bash
git add Tests/ConfigPipeline_tests.cpp Tests/CMakeLists.txt
git commit -m "test: end-to-end config-driven conversion pipeline"
```

---

### Task 10: Update CLAUDE.md to document the new CLI shape

**Files:**
- Modify: `CLAUDE.md`

- [ ] **Step 1: Update CLI shape section**

Open `CLAUDE.md`. Find the `## CLI shape` section and replace the command-line block with:

```
converter [OPTIONS] INPUT.mid [OUTPUT.abc]
  --config PATH         Load a config file (JSON/TOML/XML)
  --config-format FMT   Override format detection (json|toml|xml)
  --instrument N=NAME   Assign instrument to track N (config-less mode only)
  --tempo BPM           Override detected main tempo
  --transpose N         Global semitone transpose (pre range-clamp)
  --drum-map PATH       Load drum-map JSON file, merged onto defaults
  --list-tracks         Print track table and exit
  --list-instruments    Print valid instrument NAME values and exit
  -v, --verbose         Log Diagnostics to stderr
  -h, --help            Print this help and exit
```

Below the flag block, add a short "Config mode" subsection referencing the spec:

```markdown
### Config mode

`--config PATH` loads a JSON (Phase 1) / TOML / XML (Phase 2) config that
authoritatively describes the output: which MIDI tracks become which ABC
parts, per-instrument transpose and volume scaling, X: index assignment,
track merging, and per-instrument drum-map overrides. Without `--config`,
the converter synthesises an equivalent internal `Config` from the CLI
flags so existing one-shot invocations keep working bit-for-bit.

See `docs/superpowers/specs/2026-04-23-config-driven-conversion-design.md`
for the full schema.
```

Also bump the test count in the `## Testing notes` section: `**76/76**` → `**112/112**`.

- [ ] **Step 2: Commit**

```bash
git add CLAUDE.md
git commit -m "docs: sync CLAUDE.md with config-driven mode"
```

---

## Phase 2 — XML and TOML loaders

### Task 11: XML loader

**Files:**
- Modify: `Source/Core/ConfigLoader.cpp`
- Create: `Tests/ConfigLoaderXml_tests.cpp`
- Modify: `Tests/CMakeLists.txt`

- [ ] **Step 1: Write failing XML tests**

Create `Tests/ConfigLoaderXml_tests.cpp`:

```cpp
#include "Core/ConfigLoader.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE ("config-loader: XML minimal config parses", "[config-loader][xml]")
{
    const std::string text = R"(<?xml version="1.0"?>
<config>
  <input>song.mid</input>
  <instruments>
    <instrument x="1" name="LuteOfAges">
      <sources><source>0</source></sources>
    </instrument>
  </instruments>
</config>)";

    lotro::Config config;
    const auto err = lotro::loadConfigFromString (text, lotro::ConfigFormat::Xml, config);
    REQUIRE (err.empty());
    CHECK (config.input == "song.mid");
    REQUIRE (config.instruments.size() == 1);
    CHECK (config.instruments[0].x    == 1);
    CHECK (config.instruments[0].name == "LuteOfAges");
    CHECK (config.instruments[0].sources == std::vector<int>{ 0 });
}

TEST_CASE ("config-loader: XML full config round-trips all fields", "[config-loader][xml]")
{
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
      <sources><source>0</source><source>2</source></sources>
      <transposeSemitones>-12</transposeSemitones>
      <volumePercent>110</volumePercent>
    </instrument>
    <instrument x="3" name="Drums">
      <sources><source>9</source></sources>
      <drumMap>kit.json</drumMap>
    </instrument>
  </instruments>
</config>)";

    lotro::Config config;
    const auto err = lotro::loadConfigFromString (text, lotro::ConfigFormat::Xml, config);
    REQUIRE (err.empty());

    CHECK (config.input       == "in.mid");
    CHECK (config.output      == std::optional<std::string>{ "out.abc" });
    CHECK (config.title       == std::optional<std::string>{ "A Song" });
    CHECK (config.transpose   == -2);

    REQUIRE (config.instruments.size() == 2);

    const auto& lead = config.instruments[0];
    CHECK (lead.x                  == 1);
    CHECK (lead.name               == "LuteOfAges");
    CHECK (lead.label              == std::optional<std::string>{ "Lead" });
    CHECK (lead.sources            == std::vector<int>{ 0, 2 });
    CHECK (lead.transposeSemitones == -12);
    CHECK (lead.volumePercent      == 110);

    const auto& drums = config.instruments[1];
    CHECK (drums.name    == "Drums");
    CHECK (drums.drumMap == std::optional<std::string>{ "kit.json" });
}

TEST_CASE ("config-loader: XML malformed input returns error", "[config-loader][xml]")
{
    const std::string text = "<config><input>a.mid</inpu";        // truncated
    lotro::Config config;
    const auto err = lotro::loadConfigFromString (text, lotro::ConfigFormat::Xml, config);
    CHECK_FALSE (err.empty());
}
```

Register in `Tests/CMakeLists.txt`:

```cmake
    ConfigLoaderXml_tests.cpp
```

- [ ] **Step 2: Verify tests fail (XML still stubbed)**

Run: `cmake --build build && ctest --test-dir build -R "xml" 2>&1 | tail -8`
Expected: failures with "XML config not yet supported (Phase 2)".

- [ ] **Step 3: Implement XML loader in ConfigLoader.cpp**

In `Source/Core/ConfigLoader.cpp`, add a helper function in the anonymous namespace:

```cpp
    std::optional<std::string> childText (const juce::XmlElement& parent, const char* tag)
    {
        if (auto* c = parent.getChildByName (tag))
        {
            const auto text = c->getAllSubText().toStdString();
            if (! text.empty()) return text;
        }
        return std::nullopt;
    }

    std::string loadXml (std::string_view text, Config& out)
    {
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
            out.tempo = std::stod (*t);

        if (auto t = childText (*xml, "transpose"))
            out.transpose = std::stoi (*t);

        auto* instrumentsElem = xml->getChildByName ("instruments");
        if (instrumentsElem == nullptr)
            return {};     // permissive — validator catches empty arrays

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
                for (auto* s = srcElem->getFirstChildElement(); s != nullptr;
                     s = s->getNextElement())
                {
                    if (s->getTagName() == "source")
                        ci.sources.push_back (s->getAllSubText().getIntValue());
                }
            }

            if (auto t = childText (*inst, "transposeSemitones"))
                ci.transposeSemitones = std::stoi (*t);
            if (auto t = childText (*inst, "volumePercent"))
                ci.volumePercent = std::stoi (*t);
            ci.drumMap = childText (*inst, "drumMap");

            out.instruments.push_back (std::move (ci));
        }

        return {};
    }
```

Update the dispatcher in `loadConfigFromString`:

```cpp
        case ConfigFormat::Xml:  return loadXml (text, out);
```

- [ ] **Step 4: Verify tests pass**

Run: `cmake --build build && ctest --test-dir build -R "xml" 2>&1 | tail -10`
Expected: all 3 XML tests pass.

- [ ] **Step 5: Full regression**

Run: `ctest --test-dir build 2>&1 | tail -3`
Expected: `115/115` (112 + 3).

- [ ] **Step 6: Commit**

```bash
git add Source/Core/ConfigLoader.cpp Tests/ConfigLoaderXml_tests.cpp Tests/CMakeLists.txt
git commit -m "feat(config): XML loader via juce::XmlDocument"
```

---

### Task 12: Vendor toml++ and add TOML loader

**Files:**
- Create: `Source/ThirdParty/tomlplusplus/toml.hpp`
- Modify: `CMakeLists.txt`
- Modify: `Source/Core/ConfigLoader.cpp`
- Create: `Tests/ConfigLoaderToml_tests.cpp`
- Modify: `Tests/CMakeLists.txt`

- [ ] **Step 1: Download the single-header toml++**

Run from the repo root:

```bash
mkdir -p Source/ThirdParty/tomlplusplus
curl -L -o Source/ThirdParty/tomlplusplus/toml.hpp \
     https://raw.githubusercontent.com/marzer/tomlplusplus/master/toml.hpp
```

Verify the download succeeded and the file looks like a TOML library header:

```bash
head -15 Source/ThirdParty/tomlplusplus/toml.hpp
```

Expected: header comments mentioning toml++ and the MIT licence.

- [ ] **Step 2: Add include path to CMakeLists.txt**

In `CMakeLists.txt`, find the `target_include_directories(converter_core PUBLIC Source)` line and add the third-party include:

```cmake
target_include_directories(converter_core PUBLIC
    Source
    Source/ThirdParty/tomlplusplus)
```

- [ ] **Step 3: Write failing TOML tests**

Create `Tests/ConfigLoaderToml_tests.cpp`:

```cpp
#include "Core/ConfigLoader.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE ("config-loader: TOML minimal config parses", "[config-loader][toml]")
{
    const std::string text = R"(
input = "song.mid"

[[instruments]]
x = 1
name = "LuteOfAges"
sources = [0]
)";

    lotro::Config config;
    const auto err = lotro::loadConfigFromString (text, lotro::ConfigFormat::Toml, config);
    REQUIRE (err.empty());
    CHECK (config.input == "song.mid");
    REQUIRE (config.instruments.size() == 1);
    CHECK (config.instruments[0].x       == 1);
    CHECK (config.instruments[0].name    == "LuteOfAges");
    CHECK (config.instruments[0].sources == std::vector<int>{ 0 });
}

TEST_CASE ("config-loader: TOML full config round-trips all fields", "[config-loader][toml]")
{
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
sources = [0, 2]
transposeSemitones = -12
volumePercent = 110

[[instruments]]
x = 3
name = "Drums"
sources = [9]
drumMap = "kit.json"
)";

    lotro::Config config;
    const auto err = lotro::loadConfigFromString (text, lotro::ConfigFormat::Toml, config);
    REQUIRE (err.empty());

    CHECK (config.input       == "in.mid");
    CHECK (config.output      == std::optional<std::string>{ "out.abc" });
    CHECK (config.title       == std::optional<std::string>{ "A Song" });
    CHECK (config.tempo       == std::optional<double>{ 140.0 });
    CHECK (config.transpose   == -2);

    REQUIRE (config.instruments.size() == 2);

    const auto& lead = config.instruments[0];
    CHECK (lead.x                  == 1);
    CHECK (lead.label              == std::optional<std::string>{ "Lead" });
    CHECK (lead.sources            == std::vector<int>{ 0, 2 });
    CHECK (lead.transposeSemitones == -12);
    CHECK (lead.volumePercent      == 110);

    const auto& drums = config.instruments[1];
    CHECK (drums.name    == "Drums");
    CHECK (drums.drumMap == std::optional<std::string>{ "kit.json" });
}

TEST_CASE ("config-loader: TOML malformed input returns error", "[config-loader][toml]")
{
    const std::string text = "input = \"a.mid\"  \n[[";        // truncated table header
    lotro::Config config;
    const auto err = lotro::loadConfigFromString (text, lotro::ConfigFormat::Toml, config);
    CHECK_FALSE (err.empty());
}
```

Register in `Tests/CMakeLists.txt`:

```cmake
    ConfigLoaderToml_tests.cpp
```

- [ ] **Step 4: Verify tests fail**

Run: `cmake --build build && ctest --test-dir build -R "toml" 2>&1 | tail -8`
Expected: "TOML config not yet supported (Phase 2)".

- [ ] **Step 5: Implement TOML loader**

In `Source/Core/ConfigLoader.cpp`, at the top, add:

```cpp
#include <toml.hpp>
```

In the anonymous namespace, add:

```cpp
    std::string loadToml (std::string_view text, Config& out)
    {
        toml::parse_result parsed;
        try
        {
            parsed = toml::parse (text);
        }
        catch (const toml::parse_error& e)
        {
            return std::string ("config TOML: ") + e.what();
        }

        const toml::table& tbl = parsed;

        if (auto v = tbl["input"].value<std::string>())
            out.input = *v;
        else
            return "config TOML: 'input' is required";

        if (auto v = tbl["output"].value<std::string>())           out.output      = *v;
        if (auto v = tbl["title"].value<std::string>())            out.title       = *v;
        if (auto v = tbl["transcriber"].value<std::string>())      out.transcriber = *v;
        if (auto v = tbl["tempo"].value<double>())                 out.tempo       = *v;
        if (auto v = tbl["transpose"].value<int64_t>())            out.transpose   = (int) *v;

        if (auto* arr = tbl["instruments"].as_array())
        {
            for (auto& el : *arr)
            {
                const auto* instTbl = el.as_table();
                if (instTbl == nullptr) continue;

                ConfigInstrument inst;
                if (auto v = (*instTbl)["x"].value<int64_t>())               inst.x    = (int) *v;
                if (auto v = (*instTbl)["name"].value<std::string>())        inst.name = *v;
                if (auto v = (*instTbl)["label"].value<std::string>())       inst.label = *v;
                if (auto v = (*instTbl)["transposeSemitones"].value<int64_t>())
                    inst.transposeSemitones = (int) *v;
                if (auto v = (*instTbl)["volumePercent"].value<int64_t>())
                    inst.volumePercent = (int) *v;
                if (auto v = (*instTbl)["drumMap"].value<std::string>())
                    inst.drumMap = *v;

                if (auto* src = (*instTbl)["sources"].as_array())
                {
                    for (auto& s : *src)
                        if (auto i = s.value<int64_t>())
                            inst.sources.push_back ((int) *i);
                }

                out.instruments.push_back (std::move (inst));
            }
        }

        return {};
    }
```

Update the dispatcher:

```cpp
        case ConfigFormat::Toml: return loadToml (text, out);
```

- [ ] **Step 6: Verify tests pass**

Run: `cmake --build build && ctest --test-dir build -R "toml" 2>&1 | tail -10`
Expected: all 3 TOML tests pass.

- [ ] **Step 7: Full regression**

Run: `ctest --test-dir build 2>&1 | tail -3`
Expected: `118/118` (115 + 3).

- [ ] **Step 8: Commit**

```bash
git add Source/ThirdParty/tomlplusplus/toml.hpp CMakeLists.txt \
        Source/Core/ConfigLoader.cpp \
        Tests/ConfigLoaderToml_tests.cpp Tests/CMakeLists.txt
git commit -m "feat(config): TOML loader via vendored toml++"
```

---

### Task 13: Final sync — update CLAUDE.md with Phase-2 completion

**Files:**
- Modify: `CLAUDE.md`

- [ ] **Step 1: Update test count and feature status**

In `CLAUDE.md`, in `## Testing notes`:

- Bump `**112/112**` → `**118/118**`.

In the `### Config mode` subsection, change `(JSON Phase 1) / TOML / XML (Phase 2)` to `(JSON / TOML / XML)` — all three formats are now implemented.

- [ ] **Step 2: Commit**

```bash
git add CLAUDE.md
git commit -m "docs: bump test count and mark all config formats live"
```

---

## Self-review notes

**Spec coverage:** every section of the spec is implemented by at least one task:

- Config schema (top-level + instrument fields) → Task 2 (struct), Task 3 (validator), Task 4 (JSON loader), Task 11 (XML), Task 12 (TOML).
- Merging semantics → Task 5.
- Volume scaling + clamp diagnostic → Task 5.
- Transpose handling (per-instrument + global additive) → Task 5, Task 8 (no-config path preserves existing `--transpose` semantics).
- X: index rules (required, unique, gaps, ascending emission) → Task 3 (validation), Task 6 (emission).
- Drum-map per-instrument → Task 1 (`Track::drumMap`), Task 5 (populate), Task 6 (emit).
- Validation / fail-fast → Task 3, wired in Task 8.
- CLI changes → Task 7 (flags), Task 8 (wiring).
- Format detection / `--config-format` → Task 4 (auto-detect scaffolding, `--config-format` plumbed through in Task 7 and respected in Task 8).
- No-config backward compatibility → Task 8.
- Phase rollout → Phase-1 tasks 1-10; Phase-2 tasks 11-12; Task 13 closes out docs.

**Placeholder scan:** none — every step has runnable commands and either exact code or an exact edit target.

**Type consistency:** `validateConfig` signature is consistent across tasks; `loadConfigFromString`/`loadConfigFromFile` signatures declared in Task 4 and reused in Tasks 8, 11, 12; `assembleInstruments` signature declared in Task 5 and used by Task 8 and Task 9's tests.

**Risk callouts:**

- Task 8's rewiring of `main()` is the most intrusive change; the full test suite (especially `EndToEnd_tests.cpp`) is the safety net for "no-config behaviour is bit-for-bit preserved." If Task 8 breaks the existing tests, back up and examine `synthesiseConfig` — the most likely bug is mis-handling `opts.tempoOverride` or `opts.instrumentOverrides`.
- Task 12's TOML dependency is fetched from GitHub; if the repo is airgapped, vendor the header some other way.
- The existing `InstrumentAssembly.cpp` emits `VolumeScale` Diagnostics with `Severity::Warning`. If you'd rather classify those as `Info`, it's a one-line change — the tests in Task 5 only check `source == "VolumeScale"` presence.

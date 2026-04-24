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

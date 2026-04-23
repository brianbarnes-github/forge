#pragma once

#include "Config.h"

#include <string>
#include <string_view>

namespace lotro
{

enum class ConfigFormat
{
    Auto,
    Json,
    Toml,
    Xml
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

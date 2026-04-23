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

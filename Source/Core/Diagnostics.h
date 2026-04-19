#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace lotro
{

enum class Severity
{
    Info,
    Warning,
    Error
};

// A structured record of something the pipeline noticed. The GUI will render
// these as clickable list items; tools can filter by severity or source.
// Fields with a sentinel of -1 mean "not applicable" (e.g. a track-level
// warning has no specific tick).
struct Diagnostic
{
    Severity    severity   = Severity::Warning;
    std::string source;      // short tag: "RangeConstraint", "MidiImporter", ...
    std::string message;     // human-readable detail
    int         trackIndex  = -1;
    int         tick        = -1;
    int         pitch       = -1;
};

using Diagnostics = std::vector<Diagnostic>;

// Convenience for renderers/logs that want a single-line representation.
inline std::string formatDiagnostic (const Diagnostic& d)
{
    std::string out;
    out += "[";
    switch (d.severity)
    {
        case Severity::Info:    out += "info";    break;
        case Severity::Warning: out += "warn";    break;
        case Severity::Error:   out += "error";   break;
    }
    out += "] ";
    if (! d.source.empty())
    {
        out += d.source;
        out += ": ";
    }
    out += d.message;
    if (d.trackIndex >= 0 || d.tick >= 0 || d.pitch >= 0)
    {
        out += " (";
        bool needsSep = false;
        auto maybeAppend = [&] (std::string_view label, int value)
        {
            if (value < 0) return;
            if (needsSep) out += ", ";
            out += label;
            out += "=";
            out += std::to_string (value);
            needsSep = true;
        };
        maybeAppend ("track", d.trackIndex);
        maybeAppend ("tick",  d.tick);
        maybeAppend ("pitch", d.pitch);
        out += ")";
    }
    return out;
}

} // namespace lotro

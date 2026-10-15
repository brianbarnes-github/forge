#include "UI/Playback/TransportText.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace lotro
{

namespace
{
    std::string trim (const std::string& s)
    {
        const auto first = s.find_first_not_of (" \t");
        if (first == std::string::npos)
            return {};
        return s.substr (first, s.find_last_not_of (" \t") - first + 1);
    }

    // Digits only, 1..9 of them.
    std::optional<long> parseCount (const std::string& s)
    {
        if (s.empty() || s.size() > 9 || s.find_first_not_of ("0123456789") != std::string::npos)
            return std::nullopt;
        return std::strtol (s.c_str(), nullptr, 10);
    }

    std::optional<double> parseNumber (const std::string& text)
    {
        const std::string t = trim (text);
        if (t.empty())
            return std::nullopt;
        char* end = nullptr;
        const double v = std::strtod (t.c_str(), &end);
        if (end != t.c_str() + t.size() || ! std::isfinite (v))
            return std::nullopt;
        return v;
    }

    size_t segmentIndexAt (const std::vector<MeterSegment>& segments, double tick)
    {
        size_t i = 0;
        while (i + 1 < segments.size() && segments[i + 1].startTick <= tick)
            ++i;
        return i;
    }
}

const char* describe (PositionParseError e)
{
    switch (e)
    {
        case PositionParseError::Empty:          return "Enter a position such as 5.1.0";
        case PositionParseError::BadFormat:      return "Use bar.beat.tick, such as 5.1.0";
        case PositionParseError::BarOutOfRange:  return "Bars start at 1";
        case PositionParseError::BeatOutOfRange: return "That beat is outside the bar";
        case PositionParseError::TickOutOfRange: return "That tick is outside the beat";
    }
    return "";
}

std::string formatPosition (const std::vector<MeterSegment>& segments, double tick)
{
    if (segments.empty())
        return "1.1.0";
    const double t = std::floor (std::max (0.0, tick) + 0.5);
    const auto& s = segments[segmentIndexAt (segments, t)];
    const double intoSegment = t - s.startTick;
    const double barsIn = std::floor (intoSegment / s.ticksPerBar);
    const double intoBar = intoSegment - barsIn * s.ticksPerBar;
    const double beatsIn = std::floor (intoBar / s.ticksPerBeat);
    const double tickInBeat = std::floor (intoBar - beatsIn * s.ticksPerBeat);
    char buffer[64];
    std::snprintf (buffer, sizeof buffer, "%lld.%d.%d", s.firstBar + (long long) barsIn + 1, (int) beatsIn + 1, (int) tickInBeat);
    return buffer;
}

ParsedPosition parsePosition (const std::string& text, const std::vector<MeterSegment>& segments)
{
    const std::string t = trim (text);
    if (t.empty())
        return { 0.0, PositionParseError::Empty };

    std::vector<std::string> parts;
    size_t from = 0;
    for (;;)
    {
        const auto dot = t.find ('.', from);
        parts.push_back (t.substr (from, dot == std::string::npos ? std::string::npos : dot - from));
        if (dot == std::string::npos)
            break;
        from = dot + 1;
    }
    if (parts.size() > 3 || segments.empty())
        return { 0.0, PositionParseError::BadFormat };

    const auto bar = parseCount (parts[0]);
    const auto beat = parts.size() > 1 ? parseCount (parts[1]) : std::optional<long> (1);
    const auto tickInBeat = parts.size() > 2 ? parseCount (parts[2]) : std::optional<long> (0);
    if (! bar || ! beat || ! tickInBeat)
        return { 0.0, PositionParseError::BadFormat };
    if (*bar < 1)
        return { 0.0, PositionParseError::BarOutOfRange };

    size_t index = 0;
    while (index + 1 < segments.size() && segments[index + 1].firstBar <= *bar - 1)
        ++index;
    const auto& s = segments[index];
    if (*beat < 1 || *beat > s.numerator)
        return { 0.0, PositionParseError::BeatOutOfRange };
    if ((double) *tickInBeat >= s.ticksPerBeat)
        return { 0.0, PositionParseError::TickOutOfRange };

    const double barsIn = (double) (*bar - 1 - s.firstBar);
    return { s.startTick + barsIn * s.ticksPerBar + (double) (*beat - 1) * s.ticksPerBeat + (double) *tickInBeat, std::nullopt };
}

double beatsToTicks (const std::vector<MeterSegment>& segments, double startTick, double beats)
{
    if (segments.empty() || ! (beats > 0.0))
        return startTick;
    double cur = startTick;
    for (size_t i = segmentIndexAt (segments, cur);; ++i)
    {
        const auto& s = segments[i];
        const bool last = i + 1 == segments.size();
        const double available = last ? std::numeric_limits<double>::infinity()
                                      : (segments[i + 1].startTick - cur) / s.ticksPerBeat;
        if (beats <= available)
            return cur + beats * s.ticksPerBeat;
        beats -= available;
        cur = segments[i + 1].startTick;
    }
}

double ticksToBeats (const std::vector<MeterSegment>& segments, double startTick, double endTick)
{
    if (segments.empty() || ! (endTick > startTick))
        return 0.0;
    double beats = 0.0;
    double cur = startTick;
    for (size_t i = segmentIndexAt (segments, cur); cur < endTick; ++i)
    {
        const auto& s = segments[i];
        const double segEnd = i + 1 == segments.size() ? endTick : std::min (endTick, segments[i + 1].startTick);
        beats += (segEnd - cur) / s.ticksPerBeat;
        cur = segEnd;
    }
    return beats;
}

std::string formatBeats (double beats)
{
    char buffer[48];
    std::snprintf (buffer, sizeof buffer, "%.2f", beats);
    std::string s = buffer;
    if (s.find ('.') != std::string::npos)
    {
        while (s.back() == '0')
            s.pop_back();
        if (s.back() == '.')
            s.pop_back();
    }
    return s;
}

std::optional<double> parseBeats (const std::string& text)
{
    const auto v = parseNumber (text);
    return v && *v > 0.0 ? v : std::nullopt;
}

std::string formatMeterText (int numerator, int denominator)
{
    return std::to_string (numerator) + "/" + std::to_string (denominator);
}

std::optional<std::pair<int, int>> parseMeterText (const std::string& text)
{
    const auto slash = text.find ('/');
    if (slash == std::string::npos)
        return std::nullopt;
    const auto n = parseCount (trim (text.substr (0, slash)));
    const auto d = parseCount (trim (text.substr (slash + 1)));
    if (! n || ! d || *n < 1 || *d < 1)
        return std::nullopt;
    return std::make_pair ((int) *n, (int) *d);
}

std::optional<double> parseTempoText (const std::string& text)
{
    std::string t = trim (text);
    const std::string note = "\xe2\x99\xa9";
    if (t.compare (0, note.size(), note) == 0)
        t = trim (t.substr (note.size()));
    if (! t.empty() && t.front() == '=')
        t = trim (t.substr (1));
    const auto v = parseNumber (t);
    return v && *v > 0.0 ? v : std::nullopt;
}

} // namespace lotro

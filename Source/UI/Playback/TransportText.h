#pragma once

// Pure text helpers for the transport strip's right-area controls (no JUCE):
// bar.beat.tick positions, beat lengths, meter and tempo text.

#include "UI/MeterSegments.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lotro
{

enum class PositionParseError { Empty, BadFormat, BarOutOfRange, BeatOutOfRange, TickOutOfRange };

struct ParsedPosition
{
    double tick = 0.0;
    std::optional<PositionParseError> error;
};

// "bar.beat.tick", bar and beat 1-based.
std::string formatPosition (const std::vector<MeterSegment>& segments, double tick);

// Accepts "5", "5.3" and "5.3.120".
ParsedPosition parsePosition (const std::string& text, const std::vector<MeterSegment>& segments);

double beatsToTicks (const std::vector<MeterSegment>& segments, double startTick, double beats);
double ticksToBeats (const std::vector<MeterSegment>& segments, double startTick, double endTick);

// At most two decimals, trailing zeros dropped.
std::string formatBeats (double beats);
// Finite and > 0.
std::optional<double> parseBeats (const std::string& text);

std::string formatMeterText (int numerator, int denominator);
// "n/d", positive integers, no range check.
std::optional<std::pair<int, int>> parseMeterText (const std::string& text);

// Accepts "<crotchet>= 93.75", "=120" and "120"; finite and > 0.
std::optional<double> parseTempoText (const std::string& text);

const char* describe (PositionParseError);

} // namespace lotro

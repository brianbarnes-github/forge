// Regression tests for the bar-alignment invariant: every line between two
// `% bar N` labels in the emitted ABC must contain tokens whose stream-advance
// durations sum to exactly one bar (`barTicks(song)`). Trailing partial bars
// at end-of-track are permitted to be short.
//
// This test pins the fix from the day we discovered Jazz Guitar bars were
// overshooting because bar-crossing chord z-pulses pushed content past the
// bar line. The Python `verify-bars` tool we used to catch that regression
// is the logical reference for what this test encodes.

#include "Core/AbcWriter.h"
#include "Core/Song.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

namespace
{
    lotro::Song songWithBass (int ppq = 480, int num = 4, int den = 4)
    {
        lotro::Song s;
        s.title          = "Bar Alignment Test";
        s.ticksPerQuarter = ppq;
        s.meterMap.push_back ({ 0, num, den });
        s.tempoMap.push_back ({ 0, 120.0 });
        return s;
    }

    lotro::Note note (int pitch, int startTick, int durationTicks, int velocity = 100)
    {
        lotro::Note n;
        n.pitch         = pitch;
        n.startTick     = startTick;
        n.durationTicks = durationTicks;
        n.velocity      = velocity;
        return n;
    }

    // Parse a single duration suffix: "", "n", "/d", or "n/d". Returns
    // (numerator, denominator) in L-units. Bare "" means 1/1.
    std::pair<int, int> parseDuration (const std::string& s)
    {
        if (s.empty()) return { 1, 1 };
        const auto slash = s.find ('/');
        if (slash == std::string::npos)
            return { std::stoi (s), 1 };
        const auto numStr = s.substr (0, slash);
        const auto denStr = s.substr (slash + 1);
        const int num = numStr.empty() ? 1 : std::stoi (numStr);
        const int den = denStr.empty() ? 2 : std::stoi (denStr);  // bare "/" defaults den=2
        return { num, den };
    }

    // Find the matching `]` for a `[` at position i. No escaping in ABC.
    size_t matchingBracket (const std::string& s, size_t i)
    {
        return s.find (']', i + 1);
    }

    // Scan a token stream and return (numerator, denominator) of its
    // total stream-advance in L-units, where L is the denominator of L:1/8
    // (i.e. eighths). Handles rests `zN`, notes `NOTE dur`, and chords
    // `[inner]` (chord advance = min inner duration). Skips `+xx+` dynamics.
    std::pair<int, int> sumAdvanceInBar (const std::string& line)
    {
        long long totalNum = 0;
        long long totalDen = 1;  // running fraction totalNum/totalDen

        auto addFraction = [&] (int n, int d)
        {
            // totalNum/totalDen += n/d.
            long long newNum = totalNum * d + (long long) n * totalDen;
            long long newDen = totalDen * d;
            const long long g = std::gcd (std::abs (newNum), newDen);
            totalNum = newNum / g;
            totalDen = newDen / g;
        };

        size_t i = 0;
        while (i < line.size())
        {
            const char c = line[i];
            if (c == '+')
            {
                // skip +dynamic+
                const auto end = line.find ('+', i + 1);
                i = (end == std::string::npos) ? line.size() : end + 1;
                continue;
            }
            if (c == '[')
            {
                const auto end = matchingBracket (line, i);
                const auto inner = line.substr (i + 1, end - i - 1);
                // find all durations inside, take min
                long long minNum = 0, minDen = 1;
                bool haveMin = false;
                size_t j = 0;
                while (j < inner.size())
                {
                    const char cc = inner[j];
                    if (cc == 'z' || (cc >= 'A' && cc <= 'G') || (cc >= 'a' && cc <= 'g')
                        || cc == '^' || cc == '_' || cc == '=')
                    {
                        // consume accidentals, note letter, octave marks
                        while (j < inner.size() && (inner[j] == '^' || inner[j] == '_' || inner[j] == '=')) ++j;
                        if (j < inner.size()) ++j;  // note letter or z
                        while (j < inner.size() && (inner[j] == ',' || inner[j] == '\'')) ++j;
                        // read duration digits/slash
                        std::string dur;
                        while (j < inner.size() && (std::isdigit ((unsigned char) inner[j]) || inner[j] == '/'))
                        {
                            dur += inner[j]; ++j;
                        }
                        const auto [n, d] = parseDuration (dur);
                        // compare to running min
                        if (! haveMin || ((long long) n * minDen < (long long) minNum * d))
                        {
                            minNum = n; minDen = d; haveMin = true;
                        }
                    }
                    else
                    {
                        ++j;
                    }
                }
                if (haveMin) addFraction ((int) minNum, (int) minDen);
                i = end + 1;
                continue;
            }
            if (c == 'z' || (c >= 'A' && c <= 'G') || (c >= 'a' && c <= 'g')
                || c == '^' || c == '_' || c == '=')
            {
                while (i < line.size() && (line[i] == '^' || line[i] == '_' || line[i] == '=')) ++i;
                if (i < line.size()) ++i;
                while (i < line.size() && (line[i] == ',' || line[i] == '\'')) ++i;
                std::string dur;
                while (i < line.size() && (std::isdigit ((unsigned char) line[i]) || line[i] == '/'))
                {
                    dur += line[i]; ++i;
                }
                const auto [n, d] = parseDuration (dur);
                addFraction (n, d);
                continue;
            }
            ++i;
        }
        return { (int) totalNum, (int) totalDen };
    }

    struct BarSum { int num, den, barIndex; };

    std::vector<BarSum> sumBarsInAbc (const std::string& abc)
    {
        std::vector<BarSum> out;
        int currentBar = 0;
        long long accNum = 0, accDen = 1;

        auto flushCurrent = [&] ()
        {
            if (currentBar > 0)
            {
                out.push_back ({ (int) accNum, (int) accDen, currentBar });
            }
            accNum = 0; accDen = 1;
        };

        size_t lineStart = 0;
        while (lineStart < abc.size())
        {
            const auto lineEnd = abc.find ('\n', lineStart);
            const auto line    = abc.substr (lineStart, (lineEnd == std::string::npos ? abc.size() : lineEnd) - lineStart);

            if (line.rfind ("% bar ", 0) == 0)
            {
                flushCurrent();
                currentBar = std::stoi (line.substr (6));
            }
            else if (currentBar > 0 && ! line.empty())
            {
                const auto [n, d] = sumAdvanceInBar (line);
                long long newNum = accNum * d + (long long) n * accDen;
                long long newDen = accDen * d;
                const long long g = std::gcd (std::abs (newNum), newDen);
                if (g > 0) { newNum /= g; newDen /= g; }
                accNum = newNum; accDen = newDen;
            }

            if (lineEnd == std::string::npos) break;
            lineStart = lineEnd + 1;
        }
        flushCurrent();
        return out;
    }
}

TEST_CASE ("bar-alignment: cluster whose chord advance spans a bar boundary stays on its own line", "[bar-alignment]")
{
    // One Lute, note at tick 1800 (still in bar 1), with an 800-tick advance
    // that would otherwise push the chord token's z-pulse past the bar line
    // at tick 1920. We expect the chord to emit on `% bar 1` with a z-pulse
    // capped at 120 (the remainder of bar 1), and the spill of 680 ticks to
    // emit as a rest on `% bar 2`.
    auto song = songWithBass();
    lotro::Track t;
    t.name       = "Lead";
    t.instrument = lotro::LotroInstrument::LuteOfAges;
    t.notes.push_back (note (/*pitch*/ 60, /*start*/ 1800, /*dur*/ 800));  // held note
    t.notes.push_back (note (/*pitch*/ 62, /*start*/ 2600, /*dur*/ 120));  // next cluster
    song.tracks.push_back (t);

    const auto abc = lotro::writeAbc (song);

    const auto sums = sumBarsInAbc (abc);
    const int eighthsPerBar = 8;  // 4/4 at L:1/8

    for (const auto& s : sums)
    {
        // Skip the final bar (may be partial).
        if ((size_t) (&s - sums.data()) + 1 == sums.size()) continue;
        const double asEighths = (double) s.num / (double) s.den;
        INFO ("bar " << s.barIndex << " summed to " << s.num << "/" << s.den << " eighths");
        CHECK (asEighths == (double) eighthsPerBar);
    }
}

TEST_CASE ("bar-alignment: dense short-note pattern with intra-bar clusters", "[bar-alignment]")
{
    // A 16 drum-hits-per-bar pattern for two bars. Every hit is short (120
    // ticks = 1/16), evenly spaced. No bar-crossing clusters; this verifies
    // the common case still sums correctly.
    auto song = songWithBass();
    lotro::Track t;
    t.name       = "Drums";
    t.instrument = lotro::LotroInstrument::Drums;
    for (int bar = 0; bar < 2; ++bar)
        for (int sixteenth = 0; sixteenth < 16; ++sixteenth)
        {
            auto n = note (/*pitch*/ 38 /* snare, maps to F */,
                           /*start*/ bar * 1920 + sixteenth * 120,
                           /*dur*/  120);
            n.isDrum = true;
            t.notes.push_back (n);
        }
    song.tracks.push_back (t);

    const auto abc = lotro::writeAbc (song);
    const auto sums = sumBarsInAbc (abc);

    // Expect 2 full bars (plus optionally a trailing partial).
    REQUIRE (sums.size() >= 2);
    for (size_t i = 0; i < 2; ++i)
    {
        const double asEighths = (double) sums[i].num / (double) sums[i].den;
        INFO ("bar " << sums[i].barIndex << " summed to " << sums[i].num << "/" << sums[i].den);
        CHECK (asEighths == 8.0);
    }
}

TEST_CASE ("bar-alignment: long held note spanning three bars does not corrupt bar sums", "[bar-alignment]")
{
    // Lute C at tick 0 lasting 3 bars (5760 ticks). Followed by another
    // cluster well past it. The held note's duration is preserved inside
    // the chord, but the chord token only advances the stream to the end
    // of bar 1 — the remaining 2 bars worth of "ring time" emit as rests
    // on bars 2 and 3.
    auto song = songWithBass();
    lotro::Track t;
    t.name       = "Hold";
    t.instrument = lotro::LotroInstrument::LuteOfAges;
    t.notes.push_back (note (60, 0, 5760));      // 3 bars at PPQ 480
    t.notes.push_back (note (62, 7680, 240));    // bar 5 onward
    song.tracks.push_back (t);

    const auto abc = lotro::writeAbc (song);
    const auto sums = sumBarsInAbc (abc);

    REQUIRE (sums.size() >= 4);
    for (size_t i = 0; i < 4; ++i)
    {
        const double asEighths = (double) sums[i].num / (double) sums[i].den;
        INFO ("bar " << sums[i].barIndex << " summed to " << sums[i].num << "/" << sums[i].den);
        CHECK (asEighths == 8.0);
    }
}

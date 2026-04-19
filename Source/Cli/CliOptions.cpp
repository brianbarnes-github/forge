#include "CliOptions.h"

namespace lotro
{

juce::String usageText()
{
    return
        "converter [OPTIONS] INPUT.mid [OUTPUT.abc]\n"
        "\n"
        "  --instrument N=NAME   Assign instrument to track N (repeatable)\n"
        "  --tempo BPM           Override detected main tempo\n"
        "  --transpose N         Global semitone transpose (pre range-clamp)\n"
        "  --list-tracks         Print track table and exit\n"
        "  --list-instruments    Print valid instrument NAME values and exit\n"
        "  -v, --verbose         Log constraint warnings to stderr\n"
        "  -h, --help            Print this help and exit\n";
}

namespace
{
    juce::String takeValue (const juce::StringArray& args, int& i, const juce::String& flag,
                            juce::String& error)
    {
        if (i + 1 >= args.size())
        {
            error = flag + " requires a value";
            return {};
        }
        return args[++i];
    }
}

CliParseResult parseCli (const juce::StringArray& rawArgs)
{
    CliParseResult result;
    CliOptions     opts;
    juce::StringArray positionals;

    for (int i = 0; i < rawArgs.size(); ++i)
    {
        const auto& arg = rawArgs[i];

        if (arg == "-h" || arg == "--help")
        {
            opts.help = true;
            result.options = opts;
            return result;
        }

        if (arg == "--list-tracks")        { opts.listTracks = true;       continue; }
        if (arg == "--list-instruments")   { opts.listInstruments = true;  continue; }
        if (arg == "-v" || arg == "--verbose") { opts.verbose = true;      continue; }

        if (arg == "--tempo")
        {
            const auto value = takeValue (rawArgs, i, arg, result.error);
            if (result.error.isNotEmpty()) return result;
            opts.tempoOverride = value.getDoubleValue();
            if (*opts.tempoOverride <= 0.0)
            {
                result.error = "--tempo must be positive (got " + value + ")";
                return result;
            }
            continue;
        }

        if (arg == "--transpose")
        {
            const auto value = takeValue (rawArgs, i, arg, result.error);
            if (result.error.isNotEmpty()) return result;
            opts.transposeSemitones = value.getIntValue();
            continue;
        }

        if (arg == "--instrument")
        {
            const auto value = takeValue (rawArgs, i, arg, result.error);
            if (result.error.isNotEmpty()) return result;

            const auto eq = value.indexOfChar ('=');
            if (eq <= 0)
            {
                result.error = "--instrument expects N=NAME (got '" + value + "')";
                return result;
            }

            const int trackIndex = value.substring (0, eq).getIntValue();
            const auto nameText = value.substring (eq + 1);

            LotroInstrument parsed;
            const auto parseError = parseName (nameText.toStdString(), parsed);
            if (! parseError.empty())
            {
                result.error = juce::String (parseError);
                return result;
            }

            opts.instrumentOverrides[trackIndex] = parsed;
            continue;
        }

        if (arg.startsWith ("-") && arg.length() > 1)
        {
            result.error = "Unknown option: " + arg;
            return result;
        }

        positionals.add (arg);
    }

    if (opts.listInstruments || opts.help)
    {
        result.options = opts;
        return result;
    }

    if (positionals.isEmpty())
    {
        result.error = "Missing input MIDI file.\n" + usageText();
        return result;
    }

    opts.inputFile = juce::File::getCurrentWorkingDirectory().getChildFile (positionals[0]);

    if (positionals.size() >= 2)
        opts.outputFile = juce::File::getCurrentWorkingDirectory().getChildFile (positionals[1]);
    else
        opts.outputFile = opts.inputFile.withFileExtension (".abc");

    result.options = opts;
    return result;
}

} // namespace lotro

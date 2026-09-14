#include "PreviewPipeline.h"

#include "Core/InstrumentAssembly.h"
#include "Core/Pipeline.h"

#include <algorithm>

namespace lotro
{

namespace
{
    bool isUnreferencedTrackInfo (const Diagnostic& d)
    {
        return d.severity == Severity::Info
            && d.source == "InstrumentAssembly"
            && d.message.find ("not referenced by any instrument") != std::string::npos;
    }
}

PreviewResult computePartPreview (const SongDocument& doc, juce::int64 partId)
{
    PreviewResult result;

    auto built = buildConfigAndRawSong (doc, { partId });

    result.assembled = assembleInstruments (built.rawSong, built.config, result.diagnostics);

    result.pipelined = result.assembled;
    runPipeline (result.pipelined, result.diagnostics);

    result.diagnostics.erase (
        std::remove_if (result.diagnostics.begin(), result.diagnostics.end(), isUnreferencedTrackInfo),
        result.diagnostics.end());

    return result;
}

} // namespace lotro

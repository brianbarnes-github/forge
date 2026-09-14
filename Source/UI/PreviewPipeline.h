#pragma once

#include "SongDocument.h"
#include "SongModelBridge.h"
#include "Core/Diagnostics.h"
#include "Core/Song.h"

#include <juce_core/juce_core.h>

namespace lotro
{

// Live per-part preview, reusing the real engine (no reimplementation of
// transpose/range logic in the UI) — see the Songsmith plan's "Live preview
// pipeline" section.
struct PreviewResult
{
    Song        assembled;    // post-assembleInstruments, pre-runPipeline
    Song        pipelined;    // post-runPipeline — the real export result
    Diagnostics diagnostics;
};

// Builds a Config+rawSong scoped to `partId` (via buildConfigAndRawSong),
// assembles it, then runs the real pipeline on a copy of the assembled
// result. "Unreferenced track" Info diagnostics from assembleInstruments
// are filtered out of the returned diagnostics — noise for a single-part
// scoped Config, not a change to assembleInstruments' own uniform behavior.
PreviewResult computePartPreview (const SongDocument& doc, juce::int64 partId);

} // namespace lotro

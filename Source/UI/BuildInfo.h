#pragma once

#include <juce_core/juce_core.h>

namespace lotro
{
    // Identifies exactly which code a SongSmith binary was built from, so a
    // user's About box can be matched back to a commit. commitCount is the
    // number of commits reachable from HEAD (monotonic along main, identical
    // for CI and local builds of the same commit); commitHash is empty when
    // the build had no git to ask.
    struct BuildInfo
    {
        juce::String version;
        int          commitCount = 0;
        juce::String commitHash;
        bool         dirty = false;
    };

    // The values baked into this binary by cmake/GenerateBuildInfo.cmake.
    BuildInfo currentBuildInfo();

    // "Version 0.1.0"
    juce::String formatVersionLine (const BuildInfo& info);

    // "Build 201 (eb054de)", "Build 201 (eb054de-dirty)", or "Build unknown".
    juce::String formatBuildLine (const BuildInfo& info);
}

#include "BuildInfo.h"

// Generated into the build tree on every build; only this file includes it,
// so a new commit recompiles one translation unit.
#include "ForgeBuildInfo.h"

namespace lotro
{
    BuildInfo currentBuildInfo()
    {
        return { FORGE_BUILD_VERSION, FORGE_BUILD_COMMIT_COUNT, FORGE_BUILD_COMMIT_HASH, FORGE_BUILD_DIRTY };
    }

    juce::String formatVersionLine (const BuildInfo& info)
    {
        return "Version " + info.version;
    }

    juce::String formatBuildLine (const BuildInfo& info)
    {
        if (info.commitHash.isEmpty())
            return "Build unknown";

        return "Build " + juce::String (info.commitCount)
             + " (" + info.commitHash + (info.dirty ? "-dirty" : "") + ")";
    }
}

#include "Core/MidiImporter.h"
#include "Core/AbcWriter.h"
#include "Core/Constraints/RangeConstraint.h"
#include "Core/Constraints/DurationConstraint.h"
#include "Core/Constraints/TempoCollapse.h"
#include "Core/Constraints/ChordConstraint.h"
#include "Core/Constraints/CollisionGuard.h"
#include "Core/Constraints/DynamicMapper.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <fstream>
#include <string>
#include <vector>

namespace
{
    lotro::Song runPipeline (const juce::File& midiFile, lotro::Diagnostics& diagnostics)
    {
        std::ifstream input (midiFile.getFullPathName().toStdString(), std::ios::binary);
        REQUIRE (input);
        auto song = lotro::importMidi (input,
                                       midiFile.getFileNameWithoutExtension().toStdString(),
                                       diagnostics);

        for (auto& track : song.tracks)
        {
            if (! track.enabled) continue;
            lotro::applyRangeConstraint    (track, diagnostics);
            lotro::applyChordConstraint    (track, diagnostics);
            lotro::applyDurationConstraint (track, song, diagnostics);
            lotro::applyTempoCollapse      (track, song, diagnostics);
            lotro::applyCollisionGuard     (track, diagnostics);
            lotro::applyDynamicMapper      (track, diagnostics);
        }

        lotro::applyTempoCollapseToSongMaps (song);

        return song;
    }

    juce::File projectRoot()
    {
        auto dir = juce::File (__FILE__).getParentDirectory().getParentDirectory();
        return dir;
    }

    bool contains (const std::string& haystack, const std::string& needle)
    {
        return haystack.find (needle) != std::string::npos;
    }

    int countOccurrences (const std::string& haystack, const std::string& needle)
    {
        int count = 0;
        size_t pos = 0;
        while ((pos = haystack.find (needle, pos)) != std::string::npos)
        {
            ++count;
            pos += needle.size();
        }
        return count;
    }
}

TEST_CASE ("end-to-end: sample MIDI converts to structurally valid ABC", "[e2e]")
{
    const auto midi = projectRoot().getChildFile ("midi").getChildFile ("Barnes Brothers Band - Pull The Wires.mid");
    REQUIRE (midi.existsAsFile());

    lotro::Diagnostics diagnostics;
    auto song = runPipeline (midi, diagnostics);

    REQUIRE_FALSE (song.tracks.empty());

    const auto abc = lotro::writeAbc (song);

    // Headers
    CHECK (contains (abc, "L:1/8"));
    CHECK (contains (abc, "K:C"));
    CHECK (contains (abc, "M:4/4"));
    CHECK (contains (abc, "Q:"));
    CHECK (contains (abc, "X:1"));

    // X: count equals enabled-with-notes track count
    int enabledTracks = 0;
    for (const auto& t : song.tracks)
        if (t.enabled && ! t.notes.empty())
            ++enabledTracks;

    CHECK (countOccurrences (abc, "X:") == enabledTracks);

    // Sanity: output is non-trivially sized
    CHECK (abc.size() > 1000);

    // LOTRO ABC letters must stay strictly in the 3-octave range C,..c':
    // never a double comma (,,) or double apostrophe (''), whatever
    // instrument the track was assigned. AbcWriter's per-instrument
    // pitch shift is what enforces this.
    CHECK_FALSE (contains (abc, ",,"));
    CHECK_FALSE (contains (abc, "''"));
}

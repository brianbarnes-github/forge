#include "SongModelBridge.h"

#include <algorithm>
#include <map>

namespace lotro
{

void appendImportedSong (SongDocument& doc, const Song& imported, int importBatch)
{
    // Only the first import sets the document's time base. A second import
    // with a different ticksPerQuarter would need every incoming note tick
    // rescaled into the document's already-established tick space to stay
    // meaningful — nobody owns that rescaling yet (open Phase 3 decision,
    // see the plan's deferred "multi-import time-base reconciliation" item)
    // — so a later import silently keeps the first import's PPQ rather than
    // clobbering or averaging it.
    if (doc.getNumTracks() == 0)
        doc.getSourceMidiNode().setProperty (SongIDs::ticksPerQuarter, imported.ticksPerQuarter, nullptr);

    for (const auto& track : imported.tracks)
    {
        auto trackTree = doc.addTrackBulk (track.name, 0, track.sourceMidiChannel, importBatch);

        for (const auto& note : track.notes)
        {
            juce::ValueTree noteTree (SongIDs::NOTE);
            noteTree.setProperty (SongIDs::pitch, note.pitch, nullptr);
            noteTree.setProperty (SongIDs::startTick, note.startTick, nullptr);
            noteTree.setProperty (SongIDs::durationTicks, note.durationTicks, nullptr);
            noteTree.setProperty (SongIDs::velocity, note.velocity, nullptr);
            noteTree.setProperty (SongIDs::isDrum, note.isDrum, nullptr);
            noteTree.setProperty (SongIDs::sourceTrackIndex, note.sourceTrackIndex, nullptr);
            noteTree.setProperty (SongIDs::sourceEventIndex, note.sourceEventIndex, nullptr);

            SongDocument::appendChildBulk (trackTree, noteTree);
        }
    }

    auto tempoMapNode = doc.getTempoMapNode();
    for (const auto& change : imported.tempoMap)
    {
        juce::ValueTree changeTree (SongIDs::TEMPO_CHANGE);
        changeTree.setProperty (SongIDs::tick, change.tick, nullptr);
        changeTree.setProperty (SongIDs::bpm, change.bpm, nullptr);
        SongDocument::appendChildBulk (tempoMapNode, changeTree);
    }

    auto meterMapNode = doc.getMeterMapNode();
    for (const auto& change : imported.meterMap)
    {
        juce::ValueTree changeTree (SongIDs::METER_CHANGE);
        changeTree.setProperty (SongIDs::tick, change.tick, nullptr);
        changeTree.setProperty (SongIDs::numerator, change.numerator, nullptr);
        changeTree.setProperty (SongIDs::denominator, change.denominator, nullptr);
        SongDocument::appendChildBulk (meterMapNode, changeTree);
    }
}

BuiltConfigAndSong buildConfigAndRawSong (const SongDocument& doc,
                                          const std::vector<juce::int64>& partIds)
{
    BuiltConfigAndSong result;
    auto& rawSong = result.rawSong;
    auto& config  = result.config;

    auto sourceMidiNode = doc.getSourceMidiNode();
    rawSong.ticksPerQuarter = (int) sourceMidiNode.getProperty (SongIDs::ticksPerQuarter);

    for (auto tempoChange : doc.getTempoMapNode())
        rawSong.tempoMap.push_back ({ (int) tempoChange.getProperty (SongIDs::tick),
                                       (double) tempoChange.getProperty (SongIDs::bpm) });

    for (auto meterChange : doc.getMeterMapNode())
        rawSong.meterMap.push_back ({ (int) meterChange.getProperty (SongIDs::tick),
                                       (int) meterChange.getProperty (SongIDs::numerator),
                                       (int) meterChange.getProperty (SongIDs::denominator) });

    auto inputMidiPath = doc.getTree().getProperty (SongIDs::inputMidiPath).toString();
    rawSong.title = inputMidiPath.isNotEmpty()
                        ? juce::File (inputMidiPath).getFileNameWithoutExtension().toStdString()
                        : Song{}.title;

    std::map<juce::int64, int> trackIdToIndex;
    for (int i = 0; i < sourceMidiNode.getNumChildren(); ++i)
    {
        auto trackTree = sourceMidiNode.getChild (i);

        Track track;
        track.name              = trackTree.getProperty (SongIDs::name).toString().toStdString();
        track.sourceMidiChannel = (int) trackTree.getProperty (SongIDs::sourceMidiChannel);

        for (auto noteTree : trackTree)
        {
            Note note;
            note.pitch            = (int) noteTree.getProperty (SongIDs::pitch);
            note.startTick        = (int) noteTree.getProperty (SongIDs::startTick);
            note.durationTicks    = (int) noteTree.getProperty (SongIDs::durationTicks);
            note.velocity         = (int) noteTree.getProperty (SongIDs::velocity);
            note.isDrum           = (bool) noteTree.getProperty (SongIDs::isDrum);
            note.sourceTrackIndex = (int) noteTree.getProperty (SongIDs::sourceTrackIndex);
            note.sourceEventIndex = (int) noteTree.getProperty (SongIDs::sourceEventIndex);
            track.notes.push_back (note);
        }

        rawSong.tracks.push_back (track);

        auto trackId = (juce::int64) trackTree.getProperty (SongIDs::trackId);
        trackIdToIndex[trackId] = i;
    }

    config.input  = doc.getTree().getProperty (SongIDs::inputMidiPath).toString().toStdString();
    config.output = std::nullopt;

    // SONG's title/transcriber/tempoBpm are override fields that are
    // deliberately ABSENT (not defaulted) on a fresh document, so "no
    // override" must be distinguished from "explicitly set" via hasProperty
    // — a defaulted std::optional("") here would silently suppress the
    // imported MIDI's own title/tempo. PART's label/drumMapPath, in
    // contrast, are always present (addPart seeds them to "") and use plain
    // string-emptiness to mean "unset", so they map "" -> nullopt instead.
    auto songTree = doc.getTree();
    config.title = songTree.hasProperty (SongIDs::title)
                       ? std::optional (songTree.getProperty (SongIDs::title).toString().toStdString())
                       : std::nullopt;
    config.transcriber = songTree.hasProperty (SongIDs::transcriber)
                       ? std::optional (songTree.getProperty (SongIDs::transcriber).toString().toStdString())
                       : std::nullopt;
    config.tempo = songTree.hasProperty (SongIDs::tempoBpm)
                       ? std::optional ((double) songTree.getProperty (SongIDs::tempoBpm))
                       : std::nullopt;
    config.transpose = (int) songTree.getProperty (SongIDs::globalTranspose);

    auto partsNode = doc.getPartsNode();
    for (auto partTree : partsNode)
    {
        if (! partIds.empty())
        {
            auto partId = (juce::int64) partTree.getProperty (SongIDs::partId);
            if (std::find (partIds.begin(), partIds.end(), partId) == partIds.end())
                continue;
        }

        ConfigInstrument inst;
        inst.x    = (int) partTree.getProperty (SongIDs::x);
        inst.name = partTree.getProperty (SongIDs::instrumentName).toString().toStdString();

        auto label = partTree.getProperty (SongIDs::label).toString();
        inst.label = label.isNotEmpty() ? std::optional (label.toStdString()) : std::nullopt;

        auto drumMapPath = partTree.getProperty (SongIDs::drumMapPath).toString();
        inst.drumMap = drumMapPath.isNotEmpty() ? std::optional (drumMapPath.toStdString()) : std::nullopt;

        for (int a = 0; a < SongDocument::getNumAssignments (partTree); ++a)
        {
            auto assignment = SongDocument::getAssignment (partTree, a);
            auto refTrackId = (juce::int64) assignment.getProperty (SongIDs::trackId);

            auto found = trackIdToIndex.find (refTrackId);
            if (found == trackIdToIndex.end())
                continue;

            ConfigSource source;
            source.midiTrackIndex     = found->second;
            source.transposeSemitones = (int) assignment.getProperty (SongIDs::transposeSemitones);
            source.volumePercent      = (int) assignment.getProperty (SongIDs::volumePercent);
            inst.sources.push_back (source);
        }

        config.instruments.push_back (inst);
    }

    return result;
}

} // namespace lotro

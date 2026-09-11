#include "SongModelBridge.h"

#include "Core/MidiImporter.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>

namespace lotro
{

namespace
{
    int rescaleTick (int tick, int docPpq, int importedPpq)
    {
        return (int) std::lround ((double) tick * (double) docPpq / (double) importedPpq);
    }

    bool isExactRescale (int tick, int docPpq, int importedPpq)
    {
        return ((long long) tick * (long long) docPpq) % (long long) importedPpq == 0;
    }
}

void appendImportedSong (SongDocument& doc, const Song& imported, int importBatch,
                         Diagnostics* diagnostics)
{
    // R1: only the first import sets the document's time base and
    // TEMPO_MAP/METER_MAP. A later import describes a different timeline,
    // not a continuation of the first's — concatenating two files' tempo/
    // meter maps would be meaningless (TempoCollapse assumes one sorted
    // timeline), so a later import's maps are compared (after R2 rescaling)
    // against the document's and, if they differ, reported via a single
    // Warning Diagnostic rather than applied.
    //
    // "First import" is defined as an empty TEMPO_MAP, not
    // doc.getNumTracks() == 0: importMidi always seeds a non-empty tempoMap
    // (defaulting to {0, 120.0} when the source file has none), so a first
    // MIDI file whose every track was silent (dropped for having no notes,
    // leaving zero tracks) would otherwise still read as "no import has
    // landed yet" on the next call, causing a second import to re-set
    // ticksPerQuarter and re-append onto maps that already hold the first
    // file's entries.
    const bool isFirstImport = doc.getTempoMapNode().getNumChildren() == 0;

    if (isFirstImport)
        doc.getSourceMidiNode().setProperty (SongIDs::ticksPerQuarter, imported.ticksPerQuarter, nullptr);

    const int docPpq      = (int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter);
    const int importedPpq = imported.ticksPerQuarter;
    const bool needsRescale = ! isFirstImport && importedPpq != docPpq;

    int roundedValueCount = 0;

    for (const auto& track : imported.tracks)
    {
        auto trackTree = doc.addTrackBulk (track.name, 0, track.sourceMidiChannel, importBatch);

        for (const auto& note : track.notes)
        {
            int startTick     = note.startTick;
            int durationTicks = note.durationTicks;

            if (needsRescale)
            {
                if (! isExactRescale (startTick, docPpq, importedPpq))     ++roundedValueCount;
                if (! isExactRescale (durationTicks, docPpq, importedPpq)) ++roundedValueCount;

                startTick     = rescaleTick (startTick, docPpq, importedPpq);
                durationTicks = rescaleTick (durationTicks, docPpq, importedPpq);
            }

            juce::ValueTree noteTree (SongIDs::NOTE);
            noteTree.setProperty (SongIDs::pitch, note.pitch, nullptr);
            noteTree.setProperty (SongIDs::startTick, startTick, nullptr);
            noteTree.setProperty (SongIDs::durationTicks, durationTicks, nullptr);
            noteTree.setProperty (SongIDs::velocity, note.velocity, nullptr);
            noteTree.setProperty (SongIDs::isDrum, note.isDrum, nullptr);
            noteTree.setProperty (SongIDs::sourceTrackIndex, note.sourceTrackIndex, nullptr);
            noteTree.setProperty (SongIDs::sourceEventIndex, note.sourceEventIndex, nullptr);

            SongDocument::appendChildBulk (trackTree, noteTree);
        }
    }

    if (needsRescale && diagnostics != nullptr)
    {
        Diagnostic d;
        d.source   = "SongModelBridge";
        d.severity = roundedValueCount > 0 ? Severity::Warning : Severity::Info;
        d.message  = "Rescaled imported MIDI ticks from " + std::to_string (importedPpq)
                   + " to the document's " + std::to_string (docPpq) + " PPQ";
        if (roundedValueCount > 0)
            d.message += " (" + std::to_string (roundedValueCount) + " value(s) rounded)";
        diagnostics->push_back (std::move (d));
    }

    if (isFirstImport)
    {
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
    else if (diagnostics != nullptr)
    {
        auto tempoMapNode = doc.getTempoMapNode();
        bool timelineDiffers = tempoMapNode.getNumChildren() != (int) imported.tempoMap.size();

        for (size_t i = 0; ! timelineDiffers && i < imported.tempoMap.size(); ++i)
        {
            const auto rescaledTick = needsRescale
                ? rescaleTick (imported.tempoMap[i].tick, docPpq, importedPpq)
                : imported.tempoMap[i].tick;
            auto existing = tempoMapNode.getChild ((int) i);
            if ((int) existing.getProperty (SongIDs::tick) != rescaledTick
                || (double) existing.getProperty (SongIDs::bpm) != imported.tempoMap[i].bpm)
                timelineDiffers = true;
        }

        if (! timelineDiffers)
        {
            auto meterMapNode = doc.getMeterMapNode();
            timelineDiffers = meterMapNode.getNumChildren() != (int) imported.meterMap.size();

            for (size_t i = 0; ! timelineDiffers && i < imported.meterMap.size(); ++i)
            {
                const auto rescaledTick = needsRescale
                    ? rescaleTick (imported.meterMap[i].tick, docPpq, importedPpq)
                    : imported.meterMap[i].tick;
                auto existing = meterMapNode.getChild ((int) i);
                if ((int) existing.getProperty (SongIDs::tick) != rescaledTick
                    || (int) existing.getProperty (SongIDs::numerator) != imported.meterMap[i].numerator
                    || (int) existing.getProperty (SongIDs::denominator) != imported.meterMap[i].denominator)
                    timelineDiffers = true;
            }
        }

        if (timelineDiffers)
        {
            Diagnostic d;
            d.source   = "SongModelBridge";
            d.severity = Severity::Warning;
            d.message  = "Later MIDI import's tempo/meter timeline differs from the document's; "
                         "the document's timeline was kept and the incoming file's was ignored";
            diagnostics->push_back (std::move (d));
        }
    }
}

bool importMidiFile (SongDocument& doc, const juce::File& midiFile, int importBatch,
                     Diagnostics& diagnostics)
{
    std::ifstream input (midiFile.getFullPathName().toStdString(), std::ios::binary);
    if (! input)
    {
        Diagnostic d;
        d.source   = "SongModelBridge";
        d.severity = Severity::Error;
        d.message  = "Could not open MIDI file: " + midiFile.getFullPathName().toStdString();
        diagnostics.push_back (std::move (d));
        return false;
    }

    const auto sourceName = midiFile.getFileNameWithoutExtension().toStdString();

    Song imported;
    try
    {
        imported = importMidi (input, sourceName, diagnostics);
    }
    catch (const MidiImportError& e)
    {
        Diagnostic d;
        d.source   = "SongModelBridge";
        d.severity = Severity::Error;
        d.message  = std::string ("Malformed MIDI file: ") + e.what();
        diagnostics.push_back (std::move (d));
        return false;
    }

    appendImportedSong (doc, imported, importBatch, &diagnostics);

    // R3: first import's filename wins for SONG.inputMidiPath (and thus the
    // bridge-derived rawSong.title) — only set it when currently empty.
    if (doc.getTree().getProperty (SongIDs::inputMidiPath).toString().isEmpty())
        doc.getTree().setProperty (SongIDs::inputMidiPath, midiFile.getFullPathName(), nullptr);

    return true;
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

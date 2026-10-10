#include "SongModelBridge.h"

#include "Core/LotroInstrument.h"
#include "Core/MidiImporter.h"
#include "MidiImportPlan.h"
#include "SongsmithColours.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <numeric>
#include <set>
#include <sstream>

namespace lotro
{

namespace
{
    // A-R3: LCM raise cap. 16 * 960. Guaranteed to stay well clear of this
    // for the multiples-of-24 PPQ family (96/120/192/240/384/480/960/1920,
    // the values MIDI files and DAWs actually use) — the worst pairwise LCM
    // in that family is far below the cap. NOT a guarantee for arbitrary
    // PPQs in the 96..1920 range: e.g. 1000 against a 960-PPQ document gives
    // lcm(960, 1000) = 24000, over the cap, so that pair silently takes the
    // Phase 3 lossy-downscale path below instead.
    constexpr long long kLcmCap = 15360;

    int rescaleTick (int tick, int docPpq, int importedPpq)
    {
        return (int) std::lround ((double) tick * (double) docPpq / (double) importedPpq);
    }

    bool isExactRescale (int tick, int docPpq, int importedPpq)
    {
        return ((long long) tick * (long long) docPpq) % (long long) importedPpq == 0;
    }

    // A-R3(a)/(b): multiplies every existing NOTE start/duration tick, every
    // EVENT tick, MIDI_TRACK endTick (conductor included), every stored SECTION
    // startTick/endTick, and every
    // TEMPO_CHANGE/METER_CHANGE tick by `factor`. The caller guarantees
    // factor == newPpq / docPpq is an exact integer (newPpq is an LCM, hence
    // a multiple of docPpq), so this never rounds. Returns the number of NOTE
    // nodes rescaled, for the "Raised document time base..." diagnostic.
    int rescaleExistingDocumentTicks (SongDocument& doc, int factor)
    {
        int noteCount = 0;
        auto sourceMidiNode = doc.getSourceMidiNode();
        for (int t = 0; t < sourceMidiNode.getNumChildren(); ++t)
        {
            auto trackTree = sourceMidiNode.getChild (t);
            for (int n = 0; n < SongDocument::getNotesNode (trackTree).getNumChildren(); ++n)
            {
                auto noteTree = SongDocument::getNotesNode (trackTree).getChild (n);
                const int startTick     = (int) noteTree.getProperty (SongIDs::startTick);
                const int durationTicks = (int) noteTree.getProperty (SongIDs::durationTicks);
                noteTree.setProperty (SongIDs::startTick, startTick * factor, nullptr);
                noteTree.setProperty (SongIDs::durationTicks, durationTicks * factor, nullptr);
                ++noteCount;
            }

            for (auto eventTree : SongDocument::getEventsNode (trackTree))
                eventTree.setProperty (SongIDs::tick, (int) eventTree.getProperty (SongIDs::tick) * factor, nullptr);
            trackTree.setProperty (SongIDs::endTick, (int) trackTree.getProperty (SongIDs::endTick) * factor, nullptr);

            // Stored sections are tick ranges on the same timeline as the notes
            // they own; leaving them behind would desync them from the notes.
            for (auto sectionTree : trackTree.getChildWithName (SongIDs::SECTIONS))
            {
                sectionTree.setProperty (SongIDs::startTick, (int) sectionTree.getProperty (SongIDs::startTick) * factor, nullptr);
                sectionTree.setProperty (SongIDs::endTick, (int) sectionTree.getProperty (SongIDs::endTick) * factor, nullptr);
            }
        }

        auto tempoMapNode = doc.getTempoMapNode();
        for (int i = 0; i < tempoMapNode.getNumChildren(); ++i)
        {
            auto c = tempoMapNode.getChild (i);
            c.setProperty (SongIDs::tick, (int) c.getProperty (SongIDs::tick) * factor, nullptr);
        }

        auto meterMapNode = doc.getMeterMapNode();
        for (int i = 0; i < meterMapNode.getNumChildren(); ++i)
        {
            auto c = meterMapNode.getChild (i);
            c.setProperty (SongIDs::tick, (int) c.getProperty (SongIDs::tick) * factor, nullptr);
        }

        return noteCount;
    }

    // A-R4: minimal, human-friendly BPM formatting ("120" instead of "120.0"
    // when the value is a whole number; otherwise the natural decimal form).
    std::string formatBpm (double bpm)
    {
        std::ostringstream oss;
        if (std::fabs (bpm - std::floor (bpm)) < 1e-9)
            oss << (long long) bpm;
        else
            oss << bpm;
        return oss.str();
    }

    // Shared by both import paths. `plan` == nullptr is the raw-less path
    // (appendImportedSong): NOTEs only, conductor and EVENTS untouched.
    void appendImport (SongDocument& doc, const Song& imported, const MidiImportPlan* plan,
                       int importBatch, Diagnostics& diagnostics)
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
        // file's entries. A tempo replace (ImportOptions::tempo == replace)
        // overrides this: the file's maps replace the document's.
        const bool isFirstImport = doc.getTempoMapNode().getNumChildren() == 0;
        // A later import whose plan writes the conductor is a tempo replace.
        const bool replacingTempo = plan != nullptr && plan->writesConductor && ! isFirstImport;

        if (isFirstImport)
            doc.getSourceMidiNode().setProperty (SongIDs::ticksPerQuarter, imported.ticksPerQuarter, nullptr);

        int docPpq             = (int) doc.getSourceMidiNode().getProperty (SongIDs::ticksPerQuarter);
        const int importedPpq  = imported.ticksPerQuarter;

        // A-R3: when a later import's PPQ differs from the document's, prefer
        // RAISING the document's time base to lcm(docPpq, importedPpq) — an
        // exact, lossless integer rescale on both the existing document's ticks
        // and the incoming ticks — over the Phase 3 lossy-downscale-into-docPpq
        // fallback, as long as the LCM stays under a sane cap.
        bool raisedDocumentTimeBase     = false;
        const int raisedFromPpq         = docPpq;
        int existingNotesRescaledCount  = 0;

        if (! isFirstImport && importedPpq != docPpq)
        {
            const long long lcmPpq = std::lcm ((long long) docPpq, (long long) importedPpq);
            if (lcmPpq <= kLcmCap && (int) lcmPpq > docPpq)
            {
                const int newPpq = (int) lcmPpq;
                const int existingRescaleFactor = newPpq / docPpq;

                existingNotesRescaledCount = rescaleExistingDocumentTicks (doc, existingRescaleFactor);

                doc.getSourceMidiNode().setProperty (SongIDs::ticksPerQuarter, newPpq, nullptr);
                // A-R3(c): recorded undo steps hold pre-rescale tick values and
                // would corrupt the document if replayed, so the raise wipes the
                // undo history outright.
                doc.getUndoManager().clearUndoHistory();

                raisedDocumentTimeBase = true;
                docPpq = newPpq;
            }
            // else: either the LCM is over the cap (falls through to the
            // Phase 3 lossy fallback below, unchanged) or the LCM already equals
            // docPpq (importedPpq divides docPpq exactly — the exact-rescale
            // formula below already handles this losslessly, so no raise, and
            // per Ruling A-R3 this keeps the Phase 3 diagnostic wording).
        }

        const bool needsRescale = ! isFirstImport && importedPpq != docPpq;

        int roundedValueCount    = 0;
        int zeroLengthNoteCount  = 0;
        int rescaledNoteCount    = 0;

        // Tracks already in the document per GM family, so each new same-family
        // track gets the next shade — continuing across imports.
        std::array<int, SongsmithColours::numGmFamilies> familyCounts {};
        for (int i = 0; i < doc.getNumTracks(); ++i)
        {
            const auto existing = doc.getTrack (i);
            if (! SongDocument::isAssignableTrack (existing))
                continue;
            ++familyCounts[(size_t) SongsmithColours::gmFamilyFor ((int) existing.getProperty (SongIDs::sourceProgram),
                                                                 (int) existing.getProperty (SongIDs::sourceMidiChannel))];
        }

        int rescaledEventValueCount = 0;
        auto rescaleOther = [&] (int tick)
        {
            if (! needsRescale)
                return tick;
            ++rescaledEventValueCount;
            if (! isExactRescale (tick, docPpq, importedPpq))
                ++roundedValueCount;
            return rescaleTick (tick, docPpq, importedPpq);
        };

        auto appendEvents = [&] (juce::ValueTree eventsNode, const std::vector<PlannedEvent>& events)
        {
            for (const auto& e : events)
            {
                juce::ValueTree eventTree (SongIDs::EVENT);
                eventTree.setProperty (SongIDs::tick, rescaleOther (e.tick), nullptr);
                eventTree.setProperty (SongIDs::order, e.order, nullptr);
                if (e.relocatedFrom >= 0)
                    eventTree.setProperty (SongIDs::relocatedFrom, e.relocatedFrom, nullptr);
                eventTree.setProperty (SongIDs::data, juce::var (juce::MemoryBlock (e.bytes.data(), e.bytes.size())), nullptr);
                SongDocument::appendChildBulk (eventsNode, eventTree);
            }
        };

        auto addSongTrack = [&] (const Track& track, const std::vector<PlannedNoteLink>* links) -> juce::ValueTree
        {
            const auto family = SongsmithColours::gmFamilyFor (track.sourceProgram, track.sourceMidiChannel);
            const auto colorArgb = (int) SongsmithColours::trackColourFor (family, familyCounts[(size_t) family]++);
            auto trackTree = doc.addTrackBulk (track.name, colorArgb, track.sourceMidiChannel, importBatch);
            trackTree.setProperty (SongIDs::sourceProgram, track.sourceProgram, nullptr);

            for (size_t n = 0; n < track.notes.size(); ++n)
            {
                const auto& note = track.notes[n];
                int startTick     = note.startTick;
                int durationTicks = note.durationTicks;

                if (needsRescale)
                {
                    ++rescaledNoteCount;

                    if (! isExactRescale (startTick, docPpq, importedPpq))     ++roundedValueCount;
                    if (! isExactRescale (durationTicks, docPpq, importedPpq)) ++roundedValueCount;

                    startTick     = rescaleTick (startTick, docPpq, importedPpq);
                    durationTicks = rescaleTick (durationTicks, docPpq, importedPpq);

                    // A lossy downscale can round a nonzero duration all the way
                    // to 0. DurationConstraint drops zero-duration notes later in
                    // the pipeline without a Diagnostic of its own, so this is
                    // the only layer positioned to report the loss. The bridge
                    // does NOT clamp/invent a duration — that would be a musical
                    // decision it isn't allowed to make — it only reports the
                    // note is written as-is (duration 0) and will be dropped.
                    if (durationTicks == 0)
                        ++zeroLengthNoteCount;
                }

                juce::ValueTree noteTree (SongIDs::NOTE);
                noteTree.setProperty (SongIDs::pitch, note.pitch, nullptr);
                noteTree.setProperty (SongIDs::startTick, startTick, nullptr);
                noteTree.setProperty (SongIDs::durationTicks, durationTicks, nullptr);
                noteTree.setProperty (SongIDs::velocity, note.velocity, nullptr);
                noteTree.setProperty (SongIDs::isDrum, note.isDrum, nullptr);
                noteTree.setProperty (SongIDs::sourceTrackIndex, note.sourceTrackIndex, nullptr);
                noteTree.setProperty (SongIDs::sourceEventIndex, note.sourceEventIndex, nullptr);

                if (links != nullptr)
                {
                    const auto& link = (*links)[n];
                    noteTree.setProperty (SongIDs::channel, link.channel, nullptr);
                    noteTree.setProperty (SongIDs::onOrder, link.onOrder, nullptr);
                    if (! link.offSynthesized)
                        noteTree.setProperty (SongIDs::offOrder, link.offOrder, nullptr);
                    noteTree.setProperty (SongIDs::offSynthesized, link.offSynthesized, nullptr);
                    noteTree.setProperty (SongIDs::offVelocity, link.offVelocity, nullptr);
                    noteTree.setProperty (SongIDs::offIsNoteOnZero, link.offIsNoteOnZero, nullptr);
                }
                else
                {
                    noteTree.setProperty (SongIDs::channel, note.isDrum ? 10 : 1, nullptr);
                    noteTree.setProperty (SongIDs::offVelocity, 64, nullptr);
                }

                SongDocument::appendChildBulk (SongDocument::getNotesNode (trackTree), noteTree);
            }
            return trackTree;
        };

        if (plan == nullptr)
        {
            for (const auto& track : imported.tracks)
                addSongTrack (track, nullptr);
        }
        else
        {
            for (const auto& planned : plan->tracks)
            {
                juce::ValueTree trackTree;
                if (planned.songTrackIndex >= 0)
                {
                    const Track& source = planned.mergedTrack.has_value()
                                              ? *planned.mergedTrack
                                              : imported.tracks[(size_t) planned.songTrackIndex];
                    trackTree = addSongTrack (source, &planned.noteLinks);
                }
                else
                {
                    // Note-less tracks take their family's next shade without
                    // advancing it, so they never shift note tracks' colours.
                    const auto family = SongsmithColours::gmFamilyFor (planned.sourceProgram, planned.sourceMidiChannel);
                    const auto colorArgb = (int) SongsmithColours::trackColourFor (family, familyCounts[(size_t) family]);
                    trackTree = doc.addTrackBulk (planned.name, colorArgb, planned.sourceMidiChannel, importBatch);
                    trackTree.setProperty (SongIDs::sourceProgram, planned.sourceProgram, nullptr);
                }

                trackTree.setProperty (SongIDs::sourceTrackIndex, planned.rawTrackIndex, nullptr);
                trackTree.setProperty (SongIDs::defaultChannel, planned.defaultChannel, nullptr);
                trackTree.setProperty (SongIDs::endTick, rescaleOther (planned.endTick), nullptr);
                appendEvents (SongDocument::getEventsNode (trackTree), planned.events);
            }

            if (plan->writesConductor) // first import, or a tempo replace
            {
                auto conductor = doc.getConductorTrack();
                auto conductorEvents = SongDocument::getEventsNode (conductor);
                if (replacingTempo)
                    conductorEvents.removeAllChildren (nullptr);   // the file's conductor replaces it wholesale
                appendEvents (conductorEvents, plan->conductorEvents);   // rescaled when the PPQs differ
                conductor.setProperty (SongIDs::endTick, rescaleOther (plan->conductorEndTick), nullptr);
            }
        }

        if (raisedDocumentTimeBase)
        {
            // A-R3(e): exactly one Info diagnostic for the raise. No Warning is
            // possible on this path — a raise is always an exact integer
            // multiply on both sides (lcm's own definition), nothing rounds.
            Diagnostic d;
            d.source   = "SongModelBridge";
            d.severity = Severity::Info;
            d.message  = "Raised document time base from " + std::to_string (raisedFromPpq)
                       + " to " + std::to_string (docPpq) + " PPQ ("
                       + std::to_string (existingNotesRescaledCount) + " existing note(s) rescaled)"
                       + "; undo history cleared";
            diagnostics.push_back (std::move (d));
        }
        // Only emit the Phase 3 rescale diagnostic when a rescale actually
        // touched at least one note, event tick or endTick — a trackless later import sets
        // needsRescale via the PPQ mismatch alone but rescales nothing, and
        // claiming a rescale happened would be misleading.
        else if (needsRescale && (rescaledNoteCount > 0 || rescaledEventValueCount > 0))
        {
            Diagnostic d;
            d.source   = "SongModelBridge";
            // A zero-length note is always a Warning, even on the (currently
            // unreachable in practice) chance it coincided with an otherwise
            // "exact" rescale — silently losing a note is never an Info-level
            // event.
            d.severity = (roundedValueCount > 0 || zeroLengthNoteCount > 0) ? Severity::Warning : Severity::Info;
            d.message  = "Rescaled imported MIDI ticks from " + std::to_string (importedPpq)
                       + " to the document's " + std::to_string (docPpq) + " PPQ";
            if (roundedValueCount > 0 && zeroLengthNoteCount > 0)
                d.message += " (" + std::to_string (roundedValueCount) + " value(s) rounded, "
                           + std::to_string (zeroLengthNoteCount) + " note(s) reduced to zero length and will be dropped)";
            else if (roundedValueCount > 0)
                d.message += " (" + std::to_string (roundedValueCount) + " value(s) rounded)";
            else if (zeroLengthNoteCount > 0)
                d.message += " (" + std::to_string (zeroLengthNoteCount) + " note(s) reduced to zero length and will be dropped)";
            diagnostics.push_back (std::move (d));
        }

        if (isFirstImport || replacingTempo)
        {
            auto tempoMapNode = doc.getTempoMapNode();
            auto meterMapNode = doc.getMeterMapNode();
            if (replacingTempo)
            {
                tempoMapNode.removeAllChildren (nullptr);
                meterMapNode.removeAllChildren (nullptr);
            }
            // The conductor events above already counted any inexact rescale, so the
            // map ticks are converted without a second count.
            const auto mapTick = [&] (int tick) { return needsRescale ? rescaleTick (tick, docPpq, importedPpq) : tick; };

            for (const auto& change : imported.tempoMap)
            {
                juce::ValueTree changeTree (SongIDs::TEMPO_CHANGE);
                changeTree.setProperty (SongIDs::tick, mapTick (change.tick), nullptr);
                changeTree.setProperty (SongIDs::bpm, change.bpm, nullptr);
                SongDocument::appendChildBulk (tempoMapNode, changeTree);
            }

            for (const auto& change : imported.meterMap)
            {
                juce::ValueTree changeTree (SongIDs::METER_CHANGE);
                changeTree.setProperty (SongIDs::tick, mapTick (change.tick), nullptr);
                changeTree.setProperty (SongIDs::numerator, change.numerator, nullptr);
                changeTree.setProperty (SongIDs::denominator, change.denominator, nullptr);
                SongDocument::appendChildBulk (meterMapNode, changeTree);
            }
        }
        else
        {
            auto tempoMapNode = doc.getTempoMapNode();
            bool tempoDiffers = tempoMapNode.getNumChildren() != (int) imported.tempoMap.size();

            for (size_t i = 0; ! tempoDiffers && i < imported.tempoMap.size(); ++i)
            {
                const auto rescaledTick = needsRescale
                    ? rescaleTick (imported.tempoMap[i].tick, docPpq, importedPpq)
                    : imported.tempoMap[i].tick;
                auto existing = tempoMapNode.getChild ((int) i);
                if ((int) existing.getProperty (SongIDs::tick) != rescaledTick
                    || std::fabs ((double) existing.getProperty (SongIDs::bpm) - imported.tempoMap[i].bpm) >= 1e-9)
                    tempoDiffers = true;
            }

            auto meterMapNode = doc.getMeterMapNode();
            bool meterDiffers = meterMapNode.getNumChildren() != (int) imported.meterMap.size();

            for (size_t i = 0; ! meterDiffers && i < imported.meterMap.size(); ++i)
            {
                const auto rescaledTick = needsRescale
                    ? rescaleTick (imported.meterMap[i].tick, docPpq, importedPpq)
                    : imported.meterMap[i].tick;
                auto existing = meterMapNode.getChild ((int) i);
                if ((int) existing.getProperty (SongIDs::tick) != rescaledTick
                    || (int) existing.getProperty (SongIDs::numerator) != imported.meterMap[i].numerator
                    || (int) existing.getProperty (SongIDs::denominator) != imported.meterMap[i].denominator)
                    meterDiffers = true;
            }

            // A-R4: name the actual first tempo/meter values on both sides
            // rather than the old generic "differs... was ignored" wording, so
            // the user knows exactly what's inconsistent. Tempo takes
            // precedence when both differ (the more audible discrepancy);
            // meter-only gets its own wording per the ruling.
            if (tempoDiffers)
            {
                const double fileBpm = imported.tempoMap.empty() ? 0.0 : imported.tempoMap[0].bpm;
                const double docBpm  = tempoMapNode.getNumChildren() == 0
                                           ? 0.0 : (double) tempoMapNode.getChild (0).getProperty (SongIDs::bpm);

                Diagnostic d;
                d.source   = "SongModelBridge";
                d.severity = Severity::Warning;
                d.message  = "Imported file's tempo map differs from the document's (file starts at "
                           + formatBpm (fileBpm) + " BPM, document at " + formatBpm (docBpm)
                           + " BPM); its tracks will play at the document's tempo";
                diagnostics.push_back (std::move (d));
            }
            else if (meterDiffers)
            {
                const int fileNum = imported.meterMap.empty() ? 4 : imported.meterMap[0].numerator;
                const int fileDen = imported.meterMap.empty() ? 4 : imported.meterMap[0].denominator;
                const int docNum  = meterMapNode.getNumChildren() == 0
                                         ? 4 : (int) meterMapNode.getChild (0).getProperty (SongIDs::numerator);
                const int docDen  = meterMapNode.getNumChildren() == 0
                                         ? 4 : (int) meterMapNode.getChild (0).getProperty (SongIDs::denominator);

                Diagnostic d;
                d.source   = "SongModelBridge";
                d.severity = Severity::Warning;
                d.message  = "Imported file's meter map differs from the document's (file starts at "
                           + std::to_string (fileNum) + "/" + std::to_string (fileDen)
                           + ", document at " + std::to_string (docNum) + "/" + std::to_string (docDen)
                           + "); its tracks will play at the document's tempo";
                diagnostics.push_back (std::move (d));
            }
        }
    }
}

void appendImportedSong (SongDocument& doc, const Song& imported, int importBatch,
                         Diagnostics& diagnostics)
{
    appendImport (doc, imported, nullptr, importBatch, diagnostics);
}

bool appendImportedMidi (SongDocument& doc, const Song& imported, const RawMidiFile& raw,
                         int importBatch, Diagnostics& diagnostics,
                         const Diagnostics& importerDiagnostics, const ImportOptions& options)
{
    const bool isFirstImport = doc.getTempoMapNode().getNumChildren() == 0;

    Diagnostics planDiagnostics;
    MidiImportPlan plan;
    try
    {
        plan = planMidiImport (imported, raw, isFirstImport, planDiagnostics, options);
    }
    catch (const MidiImportPlanError& e)
    {
        Diagnostic d;
        d.source   = "SongModelBridge";
        d.severity = Severity::Error;
        d.message  = std::string ("Could not import MIDI file: ") + e.what();
        diagnostics.push_back (std::move (d));
        return false;
    }

    // importMidi's trackIndex counts only tracks with notes; point it at the
    // document row (SOURCE_MIDI child index) instead.
    const int firstRow = doc.getNumTracks();
    std::map<int, int> rowForSongTrack;
    for (int p = 0; p < (int) plan.tracks.size(); ++p)
    {
        const auto& planned = plan.tracks[(size_t) p];
        if (planned.mergedTrack.has_value())
        {
            for (const int songTrack : planned.mergedSongTrackIndices)
                rowForSongTrack[songTrack] = firstRow + p;   // every folded-in track lands on the merged row
        }
        else if (planned.songTrackIndex >= 0)
            rowForSongTrack[planned.songTrackIndex] = firstRow + p;
    }

    for (auto d : importerDiagnostics)
    {
        if (d.trackIndex >= 0)
        {
            const auto found = rowForSongTrack.find (d.trackIndex);
            d.trackIndex = found != rowForSongTrack.end() ? found->second : -1;
        }
        diagnostics.push_back (std::move (d));
    }
    diagnostics.insert (diagnostics.end(), planDiagnostics.begin(), planDiagnostics.end());

    appendImport (doc, imported, &plan, importBatch, diagnostics);
    return true;
}

namespace
{
    // Opens and parses `midiFile` with both parsers. On failure appends an Error
    // diagnostic (source "SongModelBridge") to `diagnostics` and returns false.
    bool parseMidiFile (const juce::File& midiFile, Song& imported, RawMidiFile& raw,
                        Diagnostics& importerDiagnostics, Diagnostics& diagnostics)
    {
        juce::MemoryBlock block;
        if (! midiFile.existsAsFile() || ! midiFile.loadFileAsData (block))
        {
            Diagnostic d;
            d.source   = "SongModelBridge";
            d.severity = Severity::Error;
            d.message  = "Could not open MIDI file: " + midiFile.getFullPathName().toStdString();
            diagnostics.push_back (std::move (d));
            return false;
        }

        const auto sourceName = midiFile.getFileNameWithoutExtension().toStdString();
        const auto* begin = static_cast<const std::uint8_t*> (block.getData());
        const std::vector<std::uint8_t> bytes (begin, begin + block.getSize());

        try
        {
            std::istringstream input (std::string (bytes.begin(), bytes.end()), std::ios::binary);
            imported = importMidi (input, sourceName, importerDiagnostics);
            raw      = readMidiBytes (bytes, sourceName);
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
        return true;
    }
}

std::optional<int> previewImportTrackCount (const juce::File& midiFile)
{
    Song imported;
    RawMidiFile raw;
    Diagnostics importerDiagnostics, discarded;
    if (! parseMidiFile (midiFile, imported, raw, importerDiagnostics, discarded))
        return std::nullopt;

    try
    {
        return (int) planMidiImport (imported, raw, true, discarded).tracks.size();
    }
    catch (const MidiImportPlanError&)
    {
        return std::nullopt;
    }
}

bool importMidiFile (SongDocument& doc, const juce::File& midiFile, int importBatch,
                     Diagnostics& diagnostics, const ImportOptions& options)
{
    Song imported;
    RawMidiFile raw;
    Diagnostics importerDiagnostics;
    if (! parseMidiFile (midiFile, imported, raw, importerDiagnostics, diagnostics))
        return false;

    if (! appendImportedMidi (doc, imported, raw, importBatch, diagnostics, importerDiagnostics, options))
        return false;

    // R3: first import's filename wins for SONG.inputMidiPath (and thus the
    // bridge-derived rawSong.title) — only set it when currently empty.
    if (doc.getTree().getProperty (SongIDs::inputMidiPath).toString().isEmpty())
        doc.getTree().setProperty (SongIDs::inputMidiPath, midiFile.getFullPathName(), nullptr);

    return true;
}

void synthesiseDefaultParts (SongDocument& doc)
{
    std::set<juce::int64> assignedTrackIds;
    auto partsNode = doc.getPartsNode();
    for (auto partTree : partsNode)
        for (int a = 0; a < SongDocument::getNumAssignments (partTree); ++a)
            assignedTrackIds.insert ((juce::int64) SongDocument::getAssignment (partTree, a).getProperty (SongIDs::trackId));

    // One undo transaction for the whole call, regardless of how many
    // parts/assignments end up being added below.
    doc.getUndoManager().beginNewTransaction();

    for (int i = 0; i < doc.getNumTracks(); ++i)
    {
        auto trackTree = doc.getTrack (i);
        if (! SongDocument::isAssignableTrack (trackTree))
            continue;
        const auto trackId = (juce::int64) trackTree.getProperty (SongIDs::trackId);
        if (assignedTrackIds.count (trackId) != 0)
            continue;

        const int channel = (int) trackTree.getProperty (SongIDs::sourceMidiChannel);
        const std::string instrumentName = (channel == 10)
            ? std::string (displayName (LotroInstrument::Drums))
            : std::string (displayName (LotroInstrument::LuteOfAges));

        auto part = doc.addPart (juce::String (instrumentName), juce::String(), false);
        doc.addAssignment (part, trackId, 0, 0, "octaveShift", false);
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

    std::set<juce::int64> assignedTrackIds;
    for (auto partTree : doc.getPartsNode())
        for (int a = 0; a < SongDocument::getNumAssignments (partTree); ++a)
            assignedTrackIds.insert ((juce::int64) SongDocument::getAssignment (partTree, a).getProperty (SongIDs::trackId));

    std::map<juce::int64, int> trackIdToIndex;
    for (int i = 0; i < sourceMidiNode.getNumChildren(); ++i)
    {
        auto trackTree = sourceMidiNode.getChild (i);
        const auto trackId = (juce::int64) trackTree.getProperty (SongIDs::trackId);

        // The conductor and unassigned note-less tracks never reach forge_core:
        // the CLI's importMidi drops note-less tracks, and an extra one here
        // would add an "unreferenced track" diagnostic the CLI never emits.
        if ((bool) trackTree.getProperty (SongIDs::isConductor, false))
            continue;
        if (SongDocument::getNotesNode (trackTree).getNumChildren() == 0
            && assignedTrackIds.count (trackId) == 0)
            continue;

        Track track;
        track.name              = trackTree.getProperty (SongIDs::name).toString().toStdString();
        track.sourceMidiChannel = (int) trackTree.getProperty (SongIDs::sourceMidiChannel);
        track.sourceProgram     = (int) trackTree.getProperty (SongIDs::sourceProgram);

        for (auto noteTree : SongDocument::getNotesNode (trackTree))
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

        trackIdToIndex[trackId] = (int) rawSong.tracks.size();
        rawSong.tracks.push_back (track);
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

            // ASSIGNMENT.rangePolicy is deliberately not translated here:
            // forge_core's ConfigSource has no such field — v1 offers only
            // octave-fold range handling, and R5 forbids adding one to
            // Config for this phase — so there is nothing on the Config
            // side for it to become. This is an intentional omission, not a
            // missed field.
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

void dropUnassignedInstruments (Config& config, Diagnostics& diagnostics)
{
    auto& instruments = config.instruments;
    instruments.erase (
        std::remove_if (instruments.begin(), instruments.end(),
            [&] (const ConfigInstrument& inst)
            {
                if (! inst.sources.empty())
                    return false;

                Diagnostic warn;
                warn.severity = Severity::Warning;
                warn.source   = "SongModelBridge";
                warn.message  = "Skipped part with no assigned tracks: "
                               + (inst.label.has_value() ? *inst.label : inst.name);
                diagnostics.push_back (warn);
                return true;
            }),
        instruments.end());
}

} // namespace lotro

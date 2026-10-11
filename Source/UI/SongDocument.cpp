#include "SongDocument.h"

#include "UI/TempoMapSync.h"

#include <algorithm>
#include <vector>

namespace lotro
{

namespace SongIDs
{
    const juce::Identifier SONG ("SONG");
    const juce::Identifier SOURCE_MIDI ("SOURCE_MIDI");
    const juce::Identifier MIDI_TRACK ("MIDI_TRACK");
    const juce::Identifier NOTE ("NOTE");
    const juce::Identifier PARTS ("PARTS");
    const juce::Identifier PART ("PART");
    const juce::Identifier ASSIGNMENT ("ASSIGNMENT");
    const juce::Identifier TEMPO_MAP ("TEMPO_MAP");
    const juce::Identifier TEMPO_CHANGE ("TEMPO_CHANGE");
    const juce::Identifier METER_MAP ("METER_MAP");
    const juce::Identifier METER_CHANGE ("METER_CHANGE");
    const juce::Identifier NOTES ("NOTES");
    const juce::Identifier EVENTS ("EVENTS");
    const juce::Identifier EVENT ("EVENT");
    const juce::Identifier SECTIONS ("SECTIONS");
    const juce::Identifier SECTION ("SECTION");

    const juce::Identifier title ("title");
    const juce::Identifier transcriber ("transcriber");
    const juce::Identifier tempoBpm ("tempoBpm");
    const juce::Identifier globalTranspose ("globalTranspose");
    const juce::Identifier inputMidiPath ("inputMidiPath");

    const juce::Identifier ticksPerQuarter ("ticksPerQuarter");
    const juce::Identifier timeBaseSet ("timeBaseSet");

    const juce::Identifier trackId ("trackId");
    const juce::Identifier name ("name");
    const juce::Identifier colorArgb ("colorArgb");
    const juce::Identifier sourceMidiChannel ("sourceMidiChannel");
    const juce::Identifier sourceProgram ("sourceProgram");
    const juce::Identifier importBatch ("importBatch");
    const juce::Identifier playbackVolume ("playbackVolume");

    const juce::Identifier pitch ("pitch");
    const juce::Identifier startTick ("startTick");
    const juce::Identifier durationTicks ("durationTicks");
    const juce::Identifier velocity ("velocity");
    const juce::Identifier isDrum ("isDrum");
    const juce::Identifier sourceTrackIndex ("sourceTrackIndex");
    const juce::Identifier sourceEventIndex ("sourceEventIndex");

    const juce::Identifier channel ("channel");
    const juce::Identifier offVelocity ("offVelocity");
    const juce::Identifier offIsNoteOnZero ("offIsNoteOnZero");
    const juce::Identifier onOrder ("onOrder");
    const juce::Identifier offOrder ("offOrder");
    const juce::Identifier sectionId ("sectionId");
    const juce::Identifier offSynthesized ("offSynthesized");
    const juce::Identifier isConductor ("isConductor");
    const juce::Identifier endTick ("endTick");
    const juce::Identifier defaultChannel ("defaultChannel");
    const juce::Identifier data ("data");
    const juce::Identifier order ("order");
    const juce::Identifier relocatedFrom ("relocatedFrom");

    const juce::Identifier partId ("partId");
    const juce::Identifier x ("x");
    const juce::Identifier instrumentName ("instrumentName");
    const juce::Identifier label ("label");
    const juce::Identifier drumMapPath ("drumMapPath");

    const juce::Identifier transposeSemitones ("transposeSemitones");
    const juce::Identifier volumePercent ("volumePercent");
    const juce::Identifier rangePolicy ("rangePolicy");

    const juce::Identifier tick ("tick");
    const juce::Identifier bpm ("bpm");
    const juce::Identifier numerator ("numerator");
    const juce::Identifier denominator ("denominator");

    // Internal bookkeeping — not part of the documented schema, never read
    // by SongModelBridge or later phases.
    static const juce::Identifier nextTrackId ("nextTrackId");
    static const juce::Identifier nextPartId ("nextPartId");
    static const juce::Identifier nextImportBatch ("nextImportBatch");
    static const juce::Identifier nextSectionId ("nextSectionId");
}

int trackPlaybackVolume (const juce::ValueTree& track)
{
    const auto v = track.getProperty (SongIDs::playbackVolume);
    return v.isInt() ? juce::jlimit (0, 100, (int) v) : 100;
}

SongDocument::SongDocument()
{
    tree = juce::ValueTree (SongIDs::SONG);
    // title/transcriber/tempoBpm are deliberately left unset here — see the
    // schema comment on the class above.
    tree.setProperty (SongIDs::globalTranspose, 0, nullptr);
    tree.setProperty (SongIDs::inputMidiPath, juce::String(), nullptr);
    tree.setProperty (SongIDs::nextTrackId, (juce::int64) 1, nullptr);
    tree.setProperty (SongIDs::nextPartId, (juce::int64) 1, nullptr);
    tree.setProperty (SongIDs::nextImportBatch, 1, nullptr);

    juce::ValueTree sourceMidi (SongIDs::SOURCE_MIDI);
    sourceMidi.setProperty (SongIDs::ticksPerQuarter, 480, nullptr);
    // Always present on a new document, so only files saved before it existed lack it.
    sourceMidi.setProperty (SongIDs::timeBaseSet, false, nullptr);
    tree.addChild (sourceMidi, -1, nullptr);

    // Every song has exactly one conductor track, created before any import
    // (2026-10-03 MIDI-fidelity spec). Not undoable: it is part of the empty
    // document, not an edit.
    juce::ValueTree conductor (SongIDs::MIDI_TRACK);
    conductor.setProperty (SongIDs::trackId, mintTrackId(), nullptr);
    conductor.setProperty (SongIDs::name, "Conductor", nullptr);
    conductor.setProperty (SongIDs::colorArgb, 0, nullptr);
    conductor.setProperty (SongIDs::sourceMidiChannel, 0, nullptr);
    conductor.setProperty (SongIDs::importBatch, 0, nullptr);
    conductor.setProperty (SongIDs::isConductor, true, nullptr);
    conductor.setProperty (SongIDs::endTick, 0, nullptr);
    conductor.addChild (juce::ValueTree (SongIDs::NOTES), -1, nullptr);
    conductor.addChild (juce::ValueTree (SongIDs::EVENTS), -1, nullptr);
    sourceMidi.addChild (conductor, -1, nullptr);

    juce::ValueTree parts (SongIDs::PARTS);
    tree.addChild (parts, -1, nullptr);

    juce::ValueTree tempoMap (SongIDs::TEMPO_MAP);
    tree.addChild (tempoMap, -1, nullptr);

    juce::ValueTree meterMap (SongIDs::METER_MAP);
    tree.addChild (meterMap, -1, nullptr);

    tempoMapSync = std::make_unique<TempoMapSync> (*this);
}

SongDocument::~SongDocument() = default;

bool SongDocument::hasTimeBase() const
{
    return (bool) getSourceMidiNode().getProperty (SongIDs::timeBaseSet, false);
}

void SongDocument::setTimeBase (bool set)
{
    getSourceMidiNode().setProperty (SongIDs::timeBaseSet, set, nullptr);
}

std::vector<MeterChange> SongDocument::meterChangesOf (const juce::ValueTree& meterMapNode)
{
    std::vector<MeterChange> out;
    for (int i = 0; i < meterMapNode.getNumChildren(); ++i)
    {
        const auto c = meterMapNode.getChild (i);
        out.push_back ({ (int) c.getProperty (SongIDs::tick, 0), (int) c.getProperty (SongIDs::numerator, 4),
                         (int) c.getProperty (SongIDs::denominator, 4) });
    }
    return out;
}

std::vector<MeterChange> SongDocument::getMeterChanges() const { return meterChangesOf (getMeterMapNode()); }

void SongDocument::undo() { undoManager.undo(); }
void SongDocument::redo() { undoManager.redo(); }
bool SongDocument::canUndo() const { return undoManager.canUndo(); }
bool SongDocument::canRedo() const { return undoManager.canRedo(); }

juce::ValueTree SongDocument::getSourceMidiNode() const
{
    return tree.getChildWithName (SongIDs::SOURCE_MIDI);
}

juce::ValueTree SongDocument::getPartsNode() const
{
    return tree.getChildWithName (SongIDs::PARTS);
}

juce::ValueTree SongDocument::getTempoMapNode() const
{
    return tree.getChildWithName (SongIDs::TEMPO_MAP);
}

juce::ValueTree SongDocument::getMeterMapNode() const
{
    return tree.getChildWithName (SongIDs::METER_MAP);
}

int SongDocument::getNumTracks() const
{
    return getSourceMidiNode().getNumChildren();
}

juce::ValueTree SongDocument::getTrack (int index) const
{
    return getSourceMidiNode().getChild (index);
}

juce::ValueTree SongDocument::findTrackById (juce::int64 trackIdToFind) const
{
    auto sourceMidi = getSourceMidiNode();
    for (int i = 0; i < sourceMidi.getNumChildren(); ++i)
    {
        auto child = sourceMidi.getChild (i);
        if ((juce::int64) child.getProperty (SongIDs::trackId) == trackIdToFind)
            return child;
    }
    return {};
}

int SongDocument::getNumParts() const
{
    return getPartsNode().getNumChildren();
}

juce::ValueTree SongDocument::getPart (int index) const
{
    return getPartsNode().getChild (index);
}

juce::ValueTree SongDocument::findPartById (juce::int64 partIdToFind) const
{
    auto parts = getPartsNode();
    for (int i = 0; i < parts.getNumChildren(); ++i)
    {
        auto child = parts.getChild (i);
        if ((juce::int64) child.getProperty (SongIDs::partId) == partIdToFind)
            return child;
    }
    return {};
}

juce::ValueTree SongDocument::getNotesNode (const juce::ValueTree& track)
{
    return track.getChildWithName (SongIDs::NOTES);
}

juce::ValueTree SongDocument::getEventsNode (const juce::ValueTree& track)
{
    return track.getChildWithName (SongIDs::EVENTS);
}

bool SongDocument::isAssignableTrack (const juce::ValueTree& track)
{
    return track.hasType (SongIDs::MIDI_TRACK)
        && ! (bool) track.getProperty (SongIDs::isConductor, false)
        && getNotesNode (track).getNumChildren() > 0;
}

int SongDocument::getNumAssignableTracks() const
{
    int count = 0;
    for (auto track : getSourceMidiNode())
        if (isAssignableTrack (track))
            ++count;
    return count;
}

juce::ValueTree SongDocument::getConductorTrack() const
{
    return getSourceMidiNode().getChild (0);
}

int SongDocument::getNumAssignments (const juce::ValueTree& part)
{
    return part.getNumChildren();
}

juce::ValueTree SongDocument::getAssignment (const juce::ValueTree& part, int index)
{
    return part.getChild (index);
}

juce::int64 SongDocument::mintTrackId()
{
    auto id = (juce::int64) tree.getProperty (SongIDs::nextTrackId, 1);
    tree.setProperty (SongIDs::nextTrackId, id + 1, nullptr);
    return id;
}

juce::int64 SongDocument::mintPartId()
{
    auto id = (juce::int64) tree.getProperty (SongIDs::nextPartId, 1);
    tree.setProperty (SongIDs::nextPartId, id + 1, nullptr);
    return id;
}

int SongDocument::mintImportBatch()
{
    const int batch = (int) tree.getProperty (SongIDs::nextImportBatch, 1);
    tree.setProperty (SongIDs::nextImportBatch, batch + 1, nullptr);
    return batch;
}

juce::int64 SongDocument::mintSectionId()
{
    auto next = (juce::int64) tree.getProperty (SongIDs::nextSectionId, 0);
    if (next < 1)
    {
        next = 1;
        for (auto track : getSourceMidiNode())
            for (auto section : track.getChildWithName (SongIDs::SECTIONS))
                next = std::max (next, (juce::int64) section.getProperty (SongIDs::sectionId, 0) + 1);
    }
    tree.setProperty (SongIDs::nextSectionId, next + 1, nullptr);
    return next;
}

std::optional<SongFileError> SongDocument::validateLoaded (const juce::ValueTree& t)
{
    const auto bad = [] (const std::string& why)
    {
        return std::optional<SongFileError> (SongFileError (SongFileErrorKind::InvalidStructure,
                                                            "This Song file is damaged: " + why));
    };

    if (! t.isValid() || ! t.hasType (SongIDs::SONG))
        return bad ("it does not contain a Song.");

    for (const auto* counter : { &SongIDs::nextTrackId, &SongIDs::nextPartId, &SongIDs::nextImportBatch })
        if (! t.hasProperty (*counter))
            return bad ("a bookkeeping counter is missing.");

    const juce::Identifier topLevel[] = { SongIDs::SOURCE_MIDI, SongIDs::PARTS, SongIDs::TEMPO_MAP, SongIDs::METER_MAP };
    if (t.getNumChildren() != 4)
        return bad ("it has unexpected sections.");
    for (const auto& id : topLevel)
    {
        int count = 0;
        for (int i = 0; i < t.getNumChildren(); ++i)
            if (t.getChild (i).hasType (id))
                ++count;
        if (count != 1)
            return bad ("a required section is missing or repeated.");
    }

    const auto sourceMidi = t.getChildWithName (SongIDs::SOURCE_MIDI);
    if (sourceMidi.getNumChildren() < 1)
        return bad ("it has no conductor track.");

    const auto nextTrack = (juce::int64) t.getProperty (SongIDs::nextTrackId);
    const auto nextPart  = (juce::int64) t.getProperty (SongIDs::nextPartId);
    const auto nextBatch = (juce::int64) t.getProperty (SongIDs::nextImportBatch);

    std::vector<juce::int64> trackIds;
    std::vector<juce::int64> sectionIds;
    for (int i = 0; i < sourceMidi.getNumChildren(); ++i)
    {
        const auto track = sourceMidi.getChild (i);
        if (! track.hasType (SongIDs::MIDI_TRACK))
            return bad ("a track entry is not a track.");
        if (((bool) track.getProperty (SongIDs::isConductor, false)) != (i == 0))
            return bad ("the conductor track is missing, repeated or not first.");
        if (! getNotesNode (track).isValid() || ! getEventsNode (track).isValid())
            return bad ("a track is missing its notes or events section.");

        const auto id = (juce::int64) track.getProperty (SongIDs::trackId, -1);
        if (id < 1 || id >= nextTrack)
            return bad ("a track id is out of range.");
        if (std::find (trackIds.begin(), trackIds.end(), id) != trackIds.end())
            return bad ("two tracks share an id.");
        trackIds.push_back (id);

        if ((juce::int64) track.getProperty (SongIDs::importBatch, 0) >= nextBatch)
            return bad ("an import batch number is out of range.");

        if (track.hasProperty (SongIDs::playbackVolume))
        {
            const auto volume = track.getProperty (SongIDs::playbackVolume);
            if (! volume.isInt() || (int) volume < 0 || (int) volume > 100)
                return bad ("a track volume is out of range.");
        }

        const auto sections = track.getChildWithName (SongIDs::SECTIONS);
        for (int s = 0; s < sections.getNumChildren(); ++s)
        {
            const auto section = sections.getChild (s);
            if (! section.hasType (SongIDs::SECTION))
                return bad ("a section entry is not a section.");

            const auto sid = (juce::int64) section.getProperty (SongIDs::sectionId, -1);
            if (sid < 1 || (t.hasProperty (SongIDs::nextSectionId)
                            && sid >= (juce::int64) t.getProperty (SongIDs::nextSectionId)))
                return bad ("a section id is out of range.");
            if (std::find (sectionIds.begin(), sectionIds.end(), sid) != sectionIds.end())
                return bad ("two sections share an id.");
            sectionIds.push_back (sid);

            const auto start = (int) section.getProperty (SongIDs::startTick, -1);
            const auto end   = (int) section.getProperty (SongIDs::endTick, -1);
            if (start < 0 || end <= start)
                return bad ("a section has an invalid tick range.");
        }
    }

    std::vector<juce::int64> partIds;
    const auto parts = t.getChildWithName (SongIDs::PARTS);
    for (int i = 0; i < parts.getNumChildren(); ++i)
    {
        const auto part = parts.getChild (i);
        if (! part.hasType (SongIDs::PART))
            return bad ("a part entry is not a part.");

        const auto id = (juce::int64) part.getProperty (SongIDs::partId, -1);
        if (id < 1 || id >= nextPart)
            return bad ("a part id is out of range.");
        if (std::find (partIds.begin(), partIds.end(), id) != partIds.end())
            return bad ("two parts share an id.");
        partIds.push_back (id);

        for (int a = 0; a < part.getNumChildren(); ++a)
        {
            const auto assignment = part.getChild (a);
            if (! assignment.hasType (SongIDs::ASSIGNMENT))
                continue;
            const auto ref = (juce::int64) assignment.getProperty (SongIDs::trackId, -1);
            if (std::find (trackIds.begin(), trackIds.end(), ref) == trackIds.end())
                return bad ("a part refers to a track that does not exist.");
        }
    }

    return std::nullopt;
}

juce::ValueTree SongDocument::addTrack (const juce::String& trackName, int colorArgb,
                                          int sourceMidiChannel, int importBatch)
{
    undoManager.beginNewTransaction();

    juce::ValueTree track (SongIDs::MIDI_TRACK);
    track.setProperty (SongIDs::trackId, mintTrackId(), &undoManager);
    track.setProperty (SongIDs::name, trackName, &undoManager);
    track.setProperty (SongIDs::colorArgb, colorArgb, &undoManager);
    track.setProperty (SongIDs::sourceMidiChannel, sourceMidiChannel, &undoManager);
    track.setProperty (SongIDs::importBatch, importBatch, &undoManager);
    track.setProperty (SongIDs::endTick, 0, nullptr);
    track.addChild (juce::ValueTree (SongIDs::NOTES), -1, nullptr);
    track.addChild (juce::ValueTree (SongIDs::EVENTS), -1, nullptr);

    getSourceMidiNode().addChild (track, -1, &undoManager);
    return track;
}

juce::ValueTree SongDocument::addTrackBulk (const juce::String& trackName, int colorArgb,
                                              int sourceMidiChannel, int importBatch)
{
    juce::ValueTree track (SongIDs::MIDI_TRACK);
    track.setProperty (SongIDs::trackId, mintTrackId(), nullptr);
    track.setProperty (SongIDs::name, trackName, nullptr);
    track.setProperty (SongIDs::colorArgb, colorArgb, nullptr);
    track.setProperty (SongIDs::sourceMidiChannel, sourceMidiChannel, nullptr);
    track.setProperty (SongIDs::importBatch, importBatch, nullptr);
    track.setProperty (SongIDs::endTick, 0, nullptr);
    track.addChild (juce::ValueTree (SongIDs::NOTES), -1, nullptr);
    track.addChild (juce::ValueTree (SongIDs::EVENTS), -1, nullptr);

    getSourceMidiNode().addChild (track, -1, nullptr);
    return track;
}

void SongDocument::removeTrack (juce::int64 trackIdToRemove)
{
    auto track = findTrackById (trackIdToRemove);
    if (! track.isValid())
        return;

    if ((bool) track.getProperty (SongIDs::isConductor, false))
        return; // the conductor can't be deleted

    undoManager.beginNewTransaction();

    // Cascade, in the same transaction as the track removal: an ASSIGNMENT
    // referencing a track that no longer exists is a dangling reference.
    // buildConfigAndRawSong silently skips these as defence in depth, but
    // the document itself should not go on holding them — and folding the
    // removal into this one transaction means a single undo() restores both
    // the track and every assignment that pointed to it.
    auto partsNode = getPartsNode();
    for (int p = 0; p < partsNode.getNumChildren(); ++p)
    {
        auto part = partsNode.getChild (p);
        for (int a = part.getNumChildren(); --a >= 0; )
        {
            auto assignment = part.getChild (a);
            if ((juce::int64) assignment.getProperty (SongIDs::trackId) == trackIdToRemove)
                part.removeChild (assignment, &undoManager);
        }
    }

    getSourceMidiNode().removeChild (track, &undoManager);
}

juce::ValueTree SongDocument::addPart (const juce::String& instrumentName, const juce::String& label,
                                        bool newTransaction)
{
    if (newTransaction)
        undoManager.beginNewTransaction();

    // x is minted as (max existing PART.x) + 1, not getNumParts() + 1: the
    // latter collides after an add/remove/add sequence (add A/B/C -> x
    // 1/2/3, remove B, add D -> getNumParts()+1 == 3, colliding with C's
    // still-live x). See the class-level comment above for why that
    // collision matters (validateConfig rejects it).
    int maxX = 0;
    auto partsNode = getPartsNode();
    for (int i = 0; i < partsNode.getNumChildren(); ++i)
        maxX = std::max (maxX, (int) partsNode.getChild (i).getProperty (SongIDs::x));

    juce::ValueTree part (SongIDs::PART);
    part.setProperty (SongIDs::partId, mintPartId(), &undoManager);
    part.setProperty (SongIDs::x, maxX + 1, &undoManager);
    part.setProperty (SongIDs::instrumentName, instrumentName, &undoManager);
    part.setProperty (SongIDs::label, label, &undoManager);
    part.setProperty (SongIDs::drumMapPath, juce::String(), &undoManager);

    getPartsNode().addChild (part, -1, &undoManager);
    return part;
}

void SongDocument::removePart (juce::int64 partIdToRemove)
{
    auto part = findPartById (partIdToRemove);
    if (! part.isValid())
        return;

    undoManager.beginNewTransaction();
    getPartsNode().removeChild (part, &undoManager);
}

juce::ValueTree SongDocument::addAssignment (juce::ValueTree part, juce::int64 refTrackId,
                                              int transposeSemitones, int volumePercent,
                                              const juce::String& rangePolicy,
                                              bool newTransaction)
{
    jassert (part.hasType (SongIDs::PART));

    if (newTransaction)
        undoManager.beginNewTransaction();

    juce::ValueTree assignment (SongIDs::ASSIGNMENT);
    assignment.setProperty (SongIDs::trackId, refTrackId, &undoManager);
    assignment.setProperty (SongIDs::transposeSemitones, transposeSemitones, &undoManager);
    assignment.setProperty (SongIDs::volumePercent, volumePercent, &undoManager);
    assignment.setProperty (SongIDs::rangePolicy, rangePolicy, &undoManager);

    part.addChild (assignment, -1, &undoManager);
    return assignment;
}

void SongDocument::removeAssignment (juce::ValueTree part, juce::ValueTree assignment)
{
    undoManager.beginNewTransaction();
    part.removeChild (assignment, &undoManager);
}

juce::ValueTree SongDocument::findAssignment (const juce::ValueTree& part, juce::int64 trackIdToFind)
{
    for (int i = 0; i < part.getNumChildren(); ++i)
    {
        auto child = part.getChild (i);
        if ((juce::int64) child.getProperty (SongIDs::trackId) == trackIdToFind)
            return child;
    }
    return {};
}

bool SongDocument::assignTrackToPart (juce::int64 partId, juce::int64 trackId)
{
    auto part = findPartById (partId);
    if (! part.isValid())
        return false;

    if (! isAssignableTrack (findTrackById (trackId)))
        return false;

    if (findAssignment (part, trackId).isValid())
        return false;

    addAssignment (part, trackId, 0, 0, "octaveShift");
    return true;
}

void SongDocument::removeProperty (juce::ValueTree targetTree, const juce::Identifier& propertyId,
                                    bool newTransaction)
{
    if (newTransaction)
        undoManager.beginNewTransaction();

    targetTree.removeProperty (propertyId, &undoManager);
}

void SongDocument::setProperty (juce::ValueTree targetTree, const juce::Identifier& propertyId,
                                 const juce::var& newValue, bool newTransaction)
{
    if (newTransaction)
        undoManager.beginNewTransaction();

    targetTree.setProperty (propertyId, newValue, &undoManager);
}

void SongDocument::addChild (juce::ValueTree parent, juce::ValueTree child, bool newTransaction)
{
    if (newTransaction)
        undoManager.beginNewTransaction();

    parent.addChild (child, -1, &undoManager);
}

void SongDocument::removeChild (juce::ValueTree parent, juce::ValueTree child, bool newTransaction)
{
    if (newTransaction)
        undoManager.beginNewTransaction();

    parent.removeChild (child, &undoManager);
}

void SongDocument::appendChildBulk (juce::ValueTree parent, juce::ValueTree child)
{
    parent.addChild (child, -1, nullptr);
}

void SongDocument::replaceContents (const juce::ValueTree& loaded)
{
    if (auto error = validateLoaded (loaded))
        throw *error;

    // Deep copy first: `loaded` may be another document's live tree.
    const auto source = loaded.createCopy();
    {
        MapSyncPause pause (*this);
        tree.copyPropertiesFrom (source, nullptr);
        for (const auto& id : { SongIDs::SOURCE_MIDI, SongIDs::PARTS, SongIDs::TEMPO_MAP, SongIDs::METER_MAP })
            tree.getChildWithName (id).copyPropertiesAndChildrenFrom (source.getChildWithName (id), nullptr);

        // Files from before timeBaseSet existed: a stored tempo map means an import happened.
        // Those files may also hold tempo/meter events in note tracks (their stored map
        // included them); move them to the conductor so the rebuilt maps and export agree.
        auto sourceMidi = getSourceMidiNode();
        if (! sourceMidi.hasProperty (SongIDs::timeBaseSet))
        {
            sourceMidi.setProperty (SongIDs::timeBaseSet, getTempoMapNode().getNumChildren() > 0, nullptr);
            moveTempoAndMeterToConductor (*this);
        }
    }   // the maps are rebuilt from the events here, healing any stale ones

    undoManager.clearUndoHistory();
}

void SongDocument::resetToEmpty()
{
    SongDocument fresh;
    replaceContents (fresh.getTree());
}

} // namespace lotro

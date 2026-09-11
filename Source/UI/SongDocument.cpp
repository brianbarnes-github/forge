#include "SongDocument.h"

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

    const juce::Identifier title ("title");
    const juce::Identifier transcriber ("transcriber");
    const juce::Identifier tempoBpm ("tempoBpm");
    const juce::Identifier globalTranspose ("globalTranspose");
    const juce::Identifier inputMidiPath ("inputMidiPath");

    const juce::Identifier ticksPerQuarter ("ticksPerQuarter");

    const juce::Identifier trackId ("trackId");
    const juce::Identifier name ("name");
    const juce::Identifier colorArgb ("colorArgb");
    const juce::Identifier sourceMidiChannel ("sourceMidiChannel");
    const juce::Identifier importBatch ("importBatch");

    const juce::Identifier pitch ("pitch");
    const juce::Identifier startTick ("startTick");
    const juce::Identifier durationTicks ("durationTicks");
    const juce::Identifier velocity ("velocity");
    const juce::Identifier isDrum ("isDrum");
    const juce::Identifier sourceTrackIndex ("sourceTrackIndex");
    const juce::Identifier sourceEventIndex ("sourceEventIndex");

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

    juce::ValueTree sourceMidi (SongIDs::SOURCE_MIDI);
    sourceMidi.setProperty (SongIDs::ticksPerQuarter, 480, nullptr);
    tree.addChild (sourceMidi, -1, nullptr);

    juce::ValueTree parts (SongIDs::PARTS);
    tree.addChild (parts, -1, nullptr);

    juce::ValueTree tempoMap (SongIDs::TEMPO_MAP);
    tree.addChild (tempoMap, -1, nullptr);

    juce::ValueTree meterMap (SongIDs::METER_MAP);
    tree.addChild (meterMap, -1, nullptr);
}

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

    getSourceMidiNode().addChild (track, -1, nullptr);
    return track;
}

void SongDocument::removeTrack (juce::int64 trackIdToRemove)
{
    auto track = findTrackById (trackIdToRemove);
    if (! track.isValid())
        return;

    undoManager.beginNewTransaction();
    getSourceMidiNode().removeChild (track, &undoManager);
}

juce::ValueTree SongDocument::addPart (const juce::String& instrumentName, const juce::String& label,
                                        bool newTransaction)
{
    if (newTransaction)
        undoManager.beginNewTransaction();

    juce::ValueTree part (SongIDs::PART);
    part.setProperty (SongIDs::partId, mintPartId(), &undoManager);
    part.setProperty (SongIDs::x, getNumParts() + 1, &undoManager);
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

    if (! findTrackById (trackId).isValid())
        return false;

    if (findAssignment (part, trackId).isValid())
        return false;

    addAssignment (part, trackId, 0, 0, "octaveShift");
    return true;
}

void SongDocument::setProperty (juce::ValueTree targetTree, const juce::Identifier& propertyId,
                                 const juce::var& newValue, bool newTransaction)
{
    if (newTransaction)
        undoManager.beginNewTransaction();

    targetTree.setProperty (propertyId, newValue, &undoManager);
}

void SongDocument::appendChildBulk (juce::ValueTree parent, juce::ValueTree child)
{
    parent.addChild (child, -1, nullptr);
}

} // namespace lotro

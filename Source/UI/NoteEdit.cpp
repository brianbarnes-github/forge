#include "UI/NoteEdit.h"

#include "UI/SectionEdit.h"

#include <algorithm>
#include <map>
#include <optional>

namespace lotro
{

namespace
{
    int startOf (const juce::ValueTree& n) { return (int) n.getProperty (SongIDs::startTick); }
    int endOf (const juce::ValueTree& n) { return startOf (n) + (int) n.getProperty (SongIDs::durationTicks); }
    int pitchOf (const juce::ValueTree& n) { return (int) n.getProperty (SongIDs::pitch); }

    bool splits (const juce::ValueTree& n, int tick) { return startOf (n) < tick && tick < endOf (n); }

    // Each pitch shared by two or more of `notes`: the note that survives a join
    // (earliest start, first given on a tie), the end it gets, and the rest.
    struct JoinGroup
    {
        juce::ValueTree survivor;
        int end = 0;
        std::vector<juce::ValueTree> absorbed;
    };

    std::vector<JoinGroup> joinGroups (const std::vector<juce::ValueTree>& notes)
    {
        std::map<int, std::vector<juce::ValueTree>> byPitch;
        for (const auto& n : notes)
            byPitch[pitchOf (n)].push_back (n);

        std::vector<JoinGroup> groups;
        for (auto& [pitch, members] : byPitch)
        {
            if (members.size() < 2)
                continue;
            std::stable_sort (members.begin(), members.end(),
                              [] (const juce::ValueTree& a, const juce::ValueTree& b) { return startOf (a) < startOf (b); });
            JoinGroup g;
            g.survivor = members.front();
            g.absorbed.assign (members.begin() + 1, members.end());
            for (const auto& m : members)
                g.end = std::max (g.end, endOf (m));
            groups.push_back (std::move (g));
        }
        return groups;
    }

    // The first of `sortedStarts` strictly after `tick`, if any.
    std::optional<int> nextStartAfter (const std::vector<int>& sortedStarts, int tick)
    {
        const auto it = std::upper_bound (sortedStarts.begin(), sortedStarts.end(), tick);
        return it == sortedStarts.end() ? std::nullopt : std::optional<int> (*it);
    }

    // Each of `notes` whose length legato changes, with that length.
    std::vector<std::pair<juce::ValueTree, int>> legatoChanges (const juce::ValueTree& track,
                                                                const std::vector<juce::ValueTree>& notes,
                                                                LegatoTarget target)
    {
        // Every start on the track, sorted, per pitch (samePitch) or all under one key.
        const auto keyOf = [target] (const juce::ValueTree& n) { return target == LegatoTarget::samePitch ? pitchOf (n) : 0; };
        std::map<int, std::vector<int>> starts;
        const auto all = SongDocument::getNotesNode (track);
        for (int i = 0; i < all.getNumChildren(); ++i)
        {
            const auto n = all.getChild (i);
            starts[keyOf (n)].push_back (startOf (n));
        }
        for (auto& [key, list] : starts)
            std::sort (list.begin(), list.end());

        std::vector<std::pair<juce::ValueTree, int>> changes;
        for (const auto& n : notes)
            if (const auto next = nextStartAfter (starts[keyOf (n)], startOf (n)))
                if (const int length = *next - startOf (n); length != (int) n.getProperty (SongIDs::durationTicks))
                    changes.emplace_back (n, length);
        return changes;
    }
}

bool canSplitNotes (const std::vector<juce::ValueTree>& notes, int tick)
{
    return std::any_of (notes.begin(), notes.end(), [tick] (const juce::ValueTree& n) { return splits (n, tick); });
}

std::vector<juce::ValueTree> splitNotes (SongDocument& doc, juce::ValueTree track,
                                         const std::vector<juce::ValueTree>& notes, int tick)
{
    if (! track.isValid() || ! canSplitNotes (notes, tick))
        return {};

    auto notesNode = SongDocument::getNotesNode (track);
    auto selection = notes;
    doc.getUndoManager().beginNewTransaction();
    for (auto note : notes)
    {
        if (! splits (note, tick))
            continue;
        // As a section split cuts a note: the tail is a copy with no recorded order.
        auto tail = note.createCopy();
        tail.setProperty (SongIDs::startTick, tick, nullptr);
        tail.setProperty (SongIDs::durationTicks, endOf (note) - tick, nullptr);
        for (const auto& p : { SongIDs::onOrder, SongIDs::offOrder })
            tail.removeProperty (p, nullptr);
        tail.setProperty (SongIDs::offSynthesized, false, nullptr);
        doc.addChild (notesNode, tail, false);
        selection.push_back (tail);

        doc.setProperty (note, SongIDs::durationTicks, tick - startOf (note), false);
        markNoteTimingEdited (doc, note);
    }
    return selection;
}

bool canJoinNotes (const std::vector<juce::ValueTree>& notes)
{
    return ! joinGroups (notes).empty();
}

std::vector<juce::ValueTree> joinNotes (SongDocument& doc, juce::ValueTree track,
                                        const std::vector<juce::ValueTree>& notes)
{
    const auto groups = joinGroups (notes);
    if (! track.isValid() || groups.empty())
        return notes;

    auto notesNode = SongDocument::getNotesNode (track);
    std::vector<juce::ValueTree> removed;
    doc.getUndoManager().beginNewTransaction();
    for (const auto& g : groups)
    {
        auto survivor = g.survivor;
        if (g.end != endOf (survivor))
        {
            doc.setProperty (survivor, SongIDs::durationTicks, g.end - startOf (survivor), false);
            markNoteTimingEdited (doc, survivor);
        }
        for (const auto& n : g.absorbed)
        {
            doc.removeChild (notesNode, n, false);
            removed.push_back (n);
        }
    }

    std::vector<juce::ValueTree> remaining;
    for (const auto& n : notes)
        if (std::find (removed.begin(), removed.end(), n) == removed.end())
            remaining.push_back (n);
    return remaining;
}

bool canLegatoNotes (const juce::ValueTree& track, const std::vector<juce::ValueTree>& notes, LegatoTarget target)
{
    return track.isValid() && ! legatoChanges (track, notes, target).empty();
}

bool legatoNotes (SongDocument& doc, juce::ValueTree track, const std::vector<juce::ValueTree>& notes,
                  LegatoTarget target)
{
    if (! track.isValid())
        return false;
    const auto changes = legatoChanges (track, notes, target);
    if (changes.empty())
        return false;

    doc.getUndoManager().beginNewTransaction();
    for (auto [note, length] : changes)
    {
        doc.setProperty (note, SongIDs::durationTicks, length, false);
        markNoteTimingEdited (doc, note);
    }
    return true;
}

} // namespace lotro

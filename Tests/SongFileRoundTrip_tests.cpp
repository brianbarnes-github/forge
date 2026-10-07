// SongFileRoundTrip: import -> arrange -> edit -> save -> load reproduces the
// Song exactly, and Songsmith's other outputs (MIDI export) are unchanged by it.

#include "UI/MidiExport.h"
#include "UI/RawMidi.h"
#include "UI/SongDocument.h"
#include "UI/SongFile.h"
#include "UI/SongModelBridge.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <juce_core/juce_core.h>

#include <set>
#include <string>

using namespace lotro;

namespace
{
    juce::File midiFixture (const std::string& name)
    {
        return juce::File (__FILE__).getParentDirectory().getParentDirectory()
                   .getChildFile ("midi").getChildFile (name);
    }

    // Arranges, deletes a note and shifts another, and retitles, so the Song is not just a pristine import.
    void editSong (SongDocument& doc)
    {
        synthesiseDefaultParts (doc);
        for (int i = 0; i < doc.getNumTracks(); ++i)
        {
            auto notes = SongDocument::getNotesNode (doc.getTrack (i));
            if (notes.getNumChildren() >= 2)
            {
                doc.removeChild (notes, notes.getChild (0));
                doc.setProperty (notes.getChild (0), SongIDs::startTick,
                                 (int) notes.getChild (0).getProperty (SongIDs::startTick) + 10);
                break;
            }
        }
        doc.setProperty (doc.getTree(), SongIDs::title, "Round Trip");
    }
}

TEST_CASE ("SongFileRoundTrip: every tracked fixture survives save and load, edits included", "[songfile-roundtrip]")
{
    auto name = GENERATE (as<std::string>{},
        "Barnes Brothers Band - Pull The Wires.mid", "anymore.mid", "blue.mid",
        "hold.mid", "land.mid", "leah.mid", "nobody.mid", "right.mid", "tellit.mid");

    DYNAMIC_SECTION (name)
    {
        SongDocument original;
        Diagnostics diags;
        REQUIRE (importMidiFile (original, midiFixture (name), original.mintImportBatch(), diags));
        editSong (original);

        const auto file = juce::File::createTempFile (".songsmith");
        REQUIRE (saveSongFile (original, file));

        SongDocument loaded;
        loaded.replaceContents (loadSongFile (file));
        file.deleteFile();

        CHECK (loaded.getTree().isEquivalentTo (original.getTree()));
        CHECK (writeMidiBytes (buildRawMidiFile (loaded)) == writeMidiBytes (buildRawMidiFile (original)));   // Export > MIDI is unchanged by the round trip
    }
}

TEST_CASE ("SongFileRoundTrip: importing into a loaded Song uses fresh track ids and batch numbers", "[songfile-roundtrip]")
{
    SongDocument first;
    Diagnostics diags;
    REQUIRE (importMidiFile (first, midiFixture ("land.mid"), first.mintImportBatch(), diags));

    SongDocument loaded;
    loaded.replaceContents (readSongBytes (writeSongBytes (first.getTree())));

    const int tracksBefore = loaded.getNumTracks();
    REQUIRE (importMidiFile (loaded, midiFixture ("blue.mid"), loaded.mintImportBatch(), diags));
    REQUIRE (loaded.getNumTracks() > tracksBefore);

    std::set<juce::int64> ids;
    for (int i = 0; i < loaded.getNumTracks(); ++i)
        CHECK (ids.insert ((juce::int64) loaded.getTrack (i).getProperty (SongIDs::trackId)).second);   // no duplicate ids

    CHECK ((int) loaded.getTrack (loaded.getNumTracks() - 1).getProperty (SongIDs::importBatch) == 2);   // not a reused 1
}

TEST_CASE ("SongFileRoundTrip: undo history is not saved but edits after a load undo normally", "[songfile-roundtrip]")
{
    SongDocument doc;
    doc.addPart ("Lute of Ages", "a");

    SongDocument loaded;
    loaded.replaceContents (readSongBytes (writeSongBytes (doc.getTree())));
    CHECK_FALSE (loaded.canUndo());

    loaded.addPart ("Harp", "b");
    CHECK (loaded.canUndo());
    loaded.undo();
    CHECK (loaded.getNumParts() == 1);
}

TEST_CASE ("song file: sections and note tags round-trip, and the counter survives", "[song-file][sections]")
{
    SongDocument doc;
    auto track = doc.addTrackBulk ("T", 0xFF336699, 1, doc.mintImportBatch());
    juce::ValueTree sections (SongIDs::SECTIONS);
    juce::ValueTree section (SongIDs::SECTION);
    const auto id = doc.mintSectionId();
    section.setProperty (SongIDs::sectionId, id, nullptr);
    section.setProperty (SongIDs::startTick, 0, nullptr);
    section.setProperty (SongIDs::endTick, 960, nullptr);
    sections.addChild (section, -1, nullptr);
    track.addChild (sections, -1, nullptr);
    juce::ValueTree note (SongIDs::NOTE);
    note.setProperty (SongIDs::pitch, 60, nullptr);
    note.setProperty (SongIDs::startTick, 0, nullptr);
    note.setProperty (SongIDs::durationTicks, 480, nullptr);
    note.setProperty (SongIDs::sectionId, id, nullptr);
    SongDocument::appendChildBulk (SongDocument::getNotesNode (track), note);

    SongDocument reloaded;
    reloaded.replaceContents (readSongBytes (writeSongBytes (doc.getTree())));

    const auto loadedTrack = reloaded.getTrack (1);
    const auto loadedSection = loadedTrack.getChildWithName (SongIDs::SECTIONS).getChild (0);
    CHECK ((juce::int64) loadedSection.getProperty (SongIDs::sectionId) == id);
    CHECK ((int) loadedSection.getProperty (SongIDs::endTick) == 960);
    CHECK ((juce::int64) SongDocument::getNotesNode (loadedTrack).getChild (0).getProperty (SongIDs::sectionId) == id);
    CHECK (reloaded.mintSectionId() == id + 1);
}

TEST_CASE ("song file: a song saved before sections existed still loads", "[song-file][sections]")
{
    SongDocument doc;   // no SECTIONS anywhere, no nextSectionId property
    doc.getTree().removeProperty (juce::Identifier ("nextSectionId"), nullptr);

    SongDocument reloaded;
    REQUIRE_NOTHROW (reloaded.replaceContents (readSongBytes (writeSongBytes (doc.getTree()))));
}

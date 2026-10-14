// TrackChipBar: a thin row of draggable track chips (the Tracks dock panel).

#include "PlaybackTestSupport.h"
#include "UI/PartSlotComponent.h"
#include "UI/SongDocument.h"
#include "UI/TrackChipBar.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;
using lotro::playbacktest::addNote;

namespace
{
    juce::ValueTree noteTrack (SongDocument& doc, const char* name, int colour = (int) 0xFF336699)
    {
        auto t = doc.addTrack (name, colour, 1, 1);
        addNote (t, 60, 0, 480);
        return t;
    }

    juce::int64 idOf (const juce::ValueTree& t) { return (juce::int64) t.getProperty (SongIDs::trackId); }

    juce::MouseEvent pressOn (juce::Component& target, juce::ModifierKeys mods = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier))
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                 juce::Point<float> (4.0f, 4.0f), mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target,
                                 juce::Time::getCurrentTime(), juce::Point<float> (4.0f, 4.0f),
                                 juce::Time::getCurrentTime(), 1, false);
    }
}

TEST_CASE ("TrackChipBar: one chip per assignable track, in track order, with the track's number, name and colour", "[chip-bar]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto a = noteTrack (doc, "Lute", (int) 0xFFAA0000);
    doc.addTrack ("Empty", (int) 0xFF00AA00, 1, 1);   // no notes: cannot be dropped on a part
    auto c = noteTrack (doc, "Harp", (int) 0xFF0000AA);

    TrackChipBar bar (doc);
    bar.setBounds (0, 0, 600, TrackChipBar::barHeight);
    bar.refreshForTesting();

    REQUIRE (bar.chipCountForTesting() == 2);
    CHECK (bar.chipInfoForTesting (0).trackId == idOf (a));
    CHECK (bar.chipInfoForTesting (1).trackId == idOf (c));
    CHECK (bar.chipInfoForTesting (0).label == "1  Lute");
    CHECK (bar.chipInfoForTesting (1).label == "3  Harp");   // numbered like its track head
    CHECK (bar.chipInfoForTesting (0).colour == juce::Colour (0xFFAA0000));
}

TEST_CASE ("TrackChipBar: the conductor never gets a chip", "[chip-bar]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    TrackChipBar bar (doc);
    bar.refreshForTesting();
    CHECK (bar.chipCountForTesting() == 0);
}

TEST_CASE ("TrackChipBar: chips follow tracks being added or removed and renamed or recoloured", "[chip-bar]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto a = noteTrack (doc, "Lute");
    TrackChipBar bar (doc);
    bar.refreshForTesting();
    REQUIRE (bar.chipCountForTesting() == 1);

    noteTrack (doc, "Harp");
    bar.refreshForTesting();
    CHECK (bar.chipCountForTesting() == 2);

    a.setProperty (SongIDs::name, "Theorbo", nullptr);
    a.setProperty (SongIDs::colorArgb, (int) 0xFF112233, nullptr);
    bar.refreshForTesting();
    CHECK (bar.chipInfoForTesting (0).label == "1  Theorbo");
    CHECK (bar.chipInfoForTesting (0).colour == juce::Colour (0xFF112233));

    doc.removeTrack (idOf (a));
    bar.refreshForTesting();
    REQUIRE (bar.chipCountForTesting() == 1);
    CHECK (bar.chipInfoForTesting (0).label == "1  Harp");   // renumbered like its track head
}

TEST_CASE ("TrackChipBar: an unrelated change (a note edit) does not rebuild the chips", "[chip-bar]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto a = noteTrack (doc, "Lute");
    TrackChipBar bar (doc);
    bar.refreshForTesting();
    const auto* before = bar.chipComponentForTesting (0);

    addNote (a, 64, 480, 480);
    bar.refreshForTesting();
    CHECK (bar.chipComponentForTesting (0) == before);   // same object: nothing to redraw
}

TEST_CASE ("TrackChipBar: a track that gains its first note gets a chip", "[chip-bar]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto empty = doc.addTrack ("Later", (int) 0xFF336699, 1, 1);
    TrackChipBar bar (doc);
    bar.refreshForTesting();
    REQUIRE (bar.chipCountForTesting() == 0);

    addNote (empty, 60, 0, 480);
    bar.refreshForTesting();
    CHECK (bar.chipCountForTesting() == 1);
}

TEST_CASE ("TrackChipBar: a chip drags the track id, which a part slot accepts and assigns", "[chip-bar]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto t = noteTrack (doc, "Lute");
    auto part = doc.addPart ("Lute of Ages", "Part 1");
    TrackChipBar bar (doc);
    bar.refreshForTesting();
    REQUIRE (bar.chipCountForTesting() == 1);

    const juce::var payload = bar.chipComponentForTesting (0)->dragDescription();
    CHECK (payload.isInt64());
    CHECK ((juce::int64) payload == idOf (t));

    PartSlotComponent slot (doc, part);
    const juce::DragAndDropTarget::SourceDetails details (payload, bar.chipComponentForTesting (0), {});
    CHECK (slot.isInterestedInDragSource (details));
    slot.itemDropped (details);
    CHECK (SongDocument::getNumAssignments (doc.getPart (0)) == 1);
}

TEST_CASE ("TrackChipBar: clicking a chip reports the track and the modifiers", "[chip-bar]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto t = noteTrack (doc, "Lute");
    TrackChipBar bar (doc);
    bar.refreshForTesting();

    juce::int64 gotId = -1;
    bool gotCtrl = false;
    int calls = 0;
    bar.onTrackClicked = [&] (juce::int64 id, const juce::ModifierKeys& m) { gotId = id; gotCtrl = m.isCtrlDown(); ++calls; };

    auto* chip = bar.chipComponentForTesting (0);
    chip->mouseDown (pressOn (*chip));
    CHECK (calls == 1);
    CHECK (gotId == idOf (t));
    CHECK_FALSE (gotCtrl);

    chip->mouseDown (pressOn (*chip, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::ctrlModifier)));
    CHECK (calls == 2);
    CHECK (gotCtrl);
}

TEST_CASE ("TrackChipBar: chips keep one width and overflow scrolls instead of shrinking", "[chip-bar]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    for (int i = 0; i < 12; ++i)
        noteTrack (doc, "A reasonably long track name");
    TrackChipBar bar (doc);
    bar.setBounds (0, 0, 400, TrackChipBar::barHeight);
    bar.refreshForTesting();

    REQUIRE (bar.chipCountForTesting() == 12);
    for (int i = 0; i < 12; ++i)
        CHECK (bar.chipComponentForTesting (i)->getWidth() == TrackChipBar::chipWidth);
    CHECK (bar.contentWidthForTesting() > 400);
    CHECK (bar.chipComponentForTesting (1)->getX() > bar.chipComponentForTesting (0)->getRight() - 1);
}

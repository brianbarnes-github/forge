#include "PlaybackTestSupport.h"
#include "UI/Mixer/ActivePlaybackTarget.h"
#include "UI/Mixer/SourceMidiTarget.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>

using namespace lotro;
using namespace lotro::playbacktest;

namespace
{
    struct Rig
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        SongDocument doc;
        RecordingSink sink;
        PlaybackController controller { doc, sink };
        SourceMidiTarget target { doc, controller };
    };

    struct CountingListener : MixerTarget::Listener
    {
        int count = 0;
        void mixerChanged() override { ++count; }
    };

    juce::int64 idOf (const juce::ValueTree& t) { return static_cast<juce::int64> (t.getProperty (SongIDs::trackId)); }
}

TEST_CASE ("SourceMidiTarget: channels skip the conductor and number the rest", "[mixer][target]")
{
    Rig r;
    auto a = addTrack (r.doc, "Lead");
    auto b = addTrack (r.doc, "Bass");
    const auto ch = r.target.channels();
    REQUIRE (ch.size() == 2);
    CHECK (ch[0].number == 1);
    CHECK (ch[1].number == 2);
    CHECK (ch[0].id == idOf (a));
    CHECK (ch[1].id == idOf (b));
    CHECK (ch[0].name == "Lead");
    CHECK (ch[1].name == "Bass");
    CHECK (ch[0].argb == 0xff336699u);
    CHECK (ch[0].volume == 100);
    CHECK (! ch[0].pan.has_value());
}

TEST_CASE ("SourceMidiTarget: setVolume writes playbackVolume, 100 removes it, one undo step per gesture", "[mixer][target]")
{
    Rig r;
    auto t = addTrack (r.doc);
    const auto id = idOf (t);

    r.target.setVolume (id, 50, true);
    r.doc.getUndoManager().beginNewTransaction();
    r.target.setVolume (id, 40, true);
    r.target.setVolume (id, 30, false);
    r.target.setVolume (id, 20, false);
    CHECK (trackPlaybackVolume (t) == 20);
    r.doc.getUndoManager().undo();
    CHECK (trackPlaybackVolume (t) == 50);

    r.target.setVolume (id, 100, true);
    CHECK (! t.hasProperty (SongIDs::playbackVolume));
}

TEST_CASE ("SourceMidiTarget: setPan writes playbackPan, nullopt removes it", "[mixer][target][pan]")
{
    Rig r;
    auto t = addTrack (r.doc);
    const auto id = idOf (t);

    r.target.setPan (id, -40, true);
    CHECK (trackPlaybackPan (t) == std::optional<int> (-40));
    CHECK (r.target.channels()[0].pan == std::optional<int> (-40));

    r.doc.getUndoManager().beginNewTransaction();
    r.target.setPan (id, 10, true);
    r.target.setPan (id, 20, false);
    r.doc.getUndoManager().undo();
    CHECK (trackPlaybackPan (t) == std::optional<int> (-40));

    r.target.setPan (id, std::nullopt, true);
    CHECK (! t.hasProperty (SongIDs::playbackPan));
    CHECK (! r.target.channels()[0].pan.has_value());
}

TEST_CASE ("SourceMidiTarget: mute, solo and master go through the controller", "[mixer][target]")
{
    Rig r;
    auto a = addTrack (r.doc, "A");
    auto b = addTrack (r.doc, "B");
    addTrack (r.doc, "C");
    r.target.setMuted (idOf (a), true);
    r.target.setSoloed (idOf (b), true);
    auto ch = r.target.channels();
    CHECK (ch[0].muted);
    CHECK (! ch[0].soloed);
    CHECK (ch[1].soloed);
    CHECK (! ch[1].silencedBySolo);
    CHECK (ch[2].silencedBySolo);
    CHECK (r.controller.isMuted (idOf (a)));

    r.target.setMasterPercent (65);
    CHECK (r.target.masterPercent() == 65);
    CHECK (r.controller.masterPercent() == 65);
}

TEST_CASE ("SourceMidiTarget: a change made anywhere notifies once after coalescing", "[mixer][target]")
{
    Rig r;
    auto t = addTrack (r.doc);
    r.target.flushPendingForTesting();
    CountingListener l;
    r.target.addListener (&l);

    t.setProperty (SongIDs::name, "Renamed", nullptr);
    r.target.setVolume (idOf (t), 30, true);
    addTrack (r.doc, "Another");
    CHECK (l.count == 0);
    r.target.flushPendingForTesting();
    CHECK (l.count == 1);
    r.target.removeListener (&l);
}

TEST_CASE ("SourceMidiTarget: survives replaceContents with different tracks", "[mixer][target]")
{
    Rig r;
    addTrack (r.doc, "Old");

    SongDocument other;
    for (const char* n : { "X", "Y", "Z" })
        other.addTrack (n, static_cast<int> (0xff336699u), 1, other.mintImportBatch());
    r.doc.replaceContents (other.getTree());
    r.target.flushPendingForTesting();

    const auto ch = r.target.channels();
    REQUIRE (ch.size() == 3);
    CHECK (ch[2].name == "Z");

    CountingListener l;
    r.target.addListener (&l);
    r.doc.getTrack (1).setProperty (SongIDs::name, "Edited", nullptr);
    r.target.flushPendingForTesting();
    CHECK (l.count == 1);
    r.target.removeListener (&l);
}

TEST_CASE ("SourceMidiTarget: a Song with no tracks or only a conductor has no channels", "[mixer][target]")
{
    Rig r;
    CHECK (r.target.channels().empty());
    r.doc.getConductorTrack();   // conductor only
    CHECK (r.target.channels().empty());
}

TEST_CASE ("ActivePlaybackTarget: hands back the initial target", "[mixer][target]")
{
    Rig r;
    ActivePlaybackTarget active (r.target);
    CHECK (&active.get() == &r.target);
}

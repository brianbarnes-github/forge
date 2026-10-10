#include "PlaybackTestSupport.h"
#include "UI/SongDocument.h"
#include "UI/TrackRowComponent.h"
#include "UI/TimelineViewState.h"
#include "UI/SongsmithColours.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

TEST_CASE ("TrackRowComponent: double-click fires onTrackDoubleClicked with the row's trackId", "[track-row]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCC, 0, 0);
        SongDocument::getNotesNode (track).addChild (juce::ValueTree (SongIDs::NOTE), -1, nullptr);
    const auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);

    TimelineViewState viewState;
    TrackRowComponent row (track, 1, viewState);

    juce::int64 firedId = -1;
    row.onTrackDoubleClicked = [&] (juce::int64 id) { firedId = id; };

    row.mouseDoubleClick (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                             juce::Point<float> (5.0f, 5.0f), juce::ModifierKeys(),
                                             0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &row, &row,
                                             juce::Time::getCurrentTime(), juce::Point<float> (5.0f, 5.0f),
                                             juce::Time::getCurrentTime(), 2, false));

    CHECK (firedId == trackId);
}

TEST_CASE ("TrackRowComponent: double-click opens a note-less track, but not the conductor", "[track-row]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto empty = doc.addTrack ("Emptied", (int) 0xFFAABBCC, 0, 0);   // no notes, e.g. all deleted
    auto conductor = doc.addTrack ("Conductor", (int) 0xFFAABBCC, 0, 0);
    conductor.setProperty (SongIDs::isConductor, true, nullptr);

    TimelineViewState viewState;
    TrackRowComponent emptyRow (empty, 1, viewState);
    TrackRowComponent conductorRow (conductor, 2, viewState);

    int emptyFired = 0, conductorFired = 0;
    emptyRow.onTrackDoubleClicked = [&] (juce::int64) { ++emptyFired; };
    conductorRow.onTrackDoubleClicked = [&] (juce::int64) { ++conductorFired; };

    auto doubleClick = [] (TrackRowComponent& row)
    {
        row.mouseDoubleClick (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                                 juce::Point<float> (5.0f, 5.0f), juce::ModifierKeys(),
                                                 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &row, &row,
                                                 juce::Time::getCurrentTime(), juce::Point<float> (5.0f, 5.0f),
                                                 juce::Time::getCurrentTime(), 2, false));
    };
    doubleClick (emptyRow);
    doubleClick (conductorRow);

    CHECK (emptyFired == 1);
    CHECK (conductorFired == 0);
    CHECK_FALSE (emptyRow.canDrag());   // still can't be dragged to a part
}

namespace
{
    // Mirrors the MouseEvent construction the double-click test above uses,
    // but targeted at an arbitrary component and click count so the
    // preview-forwarding tests can aim at the embedded TrackNotePreview.
    juce::MouseEvent eventAt (juce::Component& target, juce::Point<int> localPos, int numClicks,
                              juce::ModifierKeys mods = {})
    {
        const auto pos = localPos.toFloat();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                  pos, mods,
                                  0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target,
                                  juce::Time::getCurrentTime(), pos,
                                  juce::Time::getCurrentTime(), numClicks, false);
    }
}

TEST_CASE ("TrackRowComponent: clicks on the note preview outside its ghost toggle still reach the row", "[track-row]")
{
    // The preview is a hit-testable child covering the right 160px of the
    // row, and JUCE does not forward a child's unhandled mouse events to its
    // parent -- so without explicit forwarding that whole strip is dead to
    // the row's own select/double-click-to-edit gestures.
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCC, 0, 0);
    SongDocument::getNotesNode (track).addChild (juce::ValueTree (SongIDs::NOTE), -1, nullptr);
    const auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);

    TimelineViewState viewState;
    TrackRowComponent row (track, 1, viewState);
    row.setBounds (0, 0, 300, TrackRowComponent::defaultRowHeight);

    auto& preview = row.notePreviewForTesting();
    const juce::Point<int> awayFromToggle { 20, 20 };
    REQUIRE_FALSE (preview.ghostToggleBounds().contains (awayFromToggle));

    juce::int64 selectedId = -1;
    juce::int64 doubleClickedId = -1;
    bool ghostFired = false;
    row.onStripSelected = [&] (juce::int64 id, const juce::ModifierKeys&) { selectedId = id; };
    row.onTrackDoubleClicked = [&] (juce::int64 id) { doubleClickedId = id; };
    row.onGhostToggled = [&] (juce::int64, bool) { ghostFired = true; };

    bool infoFired = false;
    row.onTrackSelected = [&] (juce::int64, const juce::ModifierKeys&) { infoFired = true; };

    preview.mouseDown (eventAt (preview, awayFromToggle, 1));
    CHECK (selectedId == trackId);
    CHECK_FALSE (infoFired);   // a strip click is reported through onStripSelected only

    preview.mouseDoubleClick (eventAt (preview, awayFromToggle, 2));
    CHECK (doubleClickedId == trackId);

    // A miss on the toggle must not also report a ghost change.
    CHECK_FALSE (ghostFired);
    CHECK_FALSE (preview.isGhostVisible());
}

TEST_CASE ("TrackRowComponent: a click on the ghost toggle toggles the ghost and does not select or open the row", "[track-row]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCC, 0, 0);

    TimelineViewState viewState;
    TrackRowComponent row (track, 1, viewState);
    row.setBounds (0, 0, 300, TrackRowComponent::defaultRowHeight);

    auto& preview = row.notePreviewForTesting();

    bool ghostFired = false;
    juce::int64 selectedId = -1;
    juce::int64 doubleClickedId = -1;
    row.onGhostToggled = [&] (juce::int64, bool) { ghostFired = true; };
    row.onStripSelected = [&] (juce::int64 id, const juce::ModifierKeys&) { selectedId = id; };
    row.onTrackDoubleClicked = [&] (juce::int64 id) { doubleClickedId = id; };

    preview.mouseDown (eventAt (preview, preview.ghostToggleBounds().getCentre(), 1));

    CHECK (ghostFired);
    CHECK (preview.isGhostVisible());
    CHECK (selectedId == -1);
    CHECK (doubleClickedId == -1);
}

TEST_CASE ("TrackRowComponent: ghost-toggle forwarding reports this row's trackId alongside the new state", "[track-row]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCC, 0, 0);
    const auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);

    TimelineViewState viewState;
    TrackRowComponent row (track, 1, viewState);
    row.setBounds (0, 0, 200, TrackRowComponent::defaultRowHeight);

    juce::int64 firedId = -1;
    bool firedVisible = false;
    row.onGhostToggled = [&] (juce::int64 id, bool visible) { firedId = id; firedVisible = visible; };

    // Drive it through the row's embedded preview directly rather than a
    // synthetic top-level MouseEvent -- see TrackNotePreview_tests.cpp for
    // the same point-based convention.
    row.notePreviewForTesting().toggleGhostIfHit (row.notePreviewForTesting().ghostToggleBounds().getCentre());

    CHECK (firedId == trackId);
    CHECK (firedVisible);
}

TEST_CASE ("TrackRowComponent: a divider line spans the row's full width along its bottom edge", "[track-row]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCC, 0, 0);

    TimelineViewState viewState;
    TrackRowComponent row (track, 1, viewState);
    constexpr int width = 600;
    row.setBounds (0, 0, width, TrackRowComponent::defaultRowHeight);

    juce::Image image (juce::Image::ARGB, width, TrackRowComponent::defaultRowHeight, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    row.paintEntireComponent (g, false);

    const auto divider = juce::Colour (SongsmithColours::trackDivider);
    const int bottom = TrackRowComponent::defaultRowHeight - 1;

    // Across both the info column and the note preview (which must not paint
    // over it).
    CHECK (image.getPixelAt (20, bottom) == divider);
    CHECK (image.getPixelAt (TrackRowComponent::trackInfoWidth + 50, bottom) == divider);
    CHECK (image.getPixelAt (width - 30, bottom) == divider);

    // Only the bottom pixel row — the line is 1px thick.
    CHECK (image.getPixelAt (20, bottom - 1) != divider);
    CHECK (image.getPixelAt (TrackRowComponent::trackInfoWidth + 50, bottom - 1) != divider);
}

TEST_CASE ("TrackRowComponent: a vertical divider separates the info column from the note preview", "[track-row]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCC, 0, 0);

    TimelineViewState viewState;
    TrackRowComponent row (track, 1, viewState);
    constexpr int width = 600;
    row.setBounds (0, 0, width, TrackRowComponent::defaultRowHeight);

    juce::Image image (juce::Image::ARGB, width, TrackRowComponent::defaultRowHeight, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    row.paintEntireComponent (g, false);

    const auto line = juce::Colour (SongsmithColours::columnDivider);
    const int edge = TrackRowComponent::trackInfoWidth;

    // The line sits at the info column's right edge, over the full row height
    // (above the bottom divider), including where the M / S buttons are.
    for (int y : { 0, TrackRowComponent::defaultRowHeight / 2, TrackRowComponent::defaultRowHeight - 2 })
        for (int t = 1; t <= TrackRowComponent::columnDividerThickness; ++t)
            CHECK (image.getPixelAt (edge - t, y) == line);

    // Not wider than that, and the preview starts right after it.
    CHECK (image.getPixelAt (edge - TrackRowComponent::columnDividerThickness - 1, 4) != line);
    CHECK (image.getPixelAt (edge, 4) != line);
}

TEST_CASE ("TrackRowComponent: the conductor row reads 'N events' and can't be dragged", "[trackrow][fidelity]")
{
    SongDocument doc;
    auto conductor = doc.getConductorTrack();
    for (int i = 0; i < 3; ++i)
        SongDocument::getEventsNode (conductor).addChild (juce::ValueTree (SongIDs::EVENT), -1, nullptr);

    TimelineViewState view;
    TrackRowComponent row (conductor, 0, view);
    CHECK (row.buildSecondLineForTesting() == "3 events");
    CHECK_FALSE (row.canDrag());
}

TEST_CASE ("TrackRowComponent: a note-less track reads '0 notes, N events' and can't be dragged", "[trackrow][fidelity]")
{
    SongDocument doc;
    auto track = doc.addTrackBulk ("Lyrics", 0, 0, 1);
    SongDocument::getEventsNode (track).addChild (juce::ValueTree (SongIDs::EVENT), -1, nullptr);

    TimelineViewState view;
    TrackRowComponent row (track, 1, view);
    CHECK (row.buildSecondLineForTesting() == juce::String::fromUTF8 ("0 notes \xc2\xb7 1 events"));
    CHECK_FALSE (row.canDrag());
}

TEST_CASE ("TrackRowComponent: double-clicking a non-assignable row does not fire onTrackDoubleClicked", "[trackrow][fidelity]")
{
    SongDocument doc;
    TimelineViewState view;
    TrackRowComponent row (doc.getConductorTrack(), 0, view);
    bool fired = false;
    row.onTrackDoubleClicked = [&] (juce::int64) { fired = true; };
    row.setBounds (0, 0, 400, TrackRowComponent::defaultRowHeight);
    row.mouseDoubleClick (eventAt (row, { 5, 5 }, 2));
    CHECK_FALSE (fired);
}

namespace
{
    // Button::triggerClick() only posts a command message; pump the loop so the click lands.
    void click (juce::Button& b)
    {
        b.triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    }
}

TEST_CASE ("TrackRowComponent: M and S buttons fire callbacks with the trackId and toggled state", "[track-row][mutesolo]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCC, 0, 0);
    const auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);
    TimelineViewState viewState;
    TrackRowComponent row (track, 1, viewState);

    juce::int64 mutedId = -1, soloedId = -1;
    bool mutedState = false, soloedState = false;
    row.onMuteToggled = [&] (juce::int64 id, bool s) { mutedId = id; mutedState = s; };
    row.onSoloToggled = [&] (juce::int64 id, bool s) { soloedId = id; soloedState = s; };

    click (row.muteButtonForTesting());
    click (row.soloButtonForTesting());
    CHECK (mutedId == trackId);
    CHECK (mutedState);
    CHECK (soloedId == trackId);
    CHECK (soloedState);
}

TEST_CASE ("TrackRowComponent: the conductor row has no mute/solo buttons", "[track-row][mutesolo]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    TimelineViewState viewState;
    TrackRowComponent row (doc.getConductorTrack(), 0, viewState);
    CHECK (! row.muteButtonForTesting().isVisible());
    CHECK (! row.soloButtonForTesting().isVisible());
}

TEST_CASE ("TrackRowComponent: setMuteSolo reflects state on the buttons", "[track-row][mutesolo]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCC, 0, 0);
    TimelineViewState viewState;
    TrackRowComponent row (track, 1, viewState);
    row.setMuteSolo (true, false, false);
    CHECK (row.muteButtonForTesting().getToggleState());
    CHECK (! row.soloButtonForTesting().getToggleState());
    row.setMuteSolo (false, true, false);
    CHECK (! row.muteButtonForTesting().getToggleState());
    CHECK (row.soloButtonForTesting().getToggleState());
}

TEST_CASE ("TrackRowComponent: a click in the note preview reports the tick under it, but not on the ghost toggle", "[track-row][marker]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCC, 0, 0);
    TimelineViewState viewState;
    TrackRowComponent row (track, 1, viewState);
    row.setBounds (0, 0, 300, TrackRowComponent::defaultRowHeight);
    auto& preview = row.notePreviewForTesting();

    std::vector<int> ticks;
    row.onTimelineClicked = [&] (int tick) { ticks.push_back (tick); };

    const juce::Point<int> awayFromToggle { 20, 20 };
    preview.mouseDown (eventAt (preview, awayFromToggle, 1));
    REQUIRE (ticks.size() == 1);
    CHECK (ticks[0] == viewState.tickForX (20));

    preview.mouseDown (eventAt (preview, preview.ghostToggleBounds().getCentre(), 1));
    CHECK (ticks.size() == 1);
}

TEST_CASE ("TrackRowComponent: strip and info-column clicks report their modifiers through separate callbacks", "[track-row][selection]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCC, 0, 0);
    SongDocument::getNotesNode (track).addChild (juce::ValueTree (SongIDs::NOTE), -1, nullptr);
    const auto trackId = (juce::int64) track.getProperty (SongIDs::trackId);

    TimelineViewState viewState;
    TrackRowComponent row (track, 1, viewState);
    row.setBounds (0, 0, 300, TrackRowComponent::defaultRowHeight);

    juce::ModifierKeys infoMods, stripMods;
    juce::int64 infoId = -1, stripId = -1;
    row.onTrackSelected = [&] (juce::int64 id, const juce::ModifierKeys& m) { infoId = id; infoMods = m; };
    row.onStripSelected = [&] (juce::int64 id, const juce::ModifierKeys& m) { stripId = id; stripMods = m; };

    auto& preview = row.notePreviewForTesting();
    preview.mouseDown (eventAt (preview, { 20, 20 }, 1, juce::ModifierKeys (juce::ModifierKeys::shiftModifier)));
    CHECK (stripId == trackId);
    CHECK (stripMods.isShiftDown());
    CHECK (infoId == -1);

    row.mouseDown (eventAt (row, { 10, 10 }, 1, juce::ModifierKeys (juce::ModifierKeys::ctrlModifier)));
    CHECK (infoId == trackId);
    CHECK (infoMods.isCtrlDown());
    CHECK_FALSE (infoMods.isShiftDown());
}

TEST_CASE ("TrackRowComponent: at the minimum and maximum row heights nothing clips or overlaps", "[track-row][wheel]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCC, 0, 0);
    SongDocument::getNotesNode (track).addChild (juce::ValueTree (SongIDs::NOTE), -1, nullptr);

    for (const int height : { TrackRowComponent::minRowHeight, TrackRowComponent::maxRowHeight })
    {
        TimelineViewState viewState;
        TrackRowComponent row (track, 1, viewState);
        row.setBounds (0, 0, 400, height);

        const auto inside = row.getLocalBounds().withTrimmedBottom (TrackRowComponent::dividerThickness);
        auto& m = row.muteButtonForTesting();
        auto& s = row.soloButtonForTesting();
        auto& preview = row.notePreviewForTesting();

        CHECK (inside.contains (m.getBounds()));
        CHECK (inside.contains (s.getBounds()));
        CHECK (inside.contains (preview.getBounds()));
        CHECK_FALSE (m.getBounds().intersects (s.getBounds()));
        CHECK_FALSE (m.getBounds().intersects (preview.getBounds()));
        CHECK_FALSE (s.getBounds().intersects (preview.getBounds()));
        // Buttons stay tall enough to read and click, and centred in the head.
        CHECK (m.getHeight() >= 12);
        CHECK (m.getY() - inside.getY() == inside.getBottom() - m.getBottom());
        // The preview sits under the instrument band, over the canvas side only.
        CHECK (preview.getY() == TrackRowComponent::instrumentBandHeight);
        CHECK (preview.getHeight() == height - TrackRowComponent::instrumentBandHeight - TrackRowComponent::dividerThickness);

        // The two text lines (11 px and 9 px fonts) each get half of the head's height:
        // enough for their glyphs at the minimum.
        CHECK (inside.getHeight() / 2 >= 13);
    }
}

TEST_CASE ("TrackRowComponent: the instrument band names the GM program, 'Drum Kit' on channel 10, nothing on the conductor", "[track-row][band]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    TimelineViewState viewState;

    auto bass = doc.addTrack ("Track A", (int) 0xFFAABBCC, 0, 0);
    bass.setProperty (SongIDs::sourceProgram, 33, nullptr);
    bass.setProperty (SongIDs::sourceMidiChannel, 2, nullptr);
    CHECK (TrackRowComponent (bass, 1, viewState).instrumentLabel() == "Electric Bass (finger)");

    auto drums = doc.addTrack ("Track B", (int) 0xFFAABBCC, 0, 0);
    drums.setProperty (SongIDs::sourceProgram, 33, nullptr);   // ignored on channel 10
    drums.setProperty (SongIDs::sourceMidiChannel, 10, nullptr);
    CHECK (TrackRowComponent (drums, 2, viewState).instrumentLabel() == "Drum Kit");

    auto conductor = doc.addTrack ("Conductor", (int) 0xFFAABBCC, 0, 0);
    conductor.setProperty (SongIDs::isConductor, true, nullptr);
    CHECK (TrackRowComponent (conductor, 0, viewState).instrumentLabel().isEmpty());
}

TEST_CASE ("TrackRowComponent: the band is painted across the canvas side only, above the notes", "[track-row][band]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto track = doc.addTrack ("Track A", (int) 0xFFAABBCC, 0, 0);
    TimelineViewState viewState;
    TrackRowComponent row (track, 1, viewState);
    row.setBounds (0, 0, 400, TrackRowComponent::defaultRowHeight + TrackRowComponent::instrumentBandHeight);

    const auto image = row.createComponentSnapshot (row.getLocalBounds(), false);
    const auto band = juce::Colour (SongsmithColours::instrumentBand);
    const int canvasX = TrackRowComponent::trackInfoWidth + 2;
    CHECK (image.getPixelAt (399, 1) == band);                                        // right end of the band
    CHECK (image.getPixelAt (canvasX, TrackRowComponent::instrumentBandHeight - 2) == band);
    CHECK (image.getPixelAt (canvasX, TrackRowComponent::instrumentBandHeight + 4) != band);   // notes area below
    CHECK (image.getPixelAt (4, 1) != band);                                          // head column unchanged
}

namespace
{
    // First menu item (searching submenus) with this id, or nullptr.
    const juce::PopupMenu::Item* findItem (const juce::PopupMenu& menu, int id, std::vector<juce::PopupMenu::Item>& keep)
    {
        juce::PopupMenu::MenuItemIterator it (menu);
        while (it.next())
        {
            keep.push_back (it.getItem());
            if (it.getItem().itemID == id)
                return &keep.back();
            if (it.getItem().subMenu != nullptr)
                if (const auto* sub = findItem (*it.getItem().subMenu, id, keep))
                    return sub;
        }
        return nullptr;
    }
}

TEST_CASE ("TrackRowComponent: the band menu offers auto split only when the track changes instrument", "[track-row][instrument]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto multi = playbacktest::addTrack (doc, "Multi");
    multi.setProperty (SongIDs::sourceProgram, 73, nullptr);
    multi.setProperty (SongIDs::endTick, 1920, nullptr);
    playbacktest::addEvent (multi, 0, { 0xC0, 73 });
    playbacktest::addEvent (multi, 960, { 0xC0, 40 });
    playbacktest::addNote (multi, 60, 0, 480);
    auto single = playbacktest::addTrack (doc, "Single");
    single.setProperty (SongIDs::sourceProgram, 5, nullptr);
    playbacktest::addNote (single, 60, 0, 480);

    TimelineViewState view;
    TrackRowComponent multiRow (multi, 1, view);
    TrackRowComponent singleRow (single, 2, view);
    std::vector<juce::PopupMenu::Item> keep;

    const auto multiMenu = multiRow.buildInstrumentMenu();
    const auto* split = findItem (multiMenu, 1, keep);
    REQUIRE (split != nullptr);
    CHECK (split->isEnabled);

    const auto singleMenu = singleRow.buildInstrumentMenu();
    const auto* disabled = findItem (singleMenu, 1, keep);
    REQUIRE (disabled != nullptr);
    CHECK_FALSE (disabled->isEnabled);
}

TEST_CASE ("TrackRowComponent: the set-instrument list ticks the track's first instrument", "[track-row][instrument]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto t = playbacktest::addTrack (doc);
    t.setProperty (SongIDs::sourceProgram, 73, nullptr);
    playbacktest::addEvent (t, 0, { 0xC0, 73 });
    playbacktest::addNote (t, 60, 0, 480);
    TimelineViewState view;
    TrackRowComponent row (t, 1, view);
    std::vector<juce::PopupMenu::Item> keep;
    const auto menu = row.buildInstrumentMenu();

    const auto* flute = findItem (menu, 1000 + 73, keep);
    const auto* other = findItem (menu, 1000 + 40, keep);

    REQUIRE (flute != nullptr);
    REQUIRE (other != nullptr);
    CHECK (flute->isTicked);
    CHECK_FALSE (other->isTicked);
}

TEST_CASE ("TrackRowComponent: choosing a band menu item fires the matching callback", "[track-row][instrument]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    auto t = playbacktest::addTrack (doc);
    playbacktest::addNote (t, 60, 0, 480);
    const auto id = (juce::int64) t.getProperty (SongIDs::trackId);
    TimelineViewState view;
    TrackRowComponent row (t, 1, view);
    juce::int64 splitId = -1, setId = -1;
    int setProgram = -1;
    row.onAutoSplitRequested = [&] (juce::int64 i) { splitId = i; };
    row.onSetInstrumentRequested = [&] (juce::int64 i, int p) { setId = i; setProgram = p; };

    row.instrumentMenuChosen (1);
    row.instrumentMenuChosen (1000 + 40);
    row.instrumentMenuChosen (0);   // dismissed: nothing

    CHECK (splitId == id);
    CHECK (setId == id);
    CHECK (setProgram == 40);
}

TEST_CASE ("TrackRowComponent: the instrument band is the top strip of the canvas side", "[track-row][instrument]")
{
    CHECK (TrackRowComponent::inInstrumentBand ({ TrackRowComponent::trackInfoWidth + 5, 3 }));
    CHECK_FALSE (TrackRowComponent::inInstrumentBand ({ TrackRowComponent::trackInfoWidth - 5, 3 }));
    CHECK_FALSE (TrackRowComponent::inInstrumentBand ({ TrackRowComponent::trackInfoWidth + 5, TrackRowComponent::instrumentBandHeight }));
}

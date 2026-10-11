// TempoMeterEditorWindow: the editable Tempo and Meter table, driven headlessly.

#include "MidiTestBytes.h"
#include "UI/SongDocument.h"
#include "UI/SongsmithColours.h"
#include "UI/SongModelBridge.h"
#include "UI/TempoEdit.h"
#include "UI/TempoMeterEditorWindow.h"
#include "UI/TempoMeterPositionStrip.h"
#include "UI/TempoMeterTable.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <cmath>
#include <optional>
#include <string>
#include <vector>

using namespace lotro;
using namespace miditest;
using Catch::Approx;

namespace lotro
{
    struct TempoMeterEditorWindowTestAccess
    {
        static int rowCount (TempoMeterEditorWindow& w) { return (int) w.rows.size(); }
        static std::string cellText (TempoMeterEditorWindow& w, int row, int column)
        {
            const auto& r = w.rows.at ((size_t) row);
            switch (column)
            {
                case 1:  return r.kindText;
                case 2:  return r.barBeatText;
                case 3:  return r.tickText;
                default: return r.valueText;
            }
        }
        static const TableRow& rowAt (TempoMeterEditorWindow& w, int row) { return w.rows.at ((size_t) row); }
        static void selectRow (TempoMeterEditorWindow& w, int row) { w.content->table.selectRow (row); }
        static int  styleFlags (TempoMeterEditorWindow& w) { return w.getDesktopWindowStyleFlags(); }
        static void deselectAll (TempoMeterEditorWindow& w) { w.content->table.deselectAllRows(); }
        static int  selectedRow (TempoMeterEditorWindow& w) { return w.content->table.getSelectedRow(); }
        static void commitCell (TempoMeterEditorWindow& w, int row, int column, const std::string& text)
        {
            w.commitCell (row, column, text);
        }
        static std::string message (TempoMeterEditorWindow& w) { return w.content->message.getText().toStdString(); }
        static juce::TextButton& button (TempoMeterEditorWindow& w, const juce::String& name)
        {
            for (auto* b : { &w.content->addTempo, &w.content->addMeter, &w.content->remove })
                if (b->getButtonText() == name)
                    return *b;
            FAIL ("no such button");
            return w.content->remove;
        }
        static bool buttonEnabled (TempoMeterEditorWindow& w, const juce::String& name)
        {
            return button (w, name).isEnabled();
        }
        static void pressAdd (TempoMeterEditorWindow& w, TempoMeterKind kind)
        {
            button (w, kind == TempoMeterKind::tempo ? "Add Tempo" : "Add Meter").onClick();
        }
        static void pressRemove (TempoMeterEditorWindow& w) { button (w, "Remove").onClick(); }
        static void refreshNow (TempoMeterEditorWindow& w) { w.refreshNow(); }
        static juce::Label* cellLabel (TempoMeterEditorWindow& w, int row, int column)
        {
            return dynamic_cast<juce::Label*> (w.content->table.getCellComponent (column, row));
        }
        static void mouseDown (juce::Component& c)
        {
            const auto now = juce::Time::getCurrentTime();
            const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), juce::Point<float> (2.0f, 2.0f),
                                      juce::ModifierKeys::leftButtonModifier, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                      &c, &c, now, juce::Point<float> (2.0f, 2.0f), now, 1, false);
            c.mouseDown (e);
        }
        static std::string readout (TempoMeterEditorWindow& w) { return w.content->readout.getText().toStdString(); }
        static PositionStrip& strip (TempoMeterEditorWindow& w) { return w.content->strip; }
        static juce::Rectangle<int> tableBounds (TempoMeterEditorWindow& w) { return w.content->table.getBounds(); }
        static int rowHeight (TempoMeterEditorWindow& w) { return w.content->table.getRowHeight(); }
        static int headerHeight (TempoMeterEditorWindow& w) { return w.content->table.getHeader().getHeight(); }
        static void resizeContent (TempoMeterEditorWindow& w, int width, int height) { w.content->setSize (width, height); }
        static juce::Rectangle<int> stripBounds (TempoMeterEditorWindow& w) { return w.content->strip.getBounds(); }
        static juce::Rectangle<int> readoutBounds (TempoMeterEditorWindow& w) { return w.content->readout.getBounds(); }
        static bool keyOnTable (TempoMeterEditorWindow& w, const juce::KeyPress& k)
        {
            return w.content->table.keyPressed (k);
        }
    };
}

namespace
{
    using Access = TempoMeterEditorWindowTestAccess;

    constexpr int kindCol = 1, tickCol = 3, valueCol = 4;

    void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil (100); }

    struct TempMidi
    {
        juce::File file = juce::File::createTempFile (".mid");
        explicit TempMidi (const Bytes& bytes) { REQUIRE (file.replaceWithData (bytes.data(), bytes.size())); }
        ~TempMidi() { file.deleteFile(); }
    };

    // 120 BPM and 4/4 at tick 0, one note, PPQ 480.
    Bytes standardSong()
    {
        TrackBody c;
        c.ev (0, { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 }).ev (0, { 0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08 })
         .ev (0, { 0xFF, 0x59, 0x02, 0x00, 0x00 }).eot (3840);
        TrackBody n;
        n.ev (0, { 0x90, 60, 100 }).ev (480, { 0x80, 60, 0x40 }).eot();
        return smf (1, 480, { c, n });
    }

    void importStandard (SongDocument& doc)
    {
        TempMidi midi (standardSong());
        Diagnostics d;
        REQUIRE (importMidiFile (doc, midi.file, 1, d));
        doc.getUndoManager().clearUndoHistory();
    }
}

TEST_CASE ("TempoMeterEditorWindow: lists the events and refreshes on edit, undo and redo", "[tempo-meter-window]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);

    TempoMeterEditorWindow w (doc, nullptr);
    REQUIRE (Access::rowCount (w) == 2);
    CHECK (Access::cellText (w, 0, kindCol) == "Tempo");
    CHECK (Access::cellText (w, 1, kindCol) == "Meter");
    CHECK (Access::cellText (w, 1, valueCol) == "4/4");

    REQUIRE_FALSE (setTempo (doc, 1920, 90));
    pump();
    REQUIRE (Access::rowCount (w) == 3);
    CHECK (Access::cellText (w, 2, valueCol) == "90");

    doc.undo();
    pump();
    CHECK (Access::rowCount (w) == 2);

    doc.redo();
    pump();
    CHECK (Access::rowCount (w) == 3);
}

TEST_CASE ("TempoMeterEditorWindow: typing a value edits through TempoEdit and one undo restores it", "[tempo-meter-window]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    TempoMeterEditorWindow w (doc, nullptr);

    Access::commitCell (w, 0, valueCol, "100");
    CHECK (listTempoMeterEvents (doc)[0].bpm == Approx (100.0).margin (0.01));
    pump();
    CHECK (Access::cellText (w, 0, valueCol) == "100");

    doc.undo();
    pump();
    CHECK (listTempoMeterEvents (doc)[0].bpm == Approx (120.0).margin (0.01));
    CHECK (Access::cellText (w, 0, valueCol) == "120");
    CHECK (Access::message (w).empty());
}

TEST_CASE ("TempoMeterEditorWindow: a rejected edit shows the message and leaves the document alone", "[tempo-meter-window]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    TempoMeterEditorWindow w (doc, nullptr);
    const auto before = listTempoMeterEvents (doc);

    Access::commitCell (w, 0, valueCol, "abc");
    CHECK (Access::message (w) == describe (TableEditError { TableEditError::Kind::UnparsableBpm, std::nullopt }));
    CHECK_FALSE (doc.canUndo());
    CHECK (listTempoMeterEvents (doc).size() == before.size());
    CHECK (listTempoMeterEvents (doc)[0].bpm == Approx (before[0].bpm));

    Access::commitCell (w, 1, valueCol, "3/5");
    CHECK (Access::message (w).find ("Meter must be") != std::string::npos);
    CHECK_FALSE (doc.canUndo());
    CHECK (listTempoMeterEvents (doc)[1].numerator == 4);
    CHECK (Access::cellText (w, 1, valueCol) == "4/4");
}

TEST_CASE ("TempoMeterEditorWindow: a meter tick edit shows the snapped tick", "[tempo-meter-window]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    REQUIRE_FALSE (setMeter (doc, 1920, 3, 4));
    TempoMeterEditorWindow w (doc, nullptr);
    REQUIRE (Access::rowCount (w) == 3);
    REQUIRE (Access::cellText (w, 2, kindCol) == "Meter");
    REQUIRE (Access::cellText (w, 2, tickCol) == "1920");

    // Without this meter the bars are 1920 long, so 3000 is nearest the bar start at 3840.
    Access::commitCell (w, 2, tickCol, "3000");
    pump();
    REQUIRE (Access::rowCount (w) == 3);
    CHECK (Access::cellText (w, 2, tickCol) == "3840");
    CHECK (Access::message (w).empty());
}

TEST_CASE ("TempoMeterEditorWindow: selection follows (kind, tick) across a refresh and is dropped when its row disappears", "[tempo-meter-window]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    REQUIRE_FALSE (setTempo (doc, 1920, 100));
    REQUIRE_FALSE (setMeter (doc, 1920, 3, 4));
    TempoMeterEditorWindow w (doc, nullptr);
    REQUIRE (Access::rowCount (w) == 4);   // T0 M0 T1920 M1920

    Access::selectRow (w, 3);
    REQUIRE (Access::selectedRow (w) == 3);

    REQUIRE_FALSE (setTempo (doc, 960, 100));   // a row appears above the selection
    pump();
    REQUIRE (Access::rowCount (w) == 5);
    REQUIRE (Access::selectedRow (w) == 4);
    CHECK (Access::rowAt (w, 4).kind == TempoMeterKind::meter);
    CHECK (Access::rowAt (w, 4).tick == 1920);

    REQUIRE_FALSE (removeMeter (doc, 1920));
    pump();
    CHECK (Access::rowCount (w) == 4);
    CHECK (Access::selectedRow (w) == -1);
}

TEST_CASE ("TempoMeterEditorWindow: buttons add and remove, and Remove on tick 0 explains itself", "[tempo-meter-window]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    TempoMeterEditorWindow w (doc, nullptr);

    Access::pressRemove (w);
    CHECK (Access::message (w) == "Select an event first");

    Access::pressAdd (w, TempoMeterKind::tempo);
    pump();
    REQUIRE (Access::rowCount (w) == 3);
    CHECK (Access::cellText (w, 2, kindCol) == "Tempo");
    CHECK (Access::cellText (w, 2, tickCol) == "1920");

    Access::selectRow (w, 2);
    Access::pressRemove (w);
    pump();
    CHECK (Access::rowCount (w) == 2);

    Access::selectRow (w, 0);
    Access::pressRemove (w);
    CHECK (Access::message (w)
           == describe (TableEditError { TableEditError::Kind::Rejected, TempoEditError::CannotRemoveFirst }));
    CHECK (Access::rowCount (w) == 2);
}

TEST_CASE ("TempoMeterEditorWindow: with no time base the table is empty and every button is disabled", "[tempo-meter-window]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    REQUIRE_FALSE (doc.hasTimeBase());

    TempoMeterEditorWindow w (doc, nullptr);
    CHECK (Access::rowCount (w) == (int) listTempoMeterEvents (doc).size());
    CHECK_FALSE (Access::buttonEnabled (w, "Add Tempo"));
    CHECK_FALSE (Access::buttonEnabled (w, "Add Meter"));
    CHECK_FALSE (Access::buttonEnabled (w, "Remove"));

    importStandard (doc);
    pump();
    CHECK (Access::rowCount (w) == 2);
    CHECK (Access::buttonEnabled (w, "Add Tempo"));
    CHECK (Access::buttonEnabled (w, "Add Meter"));
    CHECK (Access::buttonEnabled (w, "Remove"));
}

TEST_CASE ("TempoMeterEditorWindow: Ctrl+Z and Ctrl+Y reach the document's undo manager", "[tempo-meter-window]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    TempoMeterEditorWindow w (doc, nullptr);

    REQUIRE_FALSE (setTempo (doc, 1920, 90));
    REQUIRE (listTempoMeterEvents (doc).size() == 3);

    CHECK (w.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)));
    CHECK (listTempoMeterEvents (doc).size() == 2);
    pump();
    CHECK (Access::rowCount (w) == 2);

    CHECK (w.keyPressed (juce::KeyPress ('y', juce::ModifierKeys::commandModifier, 0)));
    CHECK (listTempoMeterEvents (doc).size() == 3);

    CHECK (w.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)));
    CHECK (listTempoMeterEvents (doc).size() == 2);
    CHECK (w.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)));
    CHECK (listTempoMeterEvents (doc).size() == 3);
}

TEST_CASE ("TempoMeterEditorWindow: closing fires onClosed and hides the window", "[tempo-meter-window]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    TempoMeterEditorWindow w (doc, nullptr);
    REQUIRE (w.isVisible());

    bool closed = false;
    w.onClosed = [&closed] { closed = true; };
    w.closeButtonPressed();
    CHECK (closed);
    CHECK_FALSE (w.isVisible());
}

TEST_CASE ("TempoMeterEditorWindow: a Label commit edits through TempoEdit; a rejected one reverts the cell", "[tempo-meter-window]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    TempoMeterEditorWindow w (doc, nullptr);

    auto* label = Access::cellLabel (w, 0, valueCol);
    REQUIRE (label != nullptr);
    CHECK (label->getText() == "120");

    label->setText ("100", juce::sendNotification);
    CHECK (listTempoMeterEvents (doc)[0].bpm == Approx (100.0).margin (0.01));
    doc.undo();
    CHECK (listTempoMeterEvents (doc)[0].bpm == Approx (120.0).margin (0.01));
    pump();

    label = Access::cellLabel (w, 0, valueCol);
    REQUIRE (label != nullptr);
    doc.getUndoManager().clearUndoHistory();
    label->setText ("abc", juce::sendNotification);
    CHECK (Access::message (w) == describe (TableEditError { TableEditError::Kind::UnparsableBpm, std::nullopt }));
    CHECK_FALSE (doc.canUndo());
    CHECK (label->getText() == "120");   // reverted to the stored value, not left as typed
}

TEST_CASE ("TempoMeterEditorWindow: a Tick Label commit on a meter row shows the snapped tick", "[tempo-meter-window]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    REQUIRE_FALSE (setMeter (doc, 1920, 3, 4));
    TempoMeterEditorWindow w (doc, nullptr);

    auto* label = Access::cellLabel (w, 2, tickCol);
    REQUIRE (label != nullptr);
    label->setText ("3000", juce::sendNotification);
    pump();
    label = Access::cellLabel (w, 2, tickCol);
    REQUIRE (label != nullptr);
    CHECK (label->getText() == "3840");
    CHECK (Access::cellText (w, 2, tickCol) == "3840");
}

TEST_CASE ("TempoMeterEditorWindow: a retained Label targets the event now shown in its row", "[tempo-meter-window]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    REQUIRE_FALSE (setTempo (doc, 1920, 90));
    TempoMeterEditorWindow w (doc, nullptr);
    REQUIRE (Access::cellLabel (w, 2, valueCol) != nullptr);

    REQUIRE_FALSE (setTempo (doc, 960, 100));   // shifts the 1920 tempo from row 2 to row 3
    pump();
    REQUIRE (Access::rowCount (w) == 4);
    auto* label = Access::cellLabel (w, 3, valueCol);
    REQUIRE (label != nullptr);
    CHECK (label->getText() == "90");

    label->setText ("70", juce::sendNotification);
    for (const auto& e : listTempoMeterEvents (doc))
        if (e.kind == TempoMeterKind::tempo && e.tick == 1920)
            CHECK (e.bpm == Approx (70.0).margin (0.01));
        else if (e.kind == TempoMeterKind::tempo && e.tick == 960)
            CHECK (e.bpm == Approx (100.0).margin (0.01));
}

TEST_CASE ("TempoMeterEditorWindow: Delete on the table removes the selected event; Backspace does not", "[tempo-meter-window]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    REQUIRE_FALSE (setTempo (doc, 1920, 90));
    TempoMeterEditorWindow w (doc, nullptr);

    Access::selectRow (w, 2);
    Access::keyOnTable (w, juce::KeyPress (juce::KeyPress::backspaceKey));
    CHECK (listTempoMeterEvents (doc).size() == 3);

    Access::keyOnTable (w, juce::KeyPress (juce::KeyPress::deleteKey));
    CHECK (listTempoMeterEvents (doc).size() == 2);
    pump();
    CHECK (Access::rowCount (w) == 2);

    CHECK_FALSE (w.keyPressed (juce::KeyPress (juce::KeyPress::backspaceKey)));
}

TEST_CASE ("TempoMeterEditorWindow: a Tick edit keeps the stored bpm when the displayed text did not change", "[tempo-meter-window]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    REQUIRE_FALSE (setTempo (doc, 1920, 120.0));
    TempoMeterEditorWindow w (doc, nullptr);
    REQUIRE (Access::rowCount (w) == 3);

    Access::commitCell (w, 2, valueCol, "120.004");   // stored 120.004, still displays "120"
    pump();
    REQUIRE (Access::cellText (w, 2, valueCol) == "120");
    double stored = 0.0;
    for (const auto& e : listTempoMeterEvents (doc))
        if (e.kind == TempoMeterKind::tempo && e.tick == 1920)
            stored = e.bpm;
    REQUIRE (stored == Approx (120.004).margin (0.0001));

    Access::commitCell (w, 2, tickCol, "960");
    bool found = false;
    for (const auto& e : listTempoMeterEvents (doc))
        if (e.kind == TempoMeterKind::tempo && e.tick == 960)
        {
            found = true;
            CHECK (e.bpm == Approx (120.004).margin (0.0001));
        }
    CHECK (found);
}

TEST_CASE ("TempoMeterEditorWindow: a Value edit on an event removed behind the window's back does not recreate it", "[tempo-meter-window]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    REQUIRE_FALSE (setTempo (doc, 1920, 90));
    TempoMeterEditorWindow w (doc, nullptr);
    REQUIRE (Access::rowCount (w) == 3);

    REQUIRE_FALSE (removeTempo (doc, 1920));   // no pump: the window still shows the row
    Access::commitCell (w, 2, valueCol, "70");
    CHECK (listTempoMeterEvents (doc).size() == 2);
    CHECK (Access::message (w)
           == describe (TableEditError { TableEditError::Kind::Rejected, TempoEditError::NoSuchEvent }));
}

TEST_CASE ("TempoMeterEditorWindow: mouse-down on a Value or Tick cell selects its row", "[tempo-meter-window]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    REQUIRE_FALSE (setTempo (doc, 1920, 90));
    TempoMeterEditorWindow w (doc, nullptr);
    REQUIRE (Access::rowCount (w) == 3);

    Access::selectRow (w, 0);
    auto* label = Access::cellLabel (w, 2, valueCol);
    REQUIRE (label != nullptr);
    CHECK (label->isEditableOnSingleClick());
    Access::mouseDown (*label);
    CHECK (Access::selectedRow (w) == 2);

    Access::selectRow (w, 0);
    auto* tickLabel = Access::cellLabel (w, 2, tickCol);
    REQUIRE (tickLabel != nullptr);
    Access::mouseDown (*tickLabel);
    CHECK (Access::selectedRow (w) == 2);

    Access::pressRemove (w);
    CHECK (listTempoMeterEvents (doc).size() == 2);
}

TEST_CASE ("TempoMeterEditorWindow: Shift+Backspace and Ctrl+Backspace do not remove", "[tempo-meter-window]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    REQUIRE_FALSE (setTempo (doc, 1920, 90));
    TempoMeterEditorWindow w (doc, nullptr);

    Access::selectRow (w, 2);
    Access::keyOnTable (w, juce::KeyPress (juce::KeyPress::backspaceKey, juce::ModifierKeys::shiftModifier, 0));
    Access::keyOnTable (w, juce::KeyPress (juce::KeyPress::backspaceKey, juce::ModifierKeys::ctrlModifier, 0));
    CHECK (listTempoMeterEvents (doc).size() == 3);
}

TEST_CASE ("TempoMeterEditorWindow: Ctrl+Alt+Z does not undo", "[tempo-meter-window]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    TempoMeterEditorWindow w (doc, nullptr);
    REQUIRE_FALSE (setTempo (doc, 1920, 90));

    w.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier, 0));
    CHECK (listTempoMeterEvents (doc).size() == 3);
}

TEST_CASE ("TempoMeterEditorWindow: the title bar has minimise and close but no maximise button", "[tempo-meter-window]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    TempoMeterEditorWindow w (doc, nullptr);

    const int flags = Access::styleFlags (w);
    CHECK ((flags & juce::ComponentPeer::windowHasMaximiseButton) == 0);
    CHECK ((flags & juce::ComponentPeer::windowHasMinimiseButton) != 0);
    CHECK ((flags & juce::ComponentPeer::windowHasCloseButton) != 0);
    CHECK (w.isResizable());
}

TEST_CASE ("TempoMeterEditorWindow: selecting a row reports that event's tick, no selection reports none", "[tempo-meter-window][event-marker]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    REQUIRE_FALSE (setTempo (doc, 1920, 100));
    REQUIRE_FALSE (setMeter (doc, 2880, 3, 4));
    TempoMeterEditorWindow w (doc, nullptr);
    REQUIRE (Access::rowCount (w) == 4);   // T0 M0 T1920 M2880

    std::vector<std::optional<int>> seen;
    w.onSelectedTickChanged = [&seen] (std::optional<int> t) { seen.push_back (t); };

    Access::selectRow (w, 2);
    REQUIRE (! seen.empty());
    CHECK (seen.back() == std::optional<int> (1920));
    Access::selectRow (w, 3);
    CHECK (Access::rowAt (w, 3).tick != 1920);
    CHECK (seen.back() == std::optional<int> (Access::rowAt (w, 3).tick));
    Access::deselectAll (w);
    CHECK (seen.back() == std::nullopt);
}

TEST_CASE ("TempoMeterEditorWindow: a refresh keeps the marker on the moved row and drops it with the row", "[tempo-meter-window][event-marker]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    REQUIRE_FALSE (setTempo (doc, 1920, 100));
    REQUIRE_FALSE (setMeter (doc, 1920, 3, 4));
    TempoMeterEditorWindow w (doc, nullptr);
    Access::selectRow (w, 3);   // M1920

    std::vector<std::optional<int>> seen;
    w.onSelectedTickChanged = [&seen] (std::optional<int> t) { seen.push_back (t); };

    REQUIRE_FALSE (setTempo (doc, 960, 100));   // row index shifts; the event does not
    pump();
    REQUIRE (Access::selectedRow (w) == 4);
    for (const auto& t : seen)
        CHECK (t == std::optional<int> (1920));

    REQUIRE_FALSE (removeMeter (doc, 1920));
    pump();
    REQUIRE (Access::selectedRow (w) == -1);
    REQUIRE (! seen.empty());
    CHECK (seen.back() == std::nullopt);
}

TEST_CASE ("TempoMeterEditorWindow: closing reports no selected tick", "[tempo-meter-window][event-marker]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    REQUIRE_FALSE (setTempo (doc, 1920, 100));
    TempoMeterEditorWindow w (doc, nullptr);
    Access::selectRow (w, 2);

    std::vector<std::optional<int>> seen;
    w.onSelectedTickChanged = [&seen] (std::optional<int> t) { seen.push_back (t); };
    w.closeButtonPressed();
    REQUIRE (! seen.empty());
    CHECK (seen.back() == std::nullopt);
}

namespace
{
    const std::string dot = "  \xC2\xB7  ";

    std::vector<int> greenColumnsAtTop (PositionStrip& strip)
    {
        juce::Image image (juce::Image::ARGB, strip.getWidth(), strip.getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g (image);
        strip.paintEntireComponent (g, false);
        std::vector<int> xs;
        const juce::Colour green (SongsmithColours::eventMarker);
        for (int x = 0; x < image.getWidth(); ++x)
        {
            const auto c = image.getPixelAt (x, 2);
            if (std::abs (c.getRed() - green.getRed()) <= 3 && std::abs (c.getGreen() - green.getGreen()) <= 3
                && std::abs (c.getBlue() - green.getBlue()) <= 3)
                xs.push_back (x);
        }
        return xs;
    }
}

TEST_CASE ("TempoMeterEditorWindow: the readout follows the selection and the seconds follow a tempo edit", "[tempo-meter-window][position]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    REQUIRE_FALSE (setTempo (doc, 1920, 60));
    REQUIRE_FALSE (setMeter (doc, 3840, 3, 4));
    TempoMeterEditorWindow w (doc, nullptr);
    REQUIRE (Access::rowCount (w) == 4);   // T0 M0 T1920 M3840

    CHECK (Access::readout (w) == "No event selected");

    Access::selectRow (w, 3);   // M3840: 2 s at 120 BPM, then 4 s at 60 BPM
    CHECK (Access::readout (w) == "Selected: bar 3.1" + dot + "tick 3840" + dot + "0:06.0");

    Access::selectRow (w, 2);
    CHECK (Access::readout (w) == "Selected: bar 2.1" + dot + "tick 1920" + dot + "0:02.0");

    Access::selectRow (w, 3);
    Access::commitCell (w, 2, valueCol, "30");   // slower: the second segment now lasts 8 s
    pump();
    REQUIRE (Access::selectedRow (w) == 3);
    CHECK (Access::readout (w) == "Selected: bar 3.1" + dot + "tick 3840" + dot + "0:10.0");

    Access::deselectAll (w);
    CHECK (Access::readout (w) == "No event selected");
}

TEST_CASE ("TempoMeterEditorWindow: the readout clears when a refresh drops the selected event", "[tempo-meter-window][position]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    REQUIRE_FALSE (setTempo (doc, 1920, 60));
    TempoMeterEditorWindow w (doc, nullptr);
    Access::selectRow (w, 2);
    REQUIRE (Access::readout (w).find ("tick 1920") != std::string::npos);

    REQUIRE_FALSE (removeTempo (doc, 1920));
    pump();
    CHECK (Access::readout (w) == "No event selected");
    CHECK_FALSE (Access::strip (w).selectedTick().has_value());
}

TEST_CASE ("TempoMeterEditorWindow: the strip marks the selected event and refreshes after an edit", "[tempo-meter-window][position]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    REQUIRE_FALSE (setMeter (doc, 3840, 3, 4));
    TempoMeterEditorWindow w (doc, nullptr);
    auto& strip = Access::strip (w);
    REQUIRE (strip.getWidth() > 100);
    CHECK (strip.getHeight() == PositionStrip::height);
    CHECK (greenColumnsAtTop (strip).empty());

    Access::selectRow (w, 2);   // M3840
    REQUIRE (strip.selectedTick() == std::optional<int> (3840));
    REQUIRE (strip.endTick() == 4 * 1920);
    auto cols = greenColumnsAtTop (strip);
    REQUIRE (! cols.empty());
    CHECK (std::abs (0.5 * (cols.front() + cols.back() + 1) - strip.xForTick (3840)) <= 1.0);
    const float before = strip.xForTick (3840);

    // An event past the old end stretches the strip, so the selected tick moves left.
    REQUIRE_FALSE (setTempo (doc, 15360, 100));
    pump();
    REQUIRE (strip.endTick() == 15360);
    CHECK (strip.xForTick (3840) < before - 20.0f);
    cols = greenColumnsAtTop (strip);
    REQUIRE (! cols.empty());
    CHECK (std::abs (0.5 * (cols.front() + cols.back() + 1) - strip.xForTick (3840)) <= 1.0);

    // Moving the selected event away drops the selection and the green line.
    Access::commitCell (w, 2, tickCol, "2500");   // meter snaps to the bar start at 1920
    pump();
    CHECK (Access::selectedRow (w) == -1);
    CHECK (greenColumnsAtTop (strip).empty());

    Access::deselectAll (w);
    CHECK (greenColumnsAtTop (strip).empty());
}

TEST_CASE ("TempoMeterEditorWindow: strip and readout fit without squeezing the table below six rows", "[tempo-meter-window][position]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    TempoMeterEditorWindow w (doc, nullptr);

    for (const auto size : { juce::Point<int> (520, 420), juce::Point<int> (360, 300) })
    {
        Access::resizeContent (w, size.x, size.y);
        const auto table = Access::tableBounds (w);
        CHECK (table.getHeight() >= Access::headerHeight (w) + 6 * Access::rowHeight (w));
        const auto strip = Access::stripBounds (w);
        const auto readout = Access::readoutBounds (w);
        CHECK (strip.getHeight() == PositionStrip::height);
        CHECK (strip.getWidth() >= size.x - 20);
        CHECK_FALSE (strip.intersects (table));
        CHECK_FALSE (readout.intersects (table));
        CHECK_FALSE (strip.intersects (readout));
    }
}

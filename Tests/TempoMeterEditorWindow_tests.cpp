// TempoMeterEditorWindow: the editable Tempo and Meter table, driven headlessly.

#include "MidiTestBytes.h"
#include "UI/SongDocument.h"
#include "UI/SongModelBridge.h"
#include "UI/TempoEdit.h"
#include "UI/TempoGraphComponent.h"
#include "UI/TempoMeterEditorWindow.h"
#include "UI/TimelineViewState.h"
#include "UI/TempoMeterTable.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

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
        static std::vector<int> selectedRows (TempoMeterEditorWindow& w)
        {
            std::vector<int> out;
            auto& t = w.content->table;
            for (int i = 0; i < t.getNumSelectedRows(); ++i)
                out.push_back (t.getSelectedRow (i));
            return out;
        }
        static void selectRange (TempoMeterEditorWindow& w, int first, int last)
        {
            w.content->table.selectRangeOfRows (first, last);
        }
        static int  selectedRow (TempoMeterEditorWindow& w) { return w.content->table.getSelectedRow(); }
        static bool rowVisible (TempoMeterEditorWindow& w, int row)
        {
            auto& table = w.content->table;
            const juce::Rectangle<int> rowArea (0, row * table.getRowHeight(), 1, table.getRowHeight());
            return table.getViewport()->getViewArea().contains (rowArea);   // wholly inside
        }
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
        static void mouseDown (juce::Component& c, juce::ModifierKeys extra = {})
        {
            const auto now = juce::Time::getCurrentTime();
            const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), juce::Point<float> (2.0f, 2.0f),
                                      juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | extra.getRawFlags()), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                      &c, &c, now, juce::Point<float> (2.0f, 2.0f), now, 1, false);
            c.mouseDown (e);
        }
        static bool graphVisible (TempoMeterEditorWindow& w) { return w.content->graph != nullptr && w.content->graph->isVisible(); }
        static std::vector<TempoMeterEvent> graphSelected (TempoMeterEditorWindow& w) { return w.content->graph->selectedForTesting(); }
        static std::vector<TempoMeterEvent> graphEvents (TempoMeterEditorWindow& w) { return w.content->graph->eventsForTesting(); }
        static int graphRepaintCount (TempoMeterEditorWindow& w) { return w.content->graph->repaintRequestsForTesting(); }
        static int tableHeight (TempoMeterEditorWindow& w) { return w.content->table.getHeight(); }
        static juce::Rectangle<int> tableBounds (TempoMeterEditorWindow& w) { return w.content->table.getBounds(); }
        static juce::Rectangle<int> graphBounds (TempoMeterEditorWindow& w) { return w.content->graph->getBounds(); }
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

TEST_CASE ("TempoMeterEditorWindow: selectEvent selects the row of that kind and tick, scrolls it into view and reports the tick", "[tempo-meter-window][select-event]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    for (int i = 1; i <= 40; ++i)
        REQUIRE_FALSE (setTempo (doc, i * 1920, 60.0 + i));
    REQUIRE_FALSE (setMeter (doc, 20 * 1920, 3, 4));   // shares a tick with a tempo
    TempoMeterEditorWindow w (doc, nullptr);
    REQUIRE (Access::rowCount (w) == 43);
    REQUIRE (Access::selectedRow (w) == -1);

    std::vector<std::optional<int>> seen;
    w.onSelectedTickChanged = [&seen] (std::optional<int> t) { seen.push_back (t); };

    // Sorted: T0 M0 T1920 ... ; at 20*1920 the tempo and meter rows are adjacent.
    int tempoRow = -1, meterRow = -1, lastRow = -1;
    for (int r = 0; r < Access::rowCount (w); ++r)
    {
        const auto& row = Access::rowAt (w, r);
        if (row.tick == 20 * 1920)
            (row.kind == TempoMeterKind::tempo ? tempoRow : meterRow) = r;
        if (row.tick == 40 * 1920)
            lastRow = r;
    }
    REQUIRE (tempoRow >= 0);
    REQUIRE (meterRow >= 0);
    REQUIRE (tempoRow != meterRow);
    REQUIRE (lastRow >= 0);
    REQUIRE_FALSE (Access::rowVisible (w, lastRow));   // so scrolling is observable

    CHECK (w.selectEvent (TempoMeterKind::meter, 20 * 1920));
    CHECK (Access::selectedRow (w) == meterRow);
    CHECK (seen.back() == std::optional<int> (20 * 1920));
    CHECK (Access::rowVisible (w, meterRow));

    CHECK (w.selectEvent (TempoMeterKind::tempo, 20 * 1920));
    CHECK (Access::selectedRow (w) == tempoRow);

    CHECK (w.selectEvent (TempoMeterKind::tempo, 40 * 1920));
    CHECK (Access::selectedRow (w) == lastRow);
    CHECK (Access::rowVisible (w, lastRow));
    CHECK (seen.back() == std::optional<int> (40 * 1920));
}

TEST_CASE ("TempoMeterEditorWindow: selectEvent for an event that does not exist returns false and changes nothing", "[tempo-meter-window][select-event]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    REQUIRE_FALSE (setTempo (doc, 1920, 100));
    TempoMeterEditorWindow w (doc, nullptr);
    REQUIRE (w.selectEvent (TempoMeterKind::tempo, 1920));
    const int before = Access::selectedRow (w);

    std::vector<std::optional<int>> seen;
    w.onSelectedTickChanged = [&seen] (std::optional<int> t) { seen.push_back (t); };
    CHECK_FALSE (w.selectEvent (TempoMeterKind::meter, 1920));   // right tick, wrong kind
    CHECK_FALSE (w.selectEvent (TempoMeterKind::tempo, 777));
    CHECK (Access::selectedRow (w) == before);
    CHECK (seen.empty());
}

TEST_CASE ("TempoMeterEditorWindow: selectEvent sees an event added a moment ago, before the coalesced refresh ran", "[tempo-meter-window][select-event]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    TempoMeterEditorWindow w (doc, nullptr);
    REQUIRE (Access::rowCount (w) == 2);
    REQUIRE_FALSE (setTempo (doc, 1920, 100));   // refresh is pending, not yet run
    CHECK (w.selectEvent (TempoMeterKind::tempo, 1920));
    CHECK (Access::rowAt (w, Access::selectedRow (w)).tick == 1920);
}

namespace
{
    // 120 BPM / 4/4 at tick 0 plus tempo changes at bars 2..4 (ticks 1920, 3840, 5760): rows 0..4.
    void addThreeTempos (SongDocument& doc)
    {
        REQUIRE_FALSE (setTempo (doc, 1920, 90));
        REQUIRE_FALSE (setTempo (doc, 3840, 100));
        REQUIRE_FALSE (setTempo (doc, 5760, 110));
    }
}

TEST_CASE ("TempoMeterEditorWindow: Ctrl+A selects every row", "[tempo-meter-window][multi-select]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    addThreeTempos (doc);
    TempoMeterEditorWindow w (doc, nullptr);

    Access::selectRow (w, 1);
    CHECK (Access::keyOnTable (w, juce::KeyPress ('a', juce::ModifierKeys::ctrlModifier, 0)));
    CHECK (Access::selectedRows (w).size() == (size_t) Access::rowCount (w));
}

TEST_CASE ("TempoMeterEditorWindow: Ctrl- and Shift-click on a cell extend the selection and do not open the editor", "[tempo-meter-window][multi-select]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    addThreeTempos (doc);
    TempoMeterEditorWindow w (doc, nullptr);

    Access::selectRow (w, 1);
    Access::mouseDown (*Access::cellLabel (w, 3, valueCol), juce::ModifierKeys::ctrlModifier);
    CHECK (Access::selectedRows (w) == std::vector<int> { 1, 3 });

    Access::mouseDown (*Access::cellLabel (w, 4, tickCol), juce::ModifierKeys::shiftModifier);
    CHECK (Access::selectedRows (w) == std::vector<int> { 1, 3, 4 });   // shift extends from the last-clicked row

    Access::selectRow (w, 1);
    Access::mouseDown (*Access::cellLabel (w, 4, valueCol), juce::ModifierKeys::shiftModifier);
    CHECK (Access::selectedRows (w) == std::vector<int> { 1, 2, 3, 4 });

    Access::deselectAll (w);
    Access::mouseDown (*Access::cellLabel (w, 2, valueCol));   // plain click on an unselected row selects just it
    CHECK (Access::selectedRows (w) == std::vector<int> { 2 });
}

TEST_CASE ("TempoMeterEditorWindow: Remove and Delete remove every selected event in one undo, keeping tick 0", "[tempo-meter-window][multi-select]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    addThreeTempos (doc);
    doc.getUndoManager().clearUndoHistory();
    TempoMeterEditorWindow w (doc, nullptr);
    REQUIRE (Access::rowCount (w) == 5);

    Access::selectRange (w, 0, 3);   // tick-0 tempo, tick-0 meter, 1920, 3840
    Access::pressRemove (w);
    pump();
    CHECK (Access::message (w).empty());
    CHECK (Access::rowCount (w) == 3);   // both tick-0 rows and 5760 remain
    CHECK (Access::cellText (w, 2, tickCol) == "5760");

    doc.undo();
    pump();
    CHECK (Access::rowCount (w) == 5);   // one undo restores both

    Access::selectRange (w, 3, 4);
    Access::keyOnTable (w, juce::KeyPress (juce::KeyPress::deleteKey));
    pump();
    CHECK (Access::rowCount (w) == 3);
}

TEST_CASE ("TempoMeterEditorWindow: Remove with only tick-0 rows selected explains itself", "[tempo-meter-window][multi-select]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    TempoMeterEditorWindow w (doc, nullptr);

    Access::selectRange (w, 0, 1);
    Access::pressRemove (w);
    CHECK (Access::message (w)
           == describe (TableEditError { TableEditError::Kind::Rejected, TempoEditError::CannotRemoveFirst }));
    CHECK (Access::rowCount (w) == 2);
}

TEST_CASE ("TempoMeterEditorWindow: a refresh keeps a multi-selection by (kind, tick)", "[tempo-meter-window][multi-select]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    addThreeTempos (doc);
    TempoMeterEditorWindow w (doc, nullptr);

    Access::selectRange (w, 3, 4);   // 3840, 5760
    REQUIRE_FALSE (setTempo (doc, 960, 95));   // a new row appears above them
    pump();
    CHECK (Access::selectedRows (w) == std::vector<int> { 4, 5 });
}

TEST_CASE ("TempoMeterEditorWindow: without a timeline view there is no graph and the layout is unchanged", "[tempo-meter-window][graph]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    TempoMeterEditorWindow w (doc, nullptr);

    CHECK_FALSE (Access::graphVisible (w));
    CHECK (Access::tableBounds (w) == juce::Rectangle<int> (8, 8, 504, 420 - 8 - 8 - 24 - 4 - 28 - 6));
    CHECK (Access::rowVisible (w, 0));
}

TEST_CASE ("TempoMeterEditorWindow: setTimelineView shows a graph above a shorter table and null hides it again", "[tempo-meter-window][graph]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    TempoMeterEditorWindow w (doc, nullptr);
    const auto baseline = Access::tableBounds (w);
    const int baselineHeight = w.getContentComponent()->getHeight();

    TimelineViewState view;
    w.setTimelineView (&view);
    REQUIRE (Access::graphVisible (w));
    CHECK (Access::rowCount (w) == 2);
    CHECK (w.getContentComponent()->getHeight() == 620);
    CHECK (Access::graphBounds (w).getHeight() == 160);
    CHECK (Access::tableBounds (w).getY() == Access::graphBounds (w).getBottom() + 6);
    CHECK (Access::tableHeight (w) > 0);
    // The window grew by 200 px and the graph (160 + 6 gap) took 166 of them.
    CHECK (Access::tableHeight (w) == baseline.getHeight() + (620 - baselineHeight) - 166);
    CHECK (Access::graphEvents (w).size() == 2);

    w.setTimelineView (nullptr);
    CHECK_FALSE (Access::graphVisible (w));
    CHECK (w.getContentComponent()->getHeight() == baselineHeight);
    CHECK (Access::tableBounds (w) == baseline);
}

TEST_CASE ("TempoMeterEditorWindow: table selection reaches the graph, tempo entries only", "[tempo-meter-window][graph]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    REQUIRE_FALSE (setTempo (doc, 1920, 90));
    TimelineViewState view;
    TempoMeterEditorWindow w (doc, nullptr);
    w.setTimelineView (&view);
    pump();
    REQUIRE (Access::rowCount (w) == 3);   // tempo@0, meter@0, tempo@1920

    Access::selectRow (w, 2);
    {
        const auto sel = Access::graphSelected (w);
        REQUIRE (sel.size() == 1);
        CHECK (sel[0].kind == TempoMeterKind::tempo);
        CHECK (sel[0].tick == 1920);
    }

    // A meter row sharing tick 0 with a tempo must not highlight that tempo.
    Access::selectRow (w, 1);
    CHECK (Access::graphSelected (w).empty());
    Access::selectRow (w, 0);
    REQUIRE (Access::graphSelected (w).size() == 1);
    CHECK (Access::graphSelected (w)[0].tick == 0);

    Access::deselectAll (w);
    CHECK (Access::graphSelected (w).empty());
}

TEST_CASE ("TempoMeterEditorWindow: the graph follows edits, undo and redo", "[tempo-meter-window][graph]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    TimelineViewState view;
    TempoMeterEditorWindow w (doc, nullptr);
    w.setTimelineView (&view);
    REQUIRE (Access::graphEvents (w).size() == 2);

    REQUIRE_FALSE (setTempo (doc, 960, 95));
    pump();
    CHECK (Access::graphEvents (w).size() == 3);
    CHECK (Access::graphEvents (w).size() == (size_t) Access::rowCount (w));
    doc.undo();
    pump();
    CHECK (Access::graphEvents (w).size() == 2);
    doc.redo();
    pump();
    CHECK (Access::graphEvents (w).size() == 3);
}

TEST_CASE ("TempoMeterEditorWindow: timelineViewChanged repaints the graph", "[tempo-meter-window][graph]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    SongDocument doc;
    importStandard (doc);
    TimelineViewState view;
    TempoMeterEditorWindow w (doc, nullptr);
    w.timelineViewChanged();   // no view yet: harmless
    w.setTimelineView (&view);

    const int before = Access::graphRepaintCount (w);
    view.setPixelsPerTick (0.2);
    w.timelineViewChanged();
    CHECK (Access::graphRepaintCount (w) == before + 1);
}

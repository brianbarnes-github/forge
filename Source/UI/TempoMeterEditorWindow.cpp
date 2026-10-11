#include "TempoMeterEditorWindow.h"
#include "HistoryKeys.h"
#include "SongsmithColours.h"

namespace lotro
{
    namespace
    {
        // A cell editor Label that also tells the table which row was pressed.
        struct CellLabel : juce::Label
        {
            std::function<void()> onPressed;
            void mouseDown (const juce::MouseEvent& e) override
            {
                if (onPressed)
                    onPressed();
                juce::Label::mouseDown (e);
            }
        };

        const juce::Identifier rowProperty ("tmRow");
        const juce::Identifier columnProperty ("tmColumn");

        bool sameRow (const TableRow& a, const TableRow& b)
        {
            return a.kind == b.kind && a.tick == b.tick && a.kindText == b.kindText
                && a.barBeatText == b.barBeatText && a.tickText == b.tickText
                && a.valueText == b.valueText && a.tickEditable == b.tickEditable;
        }

        bool sameRows (const std::vector<TableRow>& a, const std::vector<TableRow>& b)
        {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); ++i)
                if (! sameRow (a[i], b[i]))
                    return false;
            return true;
        }
    }

    TempoMeterEditorWindow::Content::Content()
    {
        table.setHeaderHeight (24);
        table.setMultipleSelectionEnabled (false);
        table.setColour (juce::ListBox::backgroundColourId, juce::Colour (SongsmithColours::background));
        table.setColour (juce::ListBox::outlineColourId, juce::Colour (SongsmithColours::border));
        table.setOutlineThickness (1);

        message.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::accentAmber));
        message.setMinimumHorizontalScale (1.0f);

        addAndMakeVisible (table);
        addAndMakeVisible (addTempo);
        addAndMakeVisible (addMeter);
        addAndMakeVisible (remove);
        addAndMakeVisible (message);
    }

    void TempoMeterEditorWindow::Content::resized()
    {
        auto area = getLocalBounds().reduced (8);
        message.setBounds (area.removeFromBottom (24));
        area.removeFromBottom (4);
        auto buttons = area.removeFromBottom (28);
        area.removeFromBottom (6);
        table.setBounds (area);

        const int w = 100;
        addTempo.setBounds (buttons.removeFromLeft (w));
        buttons.removeFromLeft (6);
        addMeter.setBounds (buttons.removeFromLeft (w));
        buttons.removeFromLeft (6);
        remove.setBounds (buttons.removeFromLeft (w));
    }

    TempoMeterEditorWindow::TempoMeterEditorWindow (SongDocument& document, juce::Component* centreAround)
        : juce::DocumentWindow ("Tempo and Meter",
                                 juce::Colour (SongsmithColours::background),
                                 juce::DocumentWindow::minimiseButton | juce::DocumentWindow::closeButton),
          doc (document),
          sourceMidiNode (doc.getSourceMidiNode()),
          content (std::make_unique<Content>())
    {
        auto& table = content->table;
        table.setModel (this);
        auto& header = table.getHeader();
        header.addColumn ("Kind",     kindColumn,     80, 40, 400, juce::TableHeaderComponent::notSortable);
        header.addColumn ("Position", positionColumn, 80, 40, 400, juce::TableHeaderComponent::notSortable);
        header.addColumn ("Tick",     tickColumn,     90, 40, 400, juce::TableHeaderComponent::notSortable);
        header.addColumn ("Value",    valueColumn,   120, 40, 400, juce::TableHeaderComponent::notSortable);

        content->addTempo.onClick = [this] { addPressed (TempoMeterKind::tempo); };
        content->addMeter.onClick = [this] { addPressed (TempoMeterKind::meter); };
        content->remove.onClick   = [this] { removePressed(); };

        content->setSize (520, 420);
        setUsingNativeTitleBar (true);
        setResizable (true, false);
        setResizeLimits (360, 240, 900, 800);   // no maximise button; keep it from being dragged huge
        setContentNonOwned (content.get(), true);

        sourceMidiNode.addListener (this);
        refreshNow();

        centreAroundComponent (centreAround, 520, 420);
        setVisible (true);
    }

    TempoMeterEditorWindow::~TempoMeterEditorWindow()
    {
        sourceMidiNode.removeListener (this);
        cancelPendingUpdate();
        content->table.setModel (nullptr);
        setContentNonOwned (nullptr, false);
    }

    void TempoMeterEditorWindow::closeButtonPressed()
    {
        setVisible (false);
        if (onSelectedTickChanged)
            onSelectedTickChanged (std::nullopt);
        if (onClosed)
            onClosed();
    }

    bool TempoMeterEditorWindow::keyPressed (const juce::KeyPress& key)
    {
        if (const auto action = historyActionFor (key))
        {
            if (*action == HistoryAction::Undo)
                doc.undo();
            else
                doc.redo();
            return true;
        }
        // With the table focused ListBox consumes Delete (deleteKeyPressed); this covers
        // focus on a button or elsewhere in the window.
        if (key == juce::KeyPress::deleteKey)
        {
            removePressed();
            return true;
        }
        return juce::DocumentWindow::keyPressed (key);
    }

    // ---- model --------------------------------------------------------------------

    void TempoMeterEditorWindow::paintRowBackground (juce::Graphics& g, int, int, int, bool selected)
    {
        g.fillAll (juce::Colour (selected ? SongsmithColours::selectedRow : SongsmithColours::background));
    }

    void TempoMeterEditorWindow::paintCell (juce::Graphics& g, int row, int column, int width, int height, bool)
    {
        if (row < 0 || row >= (int) rows.size())
            return;
        const auto& r = rows[(size_t) row];

        // Value, and Tick off tick 0, are Labels (refreshComponentForCell).
        juce::String text;
        switch (column)
        {
            case kindColumn:     text = r.kindText; break;
            case positionColumn: text = r.barBeatText; break;
            case tickColumn:     if (! r.tickEditable) text = r.tickText; break;
            default: break;
        }
        const auto colour = column == kindColumn
            ? juce::Colour (r.kind == TempoMeterKind::tempo ? SongsmithColours::conductorTempo
                                                            : SongsmithColours::conductorMeter)
            : juce::Colour (column == tickColumn ? SongsmithColours::textMuted : SongsmithColours::text);
        g.setColour (colour);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (text, 6, 0, width - 8, height, juce::Justification::centredLeft, true);
    }

    void TempoMeterEditorWindow::deleteKeyPressed (int)
    {
        removePressed();
    }

    void TempoMeterEditorWindow::selectedRowsChanged (int)
    {
        notifySelectedTick();
    }

    void TempoMeterEditorWindow::notifySelectedTick()
    {
        if (! onSelectedTickChanged)
            return;
        const auto selected = selectedEvent();
        onSelectedTickChanged (selected ? std::optional<int> (selected->tick) : std::nullopt);
    }

    juce::Component* TempoMeterEditorWindow::refreshComponentForCell (int row, int column, bool,
                                                                       juce::Component* existing)
    {
        const bool wanted = row >= 0 && row < (int) rows.size()
                         && (column == valueColumn || (column == tickColumn && rows[(size_t) row].tickEditable));
        if (! wanted)
        {
            delete existing;   // ownership is handed to us; paintCell draws this cell
            return nullptr;
        }

        auto* label = dynamic_cast<juce::Label*> (existing);
        if (label == nullptr)
        {
            delete existing;
            auto* cell = new CellLabel();
            label = cell;
            // Label swallows the mouse-down, so select the row ourselves.
            cell->onPressed = [this, safe = juce::Component::SafePointer<juce::Label> (cell)]
            {
                if (safe != nullptr)
                    content->table.selectRow ((int) safe->getProperties()[rowProperty]);
            };
            label->setEditable (true, false);   // a single click edits
            label->setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::text));
            label->setColour (juce::Label::backgroundWhenEditingColourId, juce::Colour (SongsmithColours::panelHeader));
            label->setColour (juce::Label::textWhenEditingColourId, juce::Colour (SongsmithColours::text));
            label->setFont (juce::FontOptions (14.0f));
            label->setBorderSize (juce::BorderSize<int> (0, 4, 0, 4));
            label->onTextChange = [this, safe = juce::Component::SafePointer<juce::Label> (label)]
            {
                if (safe != nullptr)
                    commitCell ((int) safe->getProperties()[rowProperty],
                                (int) safe->getProperties()[columnProperty],
                                safe->getText());
            };
        }

        label->getProperties().set (rowProperty, row);
        label->getProperties().set (columnProperty, column);
        const auto& r = rows[(size_t) row];
        label->setText (column == valueColumn ? r.valueText : r.tickText, juce::dontSendNotification);
        return label;
    }

    // ---- refresh ------------------------------------------------------------------

    std::optional<TempoMeterEvent> TempoMeterEditorWindow::selectedEvent() const
    {
        const int row = content->table.getSelectedRow();
        if (row < 0 || row >= (int) events.size())
            return std::nullopt;
        return events[(size_t) row];
    }

    void TempoMeterEditorWindow::updateButtons()
    {
        const bool on = doc.hasTimeBase();
        content->addTempo.setEnabled (on);
        content->addMeter.setEnabled (on);
        content->remove.setEnabled (on);
    }

    void TempoMeterEditorWindow::refreshNow()
    {
        updateButtons();

        const int ticksPerQuarter = (int) sourceMidiNode.getProperty (SongIDs::ticksPerQuarter, 480);
        auto newEvents = listTempoMeterEvents (doc);
        auto newRows   = tableRows (newEvents, doc.getMeterChanges(), ticksPerQuarter);
        if (sameRows (newRows, rows))
        {
            events = std::move (newEvents);   // the stored values may differ below display precision
            return;   // keeps an open cell editor alive
        }

        const auto previous = selectedEvent();
        events = std::move (newEvents);
        rows   = std::move (newRows);

        auto& table = content->table;
        table.updateContent();

        int restored = -1;
        if (previous)
            for (size_t i = 0; i < events.size(); ++i)
                if (events[i].kind == previous->kind && events[i].tick == previous->tick)
                    restored = (int) i;

        if (restored >= 0)
            table.selectRow (restored);
        else
            table.deselectAllRows();
        table.repaint();
        notifySelectedTick();   // idempotent; covers a restore that changed no row index
    }

    void TempoMeterEditorWindow::forceRedisplay()
    {
        content->table.updateContent();
        content->table.repaint();
    }

    // ---- edits --------------------------------------------------------------------

    void TempoMeterEditorWindow::showResult (const TableEditResult& result)
    {
        content->message.setText (result ? juce::String (describe (*result)) : juce::String(),
                                  juce::dontSendNotification);
    }

    void TempoMeterEditorWindow::commitCell (int row, int column, const juce::String& text)
    {
        if (row < 0 || row >= (int) events.size())
            return;

        const auto event = events[(size_t) row];
        if (column == valueColumn)
            showResult (editValue (doc, event, text.toStdString()));
        else if (column == tickColumn)
            showResult (editTick (doc, event, text.toStdString()));
        else
            return;

        // The document may be unchanged (a rejected edit, or text that parses to the stored
        // value), in which case no refresh comes: put the stored text back in the cell.
        forceRedisplay();
    }

    void TempoMeterEditorWindow::addPressed (TempoMeterKind kind)
    {
        showResult (addEvent (doc, kind, selectedEvent()));
    }

    void TempoMeterEditorWindow::removePressed()
    {
        const auto selected = selectedEvent();
        if (! selected)
        {
            content->message.setText ("Select an event first", juce::dontSendNotification);
            return;
        }
        showResult (removeEvent (doc, *selected));
    }
}

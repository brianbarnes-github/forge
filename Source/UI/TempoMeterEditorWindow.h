#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "SongDocument.h"
#include "TempoMeterTable.h"

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace lotro
{
    // Floating Tempo and Meter editor: an editable table over TempoMeterTable
    // (spec 2026-10-10, section 5). Edits go through TempoEdit, so each is one
    // undo step; the table follows the document via a coalesced refresh.
    class TempoMeterEditorWindow : public juce::DocumentWindow,
                                   private juce::TableListBoxModel,
                                   private juce::ValueTree::Listener,
                                   private juce::AsyncUpdater
    {
    public:
        TempoMeterEditorWindow (SongDocument& document, juce::Component* centreAround);
        ~TempoMeterEditorWindow() override;

        void closeButtonPressed() override;
        bool keyPressed (const juce::KeyPress& key) override;

        // Fired when the window is closed by the user.
        std::function<void()> onClosed;

    private:
        friend struct TempoMeterEditorWindowTestAccess;

        enum Column { kindColumn = 1, positionColumn, tickColumn, valueColumn };

        class Content : public juce::Component
        {
        public:
            Content();
            void resized() override;

            // ListBox treats Backspace like Delete (both reach deleteKeyPressed); only
            // Delete should remove an event, so Backspace is declined here.
            struct Table : juce::TableListBox
            {
                bool keyPressed (const juce::KeyPress& k) override
                {
                    return k == juce::KeyPress::backspaceKey ? false : juce::TableListBox::keyPressed (k);
                }
            };
            Table table;
            juce::TextButton   addTempo { "Add Tempo" }, addMeter { "Add Meter" }, remove { "Remove" };
            juce::Label        message;
        };

        // TableListBoxModel
        int  getNumRows() override { return (int) rows.size(); }
        void paintRowBackground (juce::Graphics&, int row, int width, int height, bool selected) override;
        void paintCell (juce::Graphics&, int row, int column, int width, int height, bool selected) override;
        void deleteKeyPressed (int lastRowSelected) override;
        juce::Component* refreshComponentForCell (int row, int column, bool selected,
                                                  juce::Component* existing) override;

        // ValueTree::Listener: every change just asks for a (coalesced) refresh.
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { triggerAsyncUpdate(); }
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { triggerAsyncUpdate(); }
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { triggerAsyncUpdate(); }
        void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { triggerAsyncUpdate(); }
        void valueTreeParentChanged (juce::ValueTree&) override {}
        void handleAsyncUpdate() override { refreshNow(); }

        // Re-lists; if the listing equals the displayed one, does nothing (keeps an open
        // cell editor alive). Else replaces it, restores the selection by (kind, tick)
        // if still present, otherwise clears it, and updateContent()s.
        void refreshNow();
        std::optional<TempoMeterEvent> selectedEvent() const;

        void commitCell (int row, int column, const juce::String& text);
        void showResult (const TableEditResult& result);
        void forceRedisplay();
        void addPressed (TempoMeterKind kind);
        void removePressed();
        void updateButtons();

        SongDocument&                doc;
        juce::ValueTree              sourceMidiNode;   // the listener handle; kept to remove it
        std::vector<TempoMeterEvent> events;
        std::vector<TableRow>        rows;
        std::unique_ptr<Content>     content;
    };
}

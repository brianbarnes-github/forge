#pragma once

#include "UI/Dock/SavedLayouts.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

namespace lotro::dock
{

// The View > Layouts > Manage Layouts... list: select a layout, rename or delete it.
// Edits go straight into the caller's list; onChanged fires after each so the owner can persist.
class ManageLayoutsPanel : public juce::Component, private juce::ListBoxModel
{
public:
    explicit ManageLayoutsPanel (SavedLayouts& listIn);

    std::function<void()> onChanged;
    std::function<void()> onCloseRequested;

    int  selectedRow() const { return listBox.getSelectedRow(); }
    void selectRow (int row) { listBox.selectRow (row); }
    int  rowCount() const { return (int) list.size(); }

    // Renames the selected layout; the error says why it was refused (nothing changed).
    std::optional<LayoutNameError> renameSelected (const std::string& newName);
    void deleteSelected();

    // Asks for a name in a small modal prompt, re-asking with the reason while it is refused.
    void promptRename();

    juce::TextButton& renameButtonForTesting() { return renameButton; }
    juce::TextButton& deleteButtonForTesting() { return deleteButton; }

    void resized() override;

private:
    int  getNumRows() override { return rowCount(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void deleteKeyPressed (int) override { deleteSelected(); }
    void selectedRowsChanged (int) override { refreshButtons(); }
    void refreshButtons();
    void askName (const juce::String& reason);

    SavedLayouts& list;
    juce::ListBox listBox { {}, this };
    juce::TextButton renameButton { "Rename..." }, deleteButton { "Delete" }, closeButton { "Close" };
};

juce::Component::SafePointer<juce::DialogWindow> showManageLayoutsDialog (SavedLayouts& list,
                                                                          std::function<void()> onChanged,
                                                                          juce::Component* centreAround);

} // namespace lotro::dock

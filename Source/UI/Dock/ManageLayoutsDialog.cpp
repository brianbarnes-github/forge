#include "UI/Dock/ManageLayoutsDialog.h"

#include "UI/SongsmithColours.h"

namespace lotro::dock
{
namespace
{
constexpr int dialogWidth = 340;
constexpr int dialogHeight = 300;
constexpr int rowHeight = 24;
constexpr int margin = 10;
constexpr int buttonHeight = 26;
}

ManageLayoutsPanel::ManageLayoutsPanel (SavedLayouts& listIn) : list (listIn)
{
    listBox.setRowHeight (rowHeight);
    listBox.setColour (juce::ListBox::backgroundColourId, juce::Colour (SongsmithColours::background));
    addAndMakeVisible (listBox);

    renameButton.onClick = [this] { promptRename(); };
    deleteButton.onClick = [this] { deleteSelected(); };
    closeButton.onClick = [this] { if (onCloseRequested) onCloseRequested(); };
    for (auto* b : { &renameButton, &deleteButton, &closeButton })
        addAndMakeVisible (*b);

    setSize (dialogWidth, dialogHeight);
    refreshButtons();
}

void ManageLayoutsPanel::resized()
{
    auto area = getLocalBounds().reduced (margin);
    auto buttons = area.removeFromBottom (buttonHeight);
    area.removeFromBottom (margin);
    listBox.setBounds (area);
    closeButton.setBounds (buttons.removeFromRight (80));
    buttons.removeFromRight (margin);
    deleteButton.setBounds (buttons.removeFromRight (80));
    buttons.removeFromRight (margin);
    renameButton.setBounds (buttons.removeFromRight (90));
}

void ManageLayoutsPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= rowCount())
        return;
    if (selected)
        g.fillAll (juce::Colour (SongsmithColours::selectedRow));
    g.setColour (juce::Colour (SongsmithColours::text));
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (juce::String (list[(size_t) row].name), 8, 0, width - 8, height, juce::Justification::centredLeft);
}

void ManageLayoutsPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    listBox.selectRow (row);
    promptRename();
}

void ManageLayoutsPanel::refreshButtons()
{
    const bool any = selectedRow() >= 0 && selectedRow() < rowCount();
    renameButton.setEnabled (any);
    deleteButton.setEnabled (any);
}

std::optional<LayoutNameError> ManageLayoutsPanel::renameSelected (const std::string& newName)
{
    const int row = selectedRow();
    if (row < 0 || row >= rowCount())
        return LayoutNameError::empty;
    const auto oldName = list[(size_t) row].name;
    if (const auto error = renameLayout (list, (std::size_t) row, newName))
        return error;
    listBox.repaint();
    if (onRenamed) onRenamed (oldName, list[(size_t) row].name);
    if (onChanged) onChanged();
    return std::nullopt;
}

void ManageLayoutsPanel::deleteSelected()
{
    const int row = selectedRow();
    if (row < 0 || row >= rowCount())
        return;
    const auto name = list[(size_t) row].name;
    removeLayout (list, (std::size_t) row);
    listBox.updateContent();
    listBox.selectRow (juce::jmin (row, rowCount() - 1));   // -1 when the list is now empty
    refreshButtons();
    listBox.repaint();
    if (onRemoved) onRemoved (name);
    if (onChanged) onChanged();
}

void ManageLayoutsPanel::promptRename() { askName ({}); }

void ManageLayoutsPanel::askName (const juce::String& reason)
{
    const int row = selectedRow();
    if (row < 0 || row >= rowCount())
        return;
    auto* alert = new juce::AlertWindow ("Rename layout",
                                         reason.isEmpty() ? juce::String ("New name:") : reason,
                                         juce::MessageBoxIconType::NoIcon, this);
    alert->addTextEditor ("name", juce::String (list[(size_t) row].name));
    alert->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    alert->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<ManageLayoutsPanel> safe (this);
    alert->enterModalState (true, juce::ModalCallbackFunction::create ([safe, alert] (int result)
    {
        if (safe == nullptr || result != 1)
            return;
        const auto text = alert->getTextEditorContents ("name").toStdString();
        if (const auto error = safe->renameSelected (text))
            safe->askName (*error == LayoutNameError::empty ? "The name can't be empty." : "Another layout already has that name.");
    }), true);
}

juce::Component::SafePointer<juce::DialogWindow> showManageLayoutsDialog (SavedLayouts& list,
                                                                          std::function<void()> onChanged,
                                                                          std::function<void (const std::string&, const std::string&)> onRenamed,
                                                                          std::function<void (const std::string&)> onRemoved,
                                                                          juce::Component* centreAround)
{
    auto content = std::make_unique<ManageLayoutsPanel> (list);
    content->onChanged = std::move (onChanged);
    content->onRenamed = std::move (onRenamed);
    content->onRemoved = std::move (onRemoved);
    auto* panel = content.get();
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (content.release());
    options.dialogTitle = "Manage Layouts";
    options.dialogBackgroundColour = juce::Colour (SongsmithColours::background);
    options.componentToCentreAround = centreAround;
    options.useNativeTitleBar = true;
    options.resizable = false;
    auto* window = options.launchAsync();
    if (window == nullptr)
        return {};
    panel->onCloseRequested = [window] { window->exitModalState (0); };
    return window;
}

} // namespace lotro::dock

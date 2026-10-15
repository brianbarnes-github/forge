#include "UI/Dock/ManageLayoutsDialog.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro::dock;

namespace
{
SavedLayouts three()
{
    SavedLayouts list;
    for (int i = 0; i < 3; ++i) addLayout (list, defaultLayout());
    return list;
}
}

TEST_CASE ("ManageLayoutsPanel: Rename and Delete are disabled until a layout is selected", "[dock][layouts]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto list = three();
    ManageLayoutsPanel panel (list);
    panel.setBounds (0, 0, 320, 260);
    CHECK (panel.rowCount() == 3);
    CHECK_FALSE (panel.renameButtonForTesting().isEnabled());
    CHECK_FALSE (panel.deleteButtonForTesting().isEnabled());
    panel.selectRow (1);
    CHECK (panel.renameButtonForTesting().isEnabled());
    CHECK (panel.deleteButtonForTesting().isEnabled());
}

TEST_CASE ("ManageLayoutsPanel: rename changes the selected layout and reports each change", "[dock][layouts]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto list = three();
    ManageLayoutsPanel panel (list);
    int changes = 0;
    panel.onChanged = [&] { ++changes; };

    CHECK (panel.renameSelected ("x") == LayoutNameError::empty);   // nothing selected
    panel.selectRow (1);
    CHECK (panel.renameSelected ("Layout 3") == LayoutNameError::duplicate);
    CHECK (changes == 0);
    CHECK_FALSE (panel.renameSelected ("Mixing"));
    CHECK (list[1].name == "Mixing");
    CHECK (changes == 1);
}

TEST_CASE ("ManageLayoutsPanel: delete removes the selected layout and selects the neighbour", "[dock][layouts]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto list = three();
    ManageLayoutsPanel panel (list);
    int changes = 0;
    panel.onChanged = [&] { ++changes; };

    panel.deleteSelected();   // nothing selected
    CHECK (list.size() == 3);
    CHECK (changes == 0);

    panel.selectRow (1);
    panel.deleteSelected();
    REQUIRE (list.size() == 2);
    CHECK (list[0].name == "Layout 1");
    CHECK (list[1].name == "Layout 3");
    CHECK (panel.rowCount() == 2);
    CHECK (panel.selectedRow() == 1);
    CHECK (changes == 1);

    panel.deleteSelected();
    panel.deleteSelected();
    CHECK (list.empty());
    CHECK (panel.selectedRow() == -1);
    CHECK_FALSE (panel.deleteButtonForTesting().isEnabled());
}

TEST_CASE ("ManageLayoutsPanel: the rename prompt is destroyed once it is dismissed", "[dock][layouts]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto list = three();
    ManageLayoutsPanel panel (list);
    panel.selectRow (0);
    panel.promptRename();

    auto* manager = juce::ModalComponentManager::getInstance();
    REQUIRE (manager->getNumModalComponents() == 1);
    juce::Component::SafePointer<juce::Component> prompt (manager->getModalComponent (0));
    REQUIRE (prompt != nullptr);

    prompt->exitModalState (0);   // Cancel
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);

    CHECK (manager->getNumModalComponents() == 0);
    CHECK (prompt == nullptr);
    CHECK (list[0].name == "Layout 1");
}

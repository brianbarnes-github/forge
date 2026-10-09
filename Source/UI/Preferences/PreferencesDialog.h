#pragma once

#include "../AppSettings.h"
#include "../SplitterComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace lotro
{
    // One entry per tree item. Adding a page = one entry in preferencePages()
    // plus its component.
    struct PreferencesPage
    {
        juce::String name;
        std::function<std::unique_ptr<juce::Component> (AppSettings&)> make;
    };

    const std::vector<PreferencesPage>& preferencePages();

    // File > Preferences... content: page tree | splitter | selected page.
    class PreferencesDialog : public juce::Component
    {
    public:
        explicit PreferencesDialog (AppSettings& settings);
        ~PreferencesDialog() override;

        void paint (juce::Graphics& g) override;
        void resized() override;

        juce::StringArray pageNames() const;
        int getSelectedPageIndex() const noexcept { return selectedIndex; }
        void selectPage (int index);
        juce::Component* currentPage() noexcept { return page.get(); }

        // Fired by the Close button; showPreferencesDialog() closes the window on it.
        std::function<void()> onCloseRequested;

        juce::TextButton& closeButtonForTesting() { return closeButton; }
        int treeLeftEdgeForTesting() const { return tree.getX(); }
        int treeWidthForTesting() const { return tree.getWidth(); }

    private:
        class RootItem;
        class PageItem;

        AppSettings& settings;
        juce::TreeView tree;
        std::unique_ptr<RootItem> root;
        juce::Component pageHost;
        juce::Label pageTitle;
        std::unique_ptr<juce::Component> page;
        SplitterComponent splitter { SplitterComponent::Orientation::leftRight };
        juce::TextButton closeButton { "Close" };
        int selectedIndex = -1;
    };

    // Opens the dialog fully modal (the main window and its menus are blocked
    // until it closes). `settings` must outlive it; MainWindow owns both.
    void showPreferencesDialog (AppSettings& settings, juce::Component* centreAround);
}

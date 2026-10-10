#pragma once

#include "../AppSettings.h"
#include "../SplitterComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace lotro
{
    // The Preferences window cannot be resized below this (set on the window by showPreferencesDialog).
    constexpr int preferencesMinWidth  = 480;
    constexpr int preferencesMinHeight = 300;

    // A page touches AppSettings only in its constructor and in click handlers,
    // never in its destructor: the window can outlive its owner's settings during shutdown.
    // One entry per tree item. Adding a page = one entry in preferencePages()
    // plus its component.
    struct PreferencesPage
    {
        juce::String name;
        std::function<std::unique_ptr<juce::Component> (AppSettings&, std::function<void()>)> make;
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
        SplitterComponent& splitterForTesting() { return splitter; }
        bool saveNoticeVisibleForTesting() const { return saveNotice.isVisible(); }

    private:
        class RootItem;
        class PageItem;

        // The splitter resizes this directly, bypassing the dialog's own
        // resized(), so it reports its resizes back for the title/page layout.
        struct PageHost : juce::Component
        {
            std::function<void()> onResized;
            void resized() override { if (onResized) onResized(); }
        };

        void layoutPage();
        void refreshSaveNotice();

        AppSettings& settings;
        juce::TreeView tree;
        std::unique_ptr<RootItem> root;
        PageHost pageHost;
        juce::Label pageTitle;
        std::unique_ptr<juce::Component> page;
        SplitterComponent splitter { SplitterComponent::Orientation::leftRight };
        juce::TextButton closeButton { "Close" };
        juce::Label saveNotice;
        int selectedIndex = -1;
    };

    // Opens the dialog fully modal (the main window and its menus are blocked
    // until it closes). `settings` must outlive it; MainWindow owns both.
    // The returned pointer goes null when the window closes; the owner closes it
    // if still alive when it is destroyed.
    juce::Component::SafePointer<juce::DialogWindow> showPreferencesDialog (AppSettings& settings, juce::Component* centreAround);
}

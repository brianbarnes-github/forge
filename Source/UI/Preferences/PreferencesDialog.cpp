#include "PreferencesDialog.h"
#include "GeneralPreferencesPage.h"
#include "ImportPreferencesPage.h"
#include "../SongsmithColours.h"

namespace lotro
{
    const std::vector<PreferencesPage>& preferencePages()
    {
        static const std::vector<PreferencesPage> pages
        {
            { "General", [] (AppSettings& s) -> std::unique_ptr<juce::Component> { return std::make_unique<GeneralPreferencesPage> (s); } },
            { "Import",  [] (AppSettings& s) -> std::unique_ptr<juce::Component> { return std::make_unique<ImportPreferencesPage> (s); } },
        };
        return pages;
    }

    class PreferencesDialog::PageItem : public juce::TreeViewItem
    {
    public:
        PageItem (PreferencesDialog& ownerIn, int indexIn) : owner (ownerIn), index (indexIn) {}

        bool mightContainSubItems() override { return false; }
        void paintItem (juce::Graphics& g, int width, int height) override
        {
            if (isSelected())
                g.fillAll (juce::Colour (SongsmithColours::selectedRow));
            g.setColour (juce::Colour (SongsmithColours::text));
            g.setFont (juce::FontOptions (14.0f));
            g.drawText (preferencePages()[(size_t) index].name, 8, 0, width - 8, height, juce::Justification::centredLeft);
        }
        void itemSelectionChanged (bool nowSelected) override
        {
            if (nowSelected)
                owner.selectPage (index);
        }

    private:
        PreferencesDialog& owner;
        int index;
    };

    class PreferencesDialog::RootItem : public juce::TreeViewItem
    {
    public:
        explicit RootItem (PreferencesDialog& owner)
        {
            for (int i = 0; i < (int) preferencePages().size(); ++i)
                addSubItem (new PageItem (owner, i));
        }
        bool mightContainSubItems() override { return true; }
    };

    PreferencesDialog::PreferencesDialog (AppSettings& settingsIn) : settings (settingsIn)
    {
        root = std::make_unique<RootItem> (*this);
        tree.setRootItem (root.get());
        tree.setRootItemVisible (false);
        tree.setDefaultOpenness (true);
        tree.setColour (juce::TreeView::backgroundColourId, juce::Colour (SongsmithColours::background));
        addAndMakeVisible (tree);

        pageTitle.setFont (juce::FontOptions (18.0f));
        pageTitle.setColour (juce::Label::textColourId, juce::Colour (SongsmithColours::text));
        pageHost.onResized = [this] { layoutPage(); };
        pageHost.addAndMakeVisible (pageTitle);
        addAndMakeVisible (pageHost);

        splitter.setComponents (&tree, &pageHost);
        splitter.setFraction (0.25f);
        addAndMakeVisible (splitter);

        closeButton.onClick = [this] { if (onCloseRequested) onCloseRequested(); };
        addAndMakeVisible (closeButton);

        setSize (640, 400);
        selectPage (0);
        if (auto* item = root->getSubItem (0))
            item->setSelected (true, false, juce::dontSendNotification);
    }

    PreferencesDialog::~PreferencesDialog()
    {
        tree.setRootItem (nullptr);   // before root is destroyed
    }

    juce::StringArray PreferencesDialog::pageNames() const
    {
        juce::StringArray names;
        for (const auto& p : preferencePages())
            names.add (p.name);
        return names;
    }

    void PreferencesDialog::selectPage (int index)
    {
        const auto& pages = preferencePages();
        if (index < 0 || index >= (int) pages.size() || index == selectedIndex)
            return;
        selectedIndex = index;
        page = pages[(size_t) index].make (settings);
        pageHost.addAndMakeVisible (*page);
        pageTitle.setText (pages[(size_t) index].name, juce::dontSendNotification);
        layoutPage();
    }

    void PreferencesDialog::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colour (SongsmithColours::background));
    }

    void PreferencesDialog::resized()
    {
        auto area = getLocalBounds();
        closeButton.setBounds (area.removeFromBottom (44).reduced (8).removeFromRight (80));
        splitter.setBounds (area);   // lays out tree (left) and pageHost (right)
        layoutPage();
    }

    void PreferencesDialog::layoutPage()
    {
        auto host = pageHost.getLocalBounds();
        pageTitle.setBounds (host.removeFromTop (36).reduced (12, 4));
        if (page != nullptr)
            page->setBounds (host);
    }

    void showPreferencesDialog (AppSettings& settings, juce::Component* centreAround)
    {
        auto content = std::make_unique<PreferencesDialog> (settings);
        juce::DialogWindow::LaunchOptions options;
        auto* dialog = content.get();
        options.content.setOwned (content.release());
        options.dialogTitle = "Preferences";
        options.dialogBackgroundColour = juce::Colour (SongsmithColours::background);
        options.componentToCentreAround = centreAround;
        options.useNativeTitleBar = true;
        options.resizable = true;
        auto* window = options.launchAsync();   // enters modal state
        // The window owns the content that owns this callback, so it cannot outlive `window`.
        dialog->onCloseRequested = [window] { window->exitModalState (0); };
    }
}

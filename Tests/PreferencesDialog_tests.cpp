#include "UI/Preferences/PreferencesDialog.h"
#include "UI/Preferences/GeneralPreferencesPage.h"

#include <catch2/catch_test_macros.hpp>

using namespace lotro;

namespace
{
    struct Fixture
    {
        juce::File file = juce::File::createTempFile (".settings");
        std::unique_ptr<juce::PropertiesFile> props = std::make_unique<juce::PropertiesFile> (file, juce::PropertiesFile::Options());
        AppSettings settings { *props };
        ~Fixture() { props.reset(); file.deleteFile(); }
    };
}

TEST_CASE ("PreferencesDialog: the tree lists the registered pages, General first and selected", "[preferences]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    PreferencesDialog dialog (f.settings);

    REQUIRE (preferencePages().size() >= 1);
    CHECK (preferencePages().front().name == "General");
    CHECK (dialog.pageNames() == juce::StringArray { "General" });
    CHECK (dialog.getSelectedPageIndex() == 0);
    CHECK (dynamic_cast<GeneralPreferencesPage*> (dialog.currentPage()) != nullptr);
}

TEST_CASE ("PreferencesDialog: tree, splitter and page share the content, tree on the left", "[preferences]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    PreferencesDialog dialog (f.settings);
    dialog.setSize (640, 400);

    auto* page = dialog.currentPage();
    REQUIRE (page != nullptr);
    const auto pageBounds = dialog.getLocalArea (page, page->getLocalBounds());
    CHECK (dialog.getLocalBounds().contains (pageBounds));
    CHECK (dialog.treeLeftEdgeForTesting() < pageBounds.getX());
    CHECK (dialog.treeWidthForTesting() < dialog.getWidth() / 2);   // fraction 0.25
}

TEST_CASE ("PreferencesDialog: dragging the splitter re-lays out the page to the host's new width", "[preferences]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    PreferencesDialog dialog (f.settings);
    dialog.setSize (640, 400);
    auto* page = dialog.currentPage();
    REQUIRE (page != nullptr);

    dialog.splitterForTesting().setFraction (0.6f);
    const int widthWhenTreeIsWide = page->getWidth();
    CHECK (widthWhenTreeIsWide == page->getParentComponent()->getWidth());

    dialog.splitterForTesting().setFraction (0.2f);
    CHECK (page->getWidth() == page->getParentComponent()->getWidth());
    CHECK (page->getWidth() > widthWhenTreeIsWide);   // the page grew with its host
}

TEST_CASE ("General page: toggles show the saved state and write straight through", "[preferences]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    {
        GeneralPreferencesPage page (f.settings);
        CHECK (page.unsavedChangesToggleForTesting().getToggleState());
        CHECK (page.replaceFileToggleForTesting().getToggleState());

        page.unsavedChangesToggleForTesting().setToggleState (false, juce::sendNotification);
        CHECK_FALSE (f.settings.askToSaveUnsavedChanges());
        CHECK (f.settings.askBeforeReplacingFile());

        page.replaceFileToggleForTesting().setToggleState (false, juce::sendNotification);
        CHECK_FALSE (f.settings.askBeforeReplacingFile());
    }
    // A fresh page over the same settings shows what was saved, not the defaults.
    GeneralPreferencesPage again (f.settings);
    CHECK_FALSE (again.unsavedChangesToggleForTesting().getToggleState());
    CHECK_FALSE (again.replaceFileToggleForTesting().getToggleState());
}

TEST_CASE ("PreferencesDialog: the Close button asks to close", "[preferences]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    PreferencesDialog dialog (f.settings);
    bool closed = false;
    dialog.onCloseRequested = [&] { closed = true; };
    dialog.closeButtonForTesting().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);   // triggerClick() is asynchronous
    CHECK (closed);
}

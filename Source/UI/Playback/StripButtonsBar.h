#pragma once

#include "UI/Dock/StripButtons.h"
#include "UI/MenuModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace lotro
{

// The state every strip-buttons bar shares (the main window's strip and the MIDI editor's), plus
// the three things a bar needs from the app. Owned by MainWindow, which outlives the bars.
// changed() tells every bar to rebuild.
class StripButtonsHost : public juce::ChangeBroadcaster
{
public:
    dock::StripButtons buttons;
    std::function<MenuState()> menuState;          // the live state the menus use; must be set
    std::function<void()> persist;                 // after the user adds or removes a button
    std::function<void (int commandId)> run;       // dispatch a MenuCommandId

    void changed() { sendSynchronousChangeMessage(); }
};

// How many leading buttons of the given widths fit in `available`. If they do not all fit, room
// for the overflow ("»") button is reserved and the count is what fits beside it.
int visibleButtonCount (const std::vector<int>& widths, int available, int gap, int overflowWidth);

// "+", then the user's buttons, then "»" when some do not fit. Right-click a button to remove it.
class StripButtonsBar : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit StripButtonsBar (StripButtonsHost&);
    ~StripButtonsBar() override;

    void refresh();    // re-read every button's label / enabled / pressed state and re-lay out
    void resized() override;

    int  visibleButtonsForTesting() const { return (int) buttons.size(); }
    bool hasOverflowForTesting() const { return overflowShown; }
    juce::PopupMenu pickerMenuForTesting() const { return buildPicker(); }
    void clickButtonForTesting (int index);
    void removeButtonForTesting (int index);
    juce::Button& plusButtonForTesting() { return plusButton; }
    juce::TextButton& buttonForTesting (int index) { return *buttons[(std::size_t) index].button; }

private:
    struct Entry
    {
        dock::StripButton data;
        std::unique_ptr<juce::TextButton> button;
    };

    void mouseDown (const juce::MouseEvent&) override;   // right-click on a button: remove menu
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void timerCallback() override { refresh(); }

    void layoutButtons();
    juce::PopupMenu buildPicker() const;
    void showPicker();
    void showOverflow();
    void showRemoveMenu (const dock::StripButton&);
    void activate (const dock::StripButton&);
    void remove (const dock::StripButton&);

    StripButtonsHost& host;
    juce::TextButton plusButton { "+" };
    juce::TextButton overflowButton { juce::String::fromUTF8 ("\xc2\xbb") };
    std::vector<Entry> buttons;                    // the ones shown, in order
    std::vector<dock::StripButton> hiddenOverflow; // the ones behind "»"
    bool overflowShown = false;
};

} // namespace lotro

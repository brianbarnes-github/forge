#include "UI/Playback/StripButtonsBar.h"

#include "UI/SongsmithColours.h"

namespace lotro
{
namespace
{
constexpr int gap = 4;
constexpr int plusWidth = 24;
constexpr int overflowWidth = 24;
constexpr int textPadding = 18;
constexpr int pickerBase = 1;             // menu item ids: pickerBase + index into stripCommands
constexpr int layoutPickerBase = 1000;    // layoutPickerBase + layout index

juce::Font buttonFont() { return juce::Font (juce::FontOptions (13.0f)); }

int widthFor (const juce::String& label)
{
    return juce::GlyphArrangement::getStringWidthInt (buttonFont(), label) + textPadding;
}
}

int visibleButtonCount (const std::vector<int>& widths, int available, int gapIn, int overflowWidthIn)
{
    int total = 0;
    for (std::size_t i = 0; i < widths.size(); ++i)
        total += widths[i] + (i > 0 ? gapIn : 0);
    if (total <= available)
        return (int) widths.size();

    const int room = available - overflowWidthIn - gapIn;
    int used = 0, count = 0;
    for (const int w : widths)
    {
        const int next = used + (count > 0 ? gapIn : 0) + w;
        if (next > room)
            break;
        used = next;
        ++count;
    }
    return count;
}

StripButtonsBar::StripButtonsBar (StripButtonsHost& hostIn) : host (hostIn)
{
    for (auto* b : { &plusButton, &overflowButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    plusButton.setTooltip ("Add a command button");
    overflowButton.setTooltip ("More buttons");
    plusButton.onClick = [this] { showPicker(); };
    overflowButton.onClick = [this] { showOverflow(); };
    host.addChangeListener (this);
    startTimerHz (5);   // enabled/pressed state changes (Undo, a panel opening) have no signal of their own
    refresh();
}

StripButtonsBar::~StripButtonsBar()
{
    host.removeChangeListener (this);
}

void StripButtonsBar::refresh()
{
    const MenuState state = host.menuState ? host.menuState() : MenuState {};
    const auto menus = buildMenus (state);   // once per refresh, not once per button

    struct Shown { dock::StripButton data; dock::ButtonView view; int width; };
    std::vector<Shown> all;
    for (const auto& b : host.buttons)
        if (const auto view = dock::buttonView (b, state, menus))
            all.push_back ({ b, *view, widthFor (juce::String (view->label)) });

    std::vector<int> widths;
    for (const auto& s : all) widths.push_back (s.width);
    const int available = juce::jmax (0, getWidth() - plusWidth - gap);
    const int shown = visibleButtonCount (widths, available, gap, overflowWidth);

    // Rebuild only when the set of shown buttons changed; otherwise just update state in place.
    bool sameSet = (int) buttons.size() == shown;
    for (int i = 0; sameSet && i < shown; ++i)
        sameSet = dock::sameButton (buttons[(std::size_t) i].data, all[(std::size_t) i].data);
    if (! sameSet)
    {
        buttons.clear();
        for (int i = 0; i < shown; ++i)
        {
            Entry e { all[(std::size_t) i].data, std::make_unique<StripBarButton>() };
            e.button->setWantsKeyboardFocus (false);
            e.button->setClickingTogglesState (false);
            const auto data = e.data;
            e.button->onClick = [this, data] { activate (data); };
            e.button->onRightClick = [this, data] { requestRemoveMenu (data); };
            addAndMakeVisible (*e.button);
            buttons.push_back (std::move (e));
        }
    }
    hiddenOverflow.clear();
    for (std::size_t i = (std::size_t) shown; i < all.size(); ++i)
        hiddenOverflow.push_back (all[i].data);
    overflowShown = ! hiddenOverflow.empty();

    for (int i = 0; i < shown; ++i)
    {
        auto& btn = *buttons[(std::size_t) i].button;
        const auto& v = all[(std::size_t) i].view;
        btn.setButtonText (juce::String (v.label));
        btn.setEnabled (v.enabled);
        btn.setToggleState (v.pressed, juce::dontSendNotification);
        btn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (SongsmithColours::accentCopper));
    }
    overflowButton.setVisible (overflowShown);
    layoutButtons();
}

void StripButtonsBar::resized()
{
    refresh();   // the width decides how many buttons fit
}

void StripButtonsBar::layoutButtons()
{
    auto area = getLocalBounds().reduced (0, 3);
    plusButton.setBounds (area.removeFromLeft (plusWidth));
    area.removeFromLeft (gap);
    for (auto& e : buttons)
    {
        e.button->setBounds (area.removeFromLeft (widthFor (e.button->getButtonText())));
        area.removeFromLeft (gap);
    }
    if (overflowShown)
        overflowButton.setBounds (area.removeFromLeft (overflowWidth).withWidth (overflowWidth));
}

juce::PopupMenu StripButtonsBar::buildPicker() const
{
    const bool full = host.buttons.size() >= dock::maxStripButtons;
    juce::PopupMenu root;
    const char* group = nullptr;
    juce::PopupMenu sub;
    auto flush = [&]
    {
        if (group != nullptr)
            root.addSubMenu (group, sub);
        sub = juce::PopupMenu();
    };
    for (std::size_t i = 0; i < stripCommands.size(); ++i)
    {
        const auto& c = stripCommands[i];
        if (group == nullptr || std::string (group) != c.group)
        {
            flush();
            group = c.group;
        }
        const bool present = dock::contains (host.buttons, { dock::StripButton::Kind::command, c.key });
        sub.addItem (pickerBase + (int) i, c.label, ! present && ! full);
    }
    flush();

    juce::PopupMenu layouts;
    const auto state = host.menuState ? host.menuState() : MenuState {};
    for (std::size_t i = 0; i < state.layoutNames.size() && (int) i < maxSavedLayouts; ++i)
    {
        const bool present = dock::contains (host.buttons, { dock::StripButton::Kind::applyLayout, state.layoutNames[i] });
        layouts.addItem (layoutPickerBase + (int) i, juce::String (state.layoutNames[i]), ! present && ! full);
    }
    root.addSubMenu ("Apply Layout", layouts, ! state.layoutNames.empty());
    return root;
}

void StripButtonsBar::showPicker()
{
    juce::Component::SafePointer<StripButtonsBar> safe (this);
    buildPicker().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&plusButton),
        [safe] (int id)
        {
            if (safe != nullptr)
                safe->pickerItemChosen (id);
        });
}

void StripButtonsBar::pickerItemChosen (int id)
{
    if (id == 0)
        return;
    dock::StripButton b;
    if (id >= layoutPickerBase)
    {
        const auto names = (host.menuState ? host.menuState() : MenuState {}).layoutNames;
        const auto index = (std::size_t) (id - layoutPickerBase);
        if (index >= names.size())
            return;
        b = { dock::StripButton::Kind::applyLayout, names[index] };
    }
    else
    {
        const auto index = (std::size_t) (id - pickerBase);
        if (index >= stripCommands.size())
            return;
        b = { dock::StripButton::Kind::command, stripCommands[index].key };
    }
    if (dock::addButton (host.buttons, std::move (b)))
    {
        if (host.persist) host.persist();
        host.changed();
    }
}

void StripButtonsBar::showOverflow()
{
    juce::PopupMenu menu;
    const auto state = host.menuState ? host.menuState() : MenuState {};
    const auto menus = buildMenus (state);
    for (std::size_t i = 0; i < hiddenOverflow.size(); ++i)
        if (const auto view = dock::buttonView (hiddenOverflow[i], state, menus))
            menu.addItem ((int) i + 1, juce::String (view->label), view->enabled, view->pressed);
    juce::Component::SafePointer<StripButtonsBar> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&overflowButton),
        [safe] (int id)
        {
            if (safe != nullptr)
                safe->overflowItemChosen (id);
        });
}

void StripButtonsBar::overflowItemChosen (int id)
{
    if (id > 0 && (std::size_t) id <= hiddenOverflow.size())
        activate (hiddenOverflow[(std::size_t) id - 1]);
}

void StripButtonsBar::requestRemoveMenu (const dock::StripButton& b)
{
    if (removeMenuHookForTesting)
        removeMenuHookForTesting (b);
    else
        showRemoveMenu (b);
}

void StripButtonsBar::showRemoveMenu (const dock::StripButton& b)
{
    juce::PopupMenu menu;
    menu.addItem (1, "Remove from strip");
    juce::Component::SafePointer<StripButtonsBar> safe (this);
    menu.showMenuAsync ({}, [safe, b] (int id)
    {
        if (safe != nullptr && id == 1)
            safe->remove (b);
    });
}

void StripButtonsBar::activate (const dock::StripButton& b)
{
    if (! host.run || ! host.menuState)
        return;
    // host.run may end in changed() -> refresh(), which can destroy the button whose onClick got
    // us here (and this bar's entries); nothing below touches a capture or member afterwards.
    if (const auto id = dock::commandIdFor (b, host.menuState()))
        host.run (*id);
}

void StripButtonsBar::remove (const dock::StripButton& b)
{
    for (std::size_t i = 0; i < host.buttons.size(); ++i)
        if (dock::sameButton (host.buttons[i], b))
        {
            dock::removeButton (host.buttons, i);
            if (host.persist) host.persist();
            host.changed();
            return;
        }
}

void StripButtonsBar::clickButtonForTesting (int index)
{
    activate (buttons[(std::size_t) index].data);
}

void StripButtonsBar::removeButtonForTesting (int index)
{
    remove (buttons[(std::size_t) index].data);
}

} // namespace lotro

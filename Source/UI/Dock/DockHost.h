#pragma once

#include "UI/Dock/DockFloatWindow.h"
#include "UI/Dock/DockLayoutState.h"
#include "UI/Dock/DockPanel.h"
#include "UI/Dock/DockStack.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lotro::dock
{

// Owns the panels and their floating windows and applies a LayoutState to two
// stacks: `topStack` ([upper, panels below the track list, lower]) and
// `bottomStack` (panels in the bottom zone, inside `lower`).
class DockHost
{
public:
    DockHost (DockStack& topStack, DockStack& bottomStack, juce::Component& upper, juce::Component& lower,
              juce::Component* centreFloatsAround);
    ~DockHost();

    // Creates the DockPanel for `def` around `content` (not owned).
    void addPanel (const PanelDef&, juce::Colour titleColour, juce::Component& content);

    void apply (const LayoutState&);
    LayoutState state() const;

    bool isVisible (const std::string& id) const;
    void setVisible (const std::string& id, bool);   // remembers the mode
    DockPanel* panel (const std::string& id);

    // Retitles the panel header and, if floating, its window.
    void setTitle (const std::string& id, const juce::String&);
    // Floating windows with no saved position open centred over this.
    void setCentreFloatsAround (juce::Component* c) noexcept { centreAround = c; }

    // `lower` never shrinks below this (the transport + part strip chrome).
    void setLowerChromeHeight (int);
    // `lower` collapses to its chrome when the bottom zone is empty and nothing else fills it.
    void setLowerHasOtherContent (bool);

    std::function<void (const LayoutState&)> onLayoutChanged;   // every user-driven change

private:
    // A drag of a floating window is a burst of move/resize events; report once it settles.
    struct SettleTimer : juce::Timer
    {
        std::function<void()> onSettled;
        void timerCallback() override { stopTimer(); if (onSettled) onSettled(); }
    };
    static constexpr int boundsSettleMs = 300;

    struct Entry
    {
        PanelDef def;
        std::unique_ptr<DockPanel> panel;
        std::unique_ptr<DockFloatWindow> window;
    };

    Entry* entry (const std::string& id);
    const Entry* entry (const std::string& id) const;
    void captureFloatBounds (LayoutState&) const;
    void rebuild();
    void userChanged();
    void weightsChanged (const std::vector<DockStack::Item>&, bool topLevel);

    DockStack& top;
    DockStack& bottom;
    juce::Component& upperComponent;
    juce::Component& lowerComponent;
    juce::Component* centreAround;
    std::vector<std::unique_ptr<Entry>> entries;
    LayoutState layout;
    SettleTimer boundsTimer;
    int lowerChromeHeight = 138;
    bool lowerHasOtherContent = false;
    bool lowerCollapsed = false;
};

} // namespace lotro::dock

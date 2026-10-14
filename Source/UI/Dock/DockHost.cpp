#include "UI/Dock/DockHost.h"

#include <algorithm>

namespace lotro::dock
{
namespace
{
constexpr int upperMinSize = 120;
constexpr double collapsedWeight = 1e-6;
} // namespace

DockHost::DockHost (DockStack& topStack, DockStack& bottomStack, juce::Component& upper, juce::Component& lower,
                    juce::Component* centreFloatsAround)
    : top (topStack), bottom (bottomStack), upperComponent (upper), lowerComponent (lower),
      centreAround (centreFloatsAround), layout (defaultLayout())
{
    boundsTimer.onSettled = [this]
    {
        if (onLayoutChanged)
            onLayoutChanged (state());
    };
    top.onWeightsChanged    = [this] (const std::vector<DockStack::Item>& items) { weightsChanged (items, true); };
    bottom.onWeightsChanged = [this] (const std::vector<DockStack::Item>& items) { weightsChanged (items, false); };
}

DockHost::~DockHost()
{
    boundsTimer.stopTimer();
    top.onWeightsChanged = nullptr;
    bottom.onWeightsChanged = nullptr;
    for (auto& e : entries)
        e->window.reset();   // detach panels before they are destroyed
}

DockHost::Entry* DockHost::entry (const std::string& id)
{
    for (auto& e : entries)
        if (e->def.id == id)
            return e.get();
    return nullptr;
}

const DockHost::Entry* DockHost::entry (const std::string& id) const
{
    return const_cast<DockHost*> (this)->entry (id);
}

DockPanel* DockHost::panel (const std::string& id)
{
    auto* e = entry (id);
    return e != nullptr ? e->panel.get() : nullptr;
}

void DockHost::setTitle (const std::string& id, const juce::String& title)
{
    if (auto* e = entry (id))
    {
        e->panel->setTitle (title);
        if (e->window != nullptr)
            e->window->setName (title);
    }
}

void DockHost::addPanel (const PanelDef& def, juce::Colour titleColour, juce::Component& content)
{
    auto e = std::make_unique<Entry>();
    e->def = def;
    e->panel = std::make_unique<DockPanel> (def.id, juce::String (def.title), titleColour, content);

    const std::string id = def.id;
    auto& p = *e->panel;
    p.onFloatToggle = [this, id]
    {
        captureFloatBounds (layout);
        const auto* s = find (layout, id);
        if (s == nullptr)
            return;
        if (s->mode == Mode::docked)
            setFloating (layout, id, std::nullopt);
        else
            setDocked (layout, id);
        userChanged();
    };
    p.onClose = [this, id] { hide (layout, id); userChanged(); };
    p.onAlwaysOnTopToggle = [this, id]
    {
        if (const auto* s = find (layout, id))
        {
            captureFloatBounds (layout);
            setAlwaysOnTop (layout, id, ! s->alwaysOnTop);
            userChanged();
        }
    };
    p.onDockTo = [this, id] (Zone zone)
    {
        captureFloatBounds (layout);
        moveToZone (layout, id, zone);
        setDocked (layout, id);
        show (layout, id);
        userChanged();
    };
    entries.push_back (std::move (e));
}

void DockHost::setUpperVisible (bool shouldShow)
{
    if (layout.upperVisible == shouldShow)
        return;
    captureFloatBounds (layout);
    layout.upperVisible = shouldShow;
    userChanged();
}

void DockHost::captureFloatBounds (LayoutState& s) const
{
    for (const auto& e : entries)
        if (e->window != nullptr)
            if (auto* p = find (s, e->def.id))
                p->floatBounds = e->window->currentBounds();
}

LayoutState DockHost::state() const
{
    auto s = layout;
    captureFloatBounds (s);
    return s;
}

bool DockHost::isVisible (const std::string& id) const
{
    const auto* p = find (layout, id);
    return p != nullptr && p->visible;
}

void DockHost::setVisible (const std::string& id, bool shouldShow)
{
    if (find (layout, id) == nullptr)
        return;
    captureFloatBounds (layout);
    if (shouldShow) show (layout, id); else hide (layout, id);
    userChanged();
}

void DockHost::setLowerChromeHeight (int h)
{
    lowerChromeHeight = h;
    rebuild();
}

void DockHost::setLowerHasOtherContent (bool has)
{
    lowerHasOtherContent = has;
    rebuild();
}

void DockHost::apply (const LayoutState& s)
{
    layout = normalise (s, panelDefs());
    rebuild();

    // A window that was already open does not move by itself: put it where the layout says.
    // (Only here: other rebuilds run with bounds that may be staler than the live window.)
    juce::Array<juce::Rectangle<int>> areas;
    for (const auto& d : juce::Desktop::getInstance().getDisplays().displays)
        areas.add (d.userArea);
    const auto* primary = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    for (auto& e : entries)
        if (e->window != nullptr)
            if (const auto* st = find (layout, e->def.id); st != nullptr && st->floatBounds)
                e->window->setBounds (DockFloatWindow::resolveBounds (
                    st->floatBounds, areas, primary != nullptr ? primary->userArea : juce::Rectangle<int> (0, 0, 1280, 720)));
}

void DockHost::userChanged()
{
    captureFloatBounds (layout);   // before rebuild() can destroy a window
    rebuild();
    if (onLayoutChanged)
        onLayoutChanged (state());
}

void DockHost::weightsChanged (const std::vector<DockStack::Item>& items, bool topLevel)
{
    // dragDivider keeps the sum of ALL weights, including the collapsed `lower` placeholder, so
    // the other items' sum would shrink on every drag. Put their sum back to what it was.
    std::vector<DockStack::Item> scaled = items;
    if (topLevel && lowerCollapsed)
    {
        double before = layout.upperWeight, after = 0.0;
        for (const auto& st : layout.panels)
            if (st.visible && st.mode == Mode::docked && st.zone == Zone::belowTracks)
                before += st.weight;
        for (const auto& it : items)
            if (it.id != "lower")
                after += it.weight;
        if (after > 0.0 && before > 0.0)
            for (auto& it : scaled)
                if (it.id != "lower")
                    it.weight *= before / after;
    }

    for (const auto& it : scaled)
    {
        // While collapsed, `lower` carries a placeholder weight that must not be saved; upper and
        // the panels still keep the ratio the user dragged to.
        if (it.id == "upper") layout.upperWeight = it.weight;
        else if (it.id == "lower") { if (! lowerCollapsed) layout.lowerWeight = it.weight; }
        else if (auto* p = find (layout, it.id)) p->weight = it.weight;
    }
    (void) topLevel;
    if (onLayoutChanged)
        onLayoutChanged (state());
}

void DockHost::rebuild()
{
    // Not capturing float bounds here: callers that can destroy a window capture first, and a
    // layout just handed to apply() must keep its own saved bounds.
    // 1. Windows that should not exist any more (docked, hidden): destroy first so
    //    their panels are free to move into a stack.
    for (auto& e : entries)
    {
        const auto* s = find (layout, e->def.id);
        const bool wantWindow = s != nullptr && s->visible && s->mode == Mode::floating;
        if (! wantWindow)
            e->window.reset();
        e->panel->setFloating (s != nullptr && s->mode == Mode::floating);
        e->panel->setAlwaysOnTop (s != nullptr && s->alwaysOnTop);
    }

    // 2. Stacks.
    auto itemsFor = [&] (Zone zone)
    {
        std::vector<const PanelState*> states;
        for (const auto& st : layout.panels)
            if (st.visible && st.mode == Mode::docked && st.zone == zone && entry (st.id) != nullptr)
                states.push_back (&st);
        std::sort (states.begin(), states.end(), [] (const PanelState* a, const PanelState* b) { return a->order < b->order; });
        std::vector<DockStack::Item> items;
        for (const auto* st : states)
            items.push_back ({ st->id, entry (st->id)->panel.get(), st->weight, entry (st->id)->def.minSize });
        return items;
    };

    auto bottomItems = itemsFor (Zone::bottom);
    // With `upper` hidden, `lower` is the only thing left to fill the window, so it never collapses.
    lowerCollapsed = bottomItems.empty() && ! lowerHasOtherContent && layout.upperVisible;

    std::vector<DockStack::Item> topItems;
    if (layout.upperVisible)
        topItems.push_back ({ "upper", &upperComponent, layout.upperWeight, upperMinSize });
    for (auto& it : itemsFor (Zone::belowTracks))
        topItems.push_back (std::move (it));
    topItems.push_back ({ "lower", &lowerComponent, lowerCollapsed ? collapsedWeight : layout.lowerWeight,
                          lowerChromeHeight });

    top.setItems (std::move (topItems));
    bottom.setItems (std::move (bottomItems));
    lowerComponent.resized();   // the lower region lays out differently when its bottom zone is empty

    // 3. Windows that should exist.
    for (auto& e : entries)
    {
        const auto* s = find (layout, e->def.id);
        if (s != nullptr && s->visible && s->mode == Mode::floating && e->window == nullptr)
        {
            const std::string id = e->def.id;
            e->window = std::make_unique<DockFloatWindow> (*e->panel, e->panel->getTitle(),
                                                           s->floatBounds, centreAround);
            e->window->onKeyPressed = [this] (const juce::KeyPress& k) { return onFloatingKey && onFloatingKey (k); };
            e->window->onBoundsChanged = [this] { boundsTimer.startTimer (boundsSettleMs); };
            e->window->onClosed = [this, id] { captureFloatBounds (layout); hide (layout, id); userChanged(); };
        }
        if (e->window != nullptr)
            e->window->setAlwaysOnTop (s != nullptr && s->alwaysOnTop);
    }
}

} // namespace lotro::dock

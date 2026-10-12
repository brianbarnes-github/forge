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
    top.onWeightsChanged    = [this] (const std::vector<DockStack::Item>& items) { weightsChanged (items, true); };
    bottom.onWeightsChanged = [this] (const std::vector<DockStack::Item>& items) { weightsChanged (items, false); };
}

DockHost::~DockHost()
{
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
}

void DockHost::userChanged()
{
    rebuild();
    if (onLayoutChanged)
        onLayoutChanged (state());
}

void DockHost::weightsChanged (const std::vector<DockStack::Item>& items, bool topLevel)
{
    for (const auto& it : items)
    {
        if (it.id == "upper" && ! lowerCollapsed) layout.upperWeight = it.weight;
        else if (it.id == "lower" && ! lowerCollapsed) layout.lowerWeight = it.weight;
        else if (auto* p = find (layout, it.id)) p->weight = it.weight;
    }
    (void) topLevel;
    if (onLayoutChanged)
        onLayoutChanged (state());
}

void DockHost::rebuild()
{
    captureFloatBounds (layout);

    // 1. Windows that should not exist any more (docked, hidden): destroy first so
    //    their panels are free to move into a stack.
    for (auto& e : entries)
    {
        const auto* s = find (layout, e->def.id);
        const bool wantWindow = s != nullptr && s->visible && s->mode == Mode::floating;
        if (! wantWindow)
            e->window.reset();
        e->panel->setFloating (s != nullptr && s->mode == Mode::floating);
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
    lowerCollapsed = bottomItems.empty() && ! lowerHasOtherContent;

    std::vector<DockStack::Item> topItems;
    topItems.push_back ({ "upper", &upperComponent, layout.upperWeight, upperMinSize });
    for (auto& it : itemsFor (Zone::belowTracks))
        topItems.push_back (std::move (it));
    topItems.push_back ({ "lower", &lowerComponent, lowerCollapsed ? collapsedWeight : layout.lowerWeight,
                          lowerChromeHeight });

    top.setItems (std::move (topItems));
    bottom.setItems (std::move (bottomItems));

    // 3. Windows that should exist.
    for (auto& e : entries)
    {
        const auto* s = find (layout, e->def.id);
        if (s != nullptr && s->visible && s->mode == Mode::floating && e->window == nullptr)
        {
            const std::string id = e->def.id;
            e->window = std::make_unique<DockFloatWindow> (*e->panel, e->panel->getTitle(),
                                                           s->floatBounds, centreAround);
            e->window->onClosed = [this, id] { captureFloatBounds (layout); hide (layout, id); userChanged(); };
        }
    }
}

} // namespace lotro::dock

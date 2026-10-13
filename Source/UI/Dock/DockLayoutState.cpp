#include "UI/Dock/DockLayoutState.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>

namespace lotro::dock
{
namespace
{
constexpr double defaultUpperWeight = 0.55;
constexpr double defaultLowerWeight = 0.45;

bool goodWeight (double w) { return std::isfinite (w) && w > 0.0; }

PanelState stateFromDef (const PanelDef& d, int order)
{
    return { d.id, d.defaultMode, d.defaultVisible, d.defaultZone, order, d.defaultWeight, std::nullopt, false };
}

// Renumber `order` 0..n-1 within each zone, keeping the existing relative order.
void renumber (LayoutState& s)
{
    for (const auto zone : { Zone::belowTracks, Zone::bottom })
    {
        std::vector<PanelState*> inZone;
        for (auto& p : s.panels)
            if (p.zone == zone)
                inZone.push_back (&p);
        std::stable_sort (inZone.begin(), inZone.end(),
                          [] (const PanelState* a, const PanelState* b) { return a->order < b->order; });
        int i = 0;
        for (auto* p : inZone)
            p->order = i++;
    }
}

const char* modeName (Mode m) { return m == Mode::docked ? "docked" : "floating"; }
const char* zoneName (Zone z) { return z == Zone::bottom ? "bottom" : "belowTracks"; }

double numberOr (const juce::var& v, double fallback)
{
    return (v.isDouble() || v.isInt() || v.isInt64()) ? static_cast<double> (v) : fallback;
}

// Saved numbers are untrusted: casting an out-of-range double to int is undefined, and a
// huge coordinate could overflow later placement arithmetic.
int intOr (const juce::var& v, int fallback)
{
    if (! (v.isDouble() || v.isInt() || v.isInt64()))
        return fallback;
    const double d = static_cast<double> (v);
    if (! std::isfinite (d))
        return fallback;
    return static_cast<int> (std::clamp (d, -1.0e6, 1.0e6));
}

std::optional<PanelState> panelFromVar (const juce::var& v)
{
    if (! v.isObject())
        return std::nullopt;
    const auto id = v.getProperty ("id", {});
    if (! id.isString() || id.toString().isEmpty())
        return std::nullopt;

    // A key that is absent keeps that panel's registry default (an entry holding only an id is valid).
    const auto name = id.toString().toStdString();
    const auto defIt = std::find_if (panelDefs().begin(), panelDefs().end(), [&] (const PanelDef& d) { return d.id == name; });
    PanelState p { name, Mode::docked, false, Zone::bottom, 0, 1.0, std::nullopt, false };
    if (defIt != panelDefs().end())
        p = stateFromDef (*defIt, 0);

    if (v.hasProperty ("mode"))
        p.mode = v.getProperty ("mode", {}).toString() == "floating" ? Mode::floating : Mode::docked;
    if (v.hasProperty ("zone"))
        p.zone = v.getProperty ("zone", {}).toString() == "belowTracks" ? Zone::belowTracks : Zone::bottom;
    const auto vis = v.getProperty ("visible", {});
    if (vis.isBool())
        p.visible = static_cast<bool> (vis);
    p.order = intOr (v.getProperty ("order", {}), p.order);
    if (v.hasProperty ("weight"))
        p.weight = numberOr (v.getProperty ("weight", {}), 0.0);

    const auto onTop = v.getProperty ("alwaysOnTop", {});
    if (onTop.isBool())
        p.alwaysOnTop = static_cast<bool> (onTop);

    const auto fb = v.getProperty ("floatBounds", {});
    if (fb.isObject())
        p.floatBounds = FloatBounds { intOr (fb.getProperty ("x", {}), 0), intOr (fb.getProperty ("y", {}), 0),
                                      intOr (fb.getProperty ("width", {}), 0), intOr (fb.getProperty ("height", {}), 0) };
    return p;
}
} // namespace

const std::vector<PanelDef>& panelDefs()
{
    static const std::vector<PanelDef> defs {
        { "lotroPreview", "LOTRO Preview", Zone::bottom,      Mode::docked,   true,  1.0,  120, 0 },
        { "midiEditor",   "MIDI Editor",   Zone::belowTracks, Mode::floating, false, 0.35, 160, 0 },
        { "parts",        "Parts",         Zone::bottom,      Mode::docked,   true,  0.6,  70,  -1 },
        { "mixer",        "Mixer",         Zone::bottom,      Mode::docked,   false, 0.6,  150, 1 },
    };
    return defs;
}

LayoutState defaultLayout()
{
    return normalise ({}, panelDefs());
}

LayoutState normalise (LayoutState s, const std::vector<PanelDef>& defs)
{
    if (! goodWeight (s.upperWeight)) s.upperWeight = defaultUpperWeight;
    if (! goodWeight (s.lowerWeight)) s.lowerWeight = defaultLowerWeight;

    std::vector<PanelState> kept;
    for (auto& p : s.panels)
    {
        const bool known = std::any_of (defs.begin(), defs.end(), [&] (const PanelDef& d) { return d.id == p.id; });
        const bool dup   = std::any_of (kept.begin(), kept.end(), [&] (const PanelState& k) { return k.id == p.id; });
        if (! known || dup)
            continue;
        const auto& def = *std::find_if (defs.begin(), defs.end(), [&] (const PanelDef& d) { return d.id == p.id; });
        if (! goodWeight (p.weight))
            p.weight = def.defaultWeight;
        if (p.floatBounds && (p.floatBounds->width < 100 || p.floatBounds->height < 60))
            p.floatBounds.reset();
        kept.push_back (std::move (p));
    }
    for (const auto& d : defs)
        if (std::none_of (kept.begin(), kept.end(), [&] (const PanelState& k) { return k.id == d.id; }))
            kept.push_back (stateFromDef (d, d.defaultOrder));

    s.panels = std::move (kept);
    renumber (s);
    return s;
}

PanelState* find (LayoutState& s, const std::string& id)
{
    for (auto& p : s.panels)
        if (p.id == id)
            return &p;
    return nullptr;
}

const PanelState* find (const LayoutState& s, const std::string& id)
{
    for (const auto& p : s.panels)
        if (p.id == id)
            return &p;
    return nullptr;
}

void show (LayoutState& s, const std::string& id)  { if (auto* p = find (s, id)) p->visible = true; }
void hide (LayoutState& s, const std::string& id)  { if (auto* p = find (s, id)) p->visible = false; }

void setFloating (LayoutState& s, const std::string& id, std::optional<FloatBounds> b)
{
    if (auto* p = find (s, id))
    {
        p->mode = Mode::floating;
        if (b)
            p->floatBounds = b;
    }
}

void setDocked (LayoutState& s, const std::string& id)
{
    if (auto* p = find (s, id))
        p->mode = Mode::docked;
}

void setAlwaysOnTop (LayoutState& s, const std::string& id, bool on)
{
    if (auto* p = find (s, id))
        p->alwaysOnTop = on;
}

void moveToZone (LayoutState& s, const std::string& id, Zone zone)
{
    auto* p = find (s, id);
    if (p == nullptr)
        return;
    int last = -1;
    for (const auto& q : s.panels)
        if (&q != p && q.zone == zone)
            last = std::max (last, q.order);
    p->zone  = zone;
    p->order = last + 1;
    renumber (s);
}

std::string toJson (const LayoutState& s)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("upperWeight", s.upperWeight);
    root->setProperty ("lowerWeight", s.lowerWeight);

    juce::Array<juce::var> panels;
    for (const auto& p : s.panels)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", juce::String (p.id));
        o->setProperty ("mode", modeName (p.mode));
        o->setProperty ("visible", p.visible);
        o->setProperty ("zone", zoneName (p.zone));
        o->setProperty ("order", p.order);
        o->setProperty ("weight", p.weight);
        o->setProperty ("alwaysOnTop", p.alwaysOnTop);
        if (p.floatBounds)
        {
            auto* b = new juce::DynamicObject();
            b->setProperty ("x", p.floatBounds->x);
            b->setProperty ("y", p.floatBounds->y);
            b->setProperty ("width", p.floatBounds->width);
            b->setProperty ("height", p.floatBounds->height);
            o->setProperty ("floatBounds", juce::var (b));
        }
        panels.add (juce::var (o));
    }
    root->setProperty ("panels", panels);
    return juce::JSON::toString (juce::var (root), true).toStdString();
}

LayoutState fromJson (const std::string& text)
{
    const auto parsed = juce::JSON::parse (juce::String::fromUTF8 (text.data(), static_cast<int> (text.size())));
    if (! parsed.isObject())
        return defaultLayout();

    LayoutState s;
    s.upperWeight = numberOr (parsed.getProperty ("upperWeight", {}), 0.0);
    s.lowerWeight = numberOr (parsed.getProperty ("lowerWeight", {}), 0.0);
    if (const auto* arr = parsed.getProperty ("panels", {}).getArray())
        for (const auto& v : *arr)
            if (auto p = panelFromVar (v))
                s.panels.push_back (std::move (*p));
    return normalise (std::move (s), panelDefs());
}

} // namespace lotro::dock

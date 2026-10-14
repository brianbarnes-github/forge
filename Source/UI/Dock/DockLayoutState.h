#pragma once

#include <optional>
#include <string>
#include <vector>

namespace lotro::dock
{

enum class Zone { belowTracks, bottom };
enum class Mode { docked, floating };

struct PanelDef
{
    std::string id;
    std::string title;
    Zone        defaultZone;
    Mode        defaultMode;
    bool        defaultVisible;
    double      defaultWeight;   // > 0
    int         minSize;         // px, >= 40
    int         defaultOrder;    // within its zone; a panel added to an older saved layout lands by this
};

struct FloatBounds { int x = 0, y = 0, width = 0, height = 0; };

struct PanelState
{
    std::string id;
    Mode   mode;
    bool   visible;
    Zone   zone;
    int    order;      // among panels of the same zone, ascending
    double weight;     // > 0, share within its stack
    std::optional<FloatBounds> floatBounds;
    bool   alwaysOnTop = false;   // while floating: the window stays above the main window
};

struct LayoutState
{
    double upperWeight = 0.55;
    double lowerWeight = 0.45;
    bool   upperVisible = true;       // the MIDI source region (header + track list); not a panel
    std::vector<PanelState> panels;   // one per registered def, any order
};

const std::vector<PanelDef>& panelDefs();
LayoutState defaultLayout();
LayoutState normalise (LayoutState, const std::vector<PanelDef>&);
PanelState*       find (LayoutState&, const std::string& id);
const PanelState* find (const LayoutState&, const std::string& id);

void show        (LayoutState&, const std::string& id);
void hide        (LayoutState&, const std::string& id);
void setFloating (LayoutState&, const std::string& id, std::optional<FloatBounds>);
void setDocked   (LayoutState&, const std::string& id);
void moveToZone  (LayoutState&, const std::string& id, Zone);
void setAlwaysOnTop (LayoutState&, const std::string& id, bool);

std::string toJson (const LayoutState&);
LayoutState fromJson (const std::string&);   // never throws; corrupt/empty -> defaultLayout()

} // namespace lotro::dock

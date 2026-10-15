#pragma once

#include <cstddef>
#include <string>
#include <vector>

// The user's chosen buttons for the transport strip's left area: pure data, no UI.
// Persisted as JSON in AppSettings (key strip.buttons). A command button stores a stable
// string key (see stripCommands in MenuModel.h); a layout button stores the layout's name.
namespace lotro::dock
{

struct StripButton
{
    enum class Kind { command, applyLayout };
    Kind kind = Kind::command;
    std::string key;
};

using StripButtons = std::vector<StripButton>;

inline constexpr std::size_t maxStripButtons = 10;

// Same kind and key. Layout names compare case-insensitively; command keys exactly.
bool sameButton (const StripButton&, const StripButton&);
bool contains (const StripButtons&, const StripButton&);

// False (nothing changed) if the button is already there, has an empty key, or the strip is full.
bool addButton (StripButtons&, StripButton);
void removeButton (StripButtons&, std::size_t index);

// The follow-the-layout rules: only applyLayout buttons are touched.
void renameLayoutButton (StripButtons&, const std::string& oldName, const std::string& newName);
void removeLayoutButton (StripButtons&, const std::string& name);

std::string toJson (const StripButtons&);
// Never throws. Empty/corrupt -> {}. Unknown kinds, empty keys and duplicates are dropped; an
// unknown command key is kept (a newer build's setting must survive a round-trip); capped at maxStripButtons.
StripButtons stripButtonsFromJson (const std::string&);

} // namespace lotro::dock

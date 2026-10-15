#pragma once

#include "UI/Dock/DockLayoutState.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

// The user's named dock layouts (View > Layouts): pure data, no UI. Persisted as JSON in AppSettings.
namespace lotro::dock
{

struct NamedLayout
{
    std::string name;
    LayoutState layout;
};

using SavedLayouts = std::vector<NamedLayout>;

enum class LayoutNameError { empty, duplicate };

// "Layout N" for the lowest N >= 1 not already used (case-insensitively).
std::string nextLayoutName (const SavedLayouts&);

// Appends the layout under nextLayoutName and returns that name.
std::string addLayout (SavedLayouts&, LayoutState);

// Trims the name; refuses an empty one or one another layout already has (case-insensitively).
// Keeping a layout's own name, or changing only its case, is fine.
std::optional<LayoutNameError> renameLayout (SavedLayouts&, std::size_t index, const std::string& newName);

void removeLayout (SavedLayouts&, std::size_t index);

std::string toJson (const SavedLayouts&);
SavedLayouts savedLayoutsFromJson (const std::string&);   // never throws; empty/corrupt -> {}; nameless entries dropped

} // namespace lotro::dock

#include "UI/Dock/SavedLayouts.h"

#include <juce_core/juce_core.h>

#include <algorithm>

namespace lotro::dock
{
namespace
{
std::string trimmed (const std::string& s)
{
    return juce::String (s).trim().toStdString();
}

bool sameName (const std::string& a, const std::string& b)
{
    return juce::String (a).equalsIgnoreCase (juce::String (b));
}

bool nameInUse (const SavedLayouts& list, const std::string& name, std::size_t except)
{
    for (std::size_t i = 0; i < list.size(); ++i)
        if (i != except && sameName (list[i].name, name))
            return true;
    return false;
}
}

std::string nextLayoutName (const SavedLayouts& list)
{
    for (int n = 1;; ++n)
    {
        const auto name = "Layout " + std::to_string (n);
        if (! nameInUse (list, name, list.size()))
            return name;
    }
}

std::string addLayout (SavedLayouts& list, LayoutState layout)
{
    auto name = nextLayoutName (list);
    list.push_back ({ name, std::move (layout) });
    return name;
}

std::optional<LayoutNameError> renameLayout (SavedLayouts& list, std::size_t index, const std::string& newName)
{
    const auto name = trimmed (newName);
    if (name.empty() || index >= list.size())
        return LayoutNameError::empty;
    if (nameInUse (list, name, index))
        return LayoutNameError::duplicate;
    list[index].name = name;
    return std::nullopt;
}

void removeLayout (SavedLayouts& list, std::size_t index)
{
    if (index < list.size())
        list.erase (list.begin() + (std::ptrdiff_t) index);
}

std::string toJson (const SavedLayouts& list)
{
    juce::Array<juce::var> items;
    for (const auto& n : list)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", juce::String (n.name));
        o->setProperty ("layout", juce::JSON::parse (juce::String (toJson (n.layout))));
        items.add (juce::var (o));
    }
    return juce::JSON::toString (juce::var (items), true).toStdString();
}

SavedLayouts savedLayoutsFromJson (const std::string& text)
{
    SavedLayouts out;
    const auto parsed = juce::JSON::parse (juce::String::fromUTF8 (text.data(), static_cast<int> (text.size())));
    const auto* arr = parsed.getArray();
    if (arr == nullptr)
        return out;
    for (const auto& v : *arr)
    {
        if (! v.isObject())
            continue;
        const auto name = trimmed (v.getProperty ("name", {}).toString().toStdString());
        if (name.empty() || nameInUse (out, name, out.size()))
            continue;
        out.push_back ({ name, fromJson (juce::JSON::toString (v.getProperty ("layout", {})).toStdString()) });
    }
    return out;
}

} // namespace lotro::dock

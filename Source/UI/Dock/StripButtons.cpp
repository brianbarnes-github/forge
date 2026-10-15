#include "UI/Dock/StripButtons.h"

#include <juce_core/juce_core.h>

#include <algorithm>

namespace lotro::dock
{
namespace
{
bool sameName (const std::string& a, const std::string& b)
{
    return juce::String (a).equalsIgnoreCase (juce::String (b));
}

const char* kindName (StripButton::Kind k)
{
    return k == StripButton::Kind::command ? "command" : "applyLayout";
}
}

bool sameButton (const StripButton& a, const StripButton& b)
{
    if (a.kind != b.kind)
        return false;
    return a.kind == StripButton::Kind::applyLayout ? sameName (a.key, b.key) : a.key == b.key;
}

bool contains (const StripButtons& list, const StripButton& button)
{
    return std::any_of (list.begin(), list.end(), [&] (const StripButton& b) { return sameButton (b, button); });
}

bool addButton (StripButtons& list, StripButton button)
{
    if (button.key.empty() || list.size() >= maxStripButtons || contains (list, button))
        return false;
    list.push_back (std::move (button));
    return true;
}

void removeButton (StripButtons& list, std::size_t index)
{
    if (index < list.size())
        list.erase (list.begin() + static_cast<std::ptrdiff_t> (index));
}

void renameLayoutButton (StripButtons& list, const std::string& oldName, const std::string& newName)
{
    for (auto& b : list)
        if (b.kind == StripButton::Kind::applyLayout && sameName (b.key, oldName))
            b.key = newName;
}

void removeLayoutButton (StripButtons& list, const std::string& name)
{
    list.erase (std::remove_if (list.begin(), list.end(), [&] (const StripButton& b)
                    { return b.kind == StripButton::Kind::applyLayout && sameName (b.key, name); }),
                list.end());
}

std::string toJson (const StripButtons& list)
{
    juce::Array<juce::var> items;
    for (const auto& b : list)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("kind", juce::String (kindName (b.kind)));
        o->setProperty ("key", juce::String (b.key));
        items.add (juce::var (o));
    }
    return juce::JSON::toString (juce::var (items), true).toStdString();
}

StripButtons stripButtonsFromJson (const std::string& text)
{
    StripButtons out;
    const auto parsed = juce::JSON::parse (juce::String::fromUTF8 (text.data(), static_cast<int> (text.size())));
    const auto* arr = parsed.getArray();
    if (arr == nullptr)
        return out;
    for (const auto& v : *arr)
    {
        if (! v.isObject())
            continue;
        const auto kind = v.getProperty ("kind", {}).toString();
        StripButton b;
        if (kind == "command")
            b.kind = StripButton::Kind::command;
        else if (kind == "applyLayout")
            b.kind = StripButton::Kind::applyLayout;
        else
            continue;
        b.key = v.getProperty ("key", {}).toString().trim().toStdString();
        addButton (out, std::move (b));   // drops empty keys and duplicates, stops at the cap
    }
    return out;
}

} // namespace lotro::dock

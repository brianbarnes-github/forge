#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace lotro
{

// Maps General-MIDI drum pitches to LOTRO ABC drum-slot letters. A default
// instance is populated from spec §2.6 plus the extended GM percussion
// (52-59) we fold onto closest-kin slots. Users can override entries from
// a JSON file (see Source/Cli/DrumMapLoader) or programmatically via set().
class DrumMap
{
public:
    DrumMap() = default;

    // Insert or replace the mapping for a single GM drum pitch.
    void set (int gmNote, std::string_view abc);

    // Remove all mappings. Useful when you want to start from a JSON file
    // without inheriting the built-in defaults.
    void clear();

    // Returns the ABC token for the given GM drum pitch, or nullopt if the
    // pitch is unmapped. The returned view is valid for the lifetime of
    // this DrumMap.
    std::optional<std::string_view> lookup (int gmNote) const;

    std::size_t size() const noexcept { return entries.size(); }
    bool        empty() const noexcept { return entries.empty(); }

private:
    std::unordered_map<int, std::string> entries;
};

// Factory — a DrumMap pre-populated with spec §2.6 + the extended GM
// percussion entries (52-59 folded onto closest-kin LOTRO slots).
DrumMap defaultDrumMap();

// Legacy free function: looks up in a process-wide default map. New code
// should prefer DrumMap::lookup() on a per-song instance. Kept for
// backward-compatibility with call sites that don't have a DrumMap in
// scope.
std::optional<std::string_view> mapDrumPitch (int generalMidiNote) noexcept;

} // namespace lotro

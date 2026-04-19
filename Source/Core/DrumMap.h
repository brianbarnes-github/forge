#pragma once

#include <optional>
#include <string_view>

namespace lotro
{

std::optional<std::string_view> mapDrumPitch (int generalMidiNote) noexcept;

} // namespace lotro

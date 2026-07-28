#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace orphen::ported::resource
{

  // Native counterpart of src/FUN_002f3118.c (0x002f3118): decodes the game's headerless LZ stream format.
  std::vector<std::uint8_t> decodeHeaderlessLzStream(std::span<const std::uint8_t> source);

} // namespace orphen::ported::resource

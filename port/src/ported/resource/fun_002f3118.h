#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace orphen::ported::resource
{

  // Counterpart for src/FUN_002f3118.c's headerless LZ stream decoder.
  std::vector<std::uint8_t> FUN_002f3118(std::span<const std::uint8_t> source);

} // namespace orphen::ported::resource
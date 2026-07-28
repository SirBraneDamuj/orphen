#pragma once

#include "ported/resource/mcb_runtime.h"

#include <cstdint>
#include <span>
#include <vector>

namespace orphen::ported::resource
{

  // Counterpart for src/FUN_00222898.c's selected MCB1 scene-bundle read.
  std::vector<std::uint8_t> FUN_00222898(const McbTable &table,
                                         std::span<const std::uint8_t> mcb1Bytes,
                                         std::uint16_t section,
                                         std::uint16_t entry);

} // namespace orphen::ported::resource
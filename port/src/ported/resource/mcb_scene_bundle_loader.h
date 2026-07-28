#pragma once

#include "ported/resource/mcb_runtime.h"

#include <cstdint>
#include <span>
#include <vector>

namespace orphen::ported::resource
{

  // Native counterpart of src/FUN_00222898.c (0x00222898): reads a selected MCB1 scene bundle.
  std::vector<std::uint8_t> loadMcb1SceneBundle(const McbTable &table,
                                                std::span<const std::uint8_t> mcb1Bytes,
                                                std::uint16_t section,
                                                std::uint16_t entry);

} // namespace orphen::ported::resource

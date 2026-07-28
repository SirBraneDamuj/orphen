#pragma once

#include "ported/psm2/psm2_runtime.h"

#include <cstdint>
#include <span>

namespace orphen::ported::psm2
{

  // Counterpart for src/FUN_0022b5a8.c, starting from an already decoded PSM2 payload.
  Psm2RuntimeState FUN_0022b5a8(std::span<const std::uint8_t> decodedPsm2);

} // namespace orphen::ported::psm2
#pragma once

#include "ported/resource/mcb_runtime.h"

#include <cstdint>
#include <span>

namespace orphen::ported::resource
{

  // Counterpart for src/FUN_00222638.c's MCB0 table materialization.
  McbTable FUN_00222638(std::span<const std::uint8_t> mcb0Bytes);

} // namespace orphen::ported::resource
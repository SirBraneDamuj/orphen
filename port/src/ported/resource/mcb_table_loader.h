#pragma once

#include "ported/resource/mcb_runtime.h"

#include <cstdint>
#include <span>

namespace orphen::ported::resource
{

  // Native counterpart of src/FUN_00222638.c (0x00222638): materializes the MCB0 scene table.
  McbTable loadMcb0Table(std::span<const std::uint8_t> mcb0Bytes);

} // namespace orphen::ported::resource

#pragma once

#include "ported/resource/mcb_runtime.h"

#include <cstdint>
#include <optional>
#include <span>

namespace orphen::ported::resource
{

  // Counterpart for src/FUN_00222c08.c's packed-id scan inside the loaded MCB bundle.
  std::optional<McbBundleRecord> FUN_00222c08(std::span<const std::uint8_t> bundle, std::uint32_t packedId);

} // namespace orphen::ported::resource
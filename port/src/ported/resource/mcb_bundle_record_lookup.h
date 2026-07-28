#pragma once

#include "ported/resource/mcb_runtime.h"

#include <cstdint>
#include <optional>
#include <span>

namespace orphen::ported::resource
{

  // Native counterpart of src/FUN_00222c08.c (0x00222c08): scans a loaded MCB bundle for a packed record id.
  std::optional<McbBundleRecord> findMcbBundleRecord(std::span<const std::uint8_t> bundle, std::uint32_t packedId);

} // namespace orphen::ported::resource

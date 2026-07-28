#include "ported/resource/fun_00222638.h"

#include <stdexcept>

namespace orphen::ported::resource
{

  McbTable FUN_00222638(std::span<const std::uint8_t> mcb0Bytes)
  {
    constexpr std::size_t kExpectedMcb0Size = McbTable::kSectionCount * McbTable::kEntryCount * 8;
    if (mcb0Bytes.size() != kExpectedMcb0Size)
    {
      throw std::runtime_error("MCB0.BIN must be exactly 12000 bytes");
    }

    McbTable table;
    for (std::size_t entryIndex = 0; entryIndex < table.entries.size(); ++entryIndex)
    {
      const std::size_t byteOffset = entryIndex * 8;
      table.entries[entryIndex] = {readLeU32(mcb0Bytes, byteOffset), readLeU32(mcb0Bytes, byteOffset + 4)};
    }

    return table;
  }

} // namespace orphen::ported::resource

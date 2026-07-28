#include "ported/resource/mcb_scene_bundle_loader.h"

#include <stdexcept>

namespace orphen::ported::resource
{

  std::vector<std::uint8_t> loadMcb1SceneBundle(const McbTable &table,
                                                std::span<const std::uint8_t> mcb1Bytes,
                                                std::uint16_t section,
                                                std::uint16_t entry)
  {
    const McbTableEntry &tableEntry = table.entryAt(section, entry);
    if (!tableEntry.populated())
    {
      throw std::runtime_error("selected MCB scene slot is empty");
    }
    if (tableEntry.byteOffset > mcb1Bytes.size() || tableEntry.byteSize > mcb1Bytes.size() - tableEntry.byteOffset)
    {
      throw std::runtime_error("selected MCB1 range extends past MCB1.BIN");
    }

    const auto begin = mcb1Bytes.begin() + tableEntry.byteOffset;
    return std::vector<std::uint8_t>(begin, begin + tableEntry.byteSize);
  }

} // namespace orphen::ported::resource

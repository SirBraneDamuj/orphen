#include "ported/resource/mcb_runtime.h"

#include <stdexcept>
#include <string>

namespace orphen::ported::resource
{

  const McbTableEntry &McbTable::entryAt(std::uint16_t section, std::uint16_t entry) const
  {
    if (section >= kSectionCount || entry >= kEntryCount)
    {
      throw std::out_of_range("MCB table selection outside 15x100 range");
    }

    return entries[static_cast<std::size_t>(section) * kEntryCount + entry];
  }

  std::uint32_t readLeU32(std::span<const std::uint8_t> bytes, std::size_t offset)
  {
    if (offset > bytes.size() || sizeof(std::uint32_t) > bytes.size() - offset)
    {
      throw std::runtime_error("u32 read outside resource buffer");
    }

    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
  }

  std::optional<McbBundleRecord> readMcbBundleRecordAt(std::span<const std::uint8_t> bundle, std::size_t offset)
  {
    if (offset + 8 > bundle.size())
    {
      return std::nullopt;
    }

    const std::uint32_t packedId = readLeU32(bundle, offset);
    const std::uint32_t payloadSize = readLeU32(bundle, offset + 4);
    if (packedId == 0xffffffff)
    {
      return std::nullopt;
    }
    if (payloadSize > bundle.size() - offset - 8)
    {
      throw std::runtime_error("MCB bundle record payload extends past bundle end");
    }

    return McbBundleRecord{packedId,
                           static_cast<std::uint16_t>((packedId >> 16) & 0xffff),
                           static_cast<std::uint16_t>(packedId & 0xffff),
                           offset,
                           bundle.subspan(offset + 8, payloadSize)};
  }

} // namespace orphen::ported::resource
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace orphen::ported::resource
{

  struct McbTableEntry
  {
    std::uint32_t byteOffset = 0;
    std::uint32_t byteSize = 0;

    bool populated() const { return byteSize != 0; }
  };

  struct McbTable
  {
    static constexpr std::size_t kSectionCount = 15;
    static constexpr std::size_t kEntryCount = 100;

    std::array<McbTableEntry, kSectionCount * kEntryCount> entries{};

    const McbTableEntry &entryAt(std::uint16_t section, std::uint16_t entry) const;
  };

  struct McbBundleRecord
  {
    std::uint32_t packedId = 0;
    std::uint16_t category = 0;
    std::uint16_t resourceId = 0;
    std::size_t offset = 0;
    std::span<const std::uint8_t> payload;
  };

  std::uint32_t readLeU32(std::span<const std::uint8_t> bytes, std::size_t offset);
  std::optional<McbBundleRecord> readMcbBundleRecordAt(std::span<const std::uint8_t> bundle, std::size_t offset);

} // namespace orphen::ported::resource
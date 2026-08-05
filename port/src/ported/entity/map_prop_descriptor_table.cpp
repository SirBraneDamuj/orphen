#include "ported/entity/map_prop_descriptor_table.h"

#include <cstring>

namespace orphen::ported::entity
{
  namespace
  {
    constexpr std::size_t kSourceRecordStride = 0x28;
    // FUN_00228e28:184 writes this into every record it builds.
    constexpr std::uint8_t kDerivedFlags = 0x41;
    // The bank array FUN_00228e28 zeroes before filling is 0x100 bytes of
    // 8-byte entries, so it cannot describe more than this many banks.
    constexpr std::size_t kMaxBanks = 0x100 / 8;

    bool readU32(std::span<const std::uint8_t> data, std::size_t offset, std::uint32_t &out)
    {
      if (offset + 4 > data.size())
      {
        return false;
      }
      std::memcpy(&out, data.data() + offset, sizeof(out));
      return true;
    }

    bool readU16(std::span<const std::uint8_t> data, std::size_t offset, std::uint16_t &out)
    {
      if (offset + 2 > data.size())
      {
        return false;
      }
      std::memcpy(&out, data.data() + offset, sizeof(out));
      return true;
    }
  } // namespace

  void MapPropDescriptorTable::reset()
  {
    banks_.clear();
  }

  bool MapPropDescriptorTable::FUN_00228e28_build(std::span<const std::uint8_t> descriptorBlob)
  {
    reset();

    // blob+0x20 is stored as a blob-relative offset and relocated to an absolute
    // pointer at load (FUN_00228e28:154). The port keeps the blob as bytes, so
    // it stays an offset.
    std::uint32_t listOffset = 0;
    if (!readU32(descriptorBlob, 0x20, listOffset) || listOffset == 0)
    {
      return false;
    }

    std::size_t cursor = listOffset;
    while (banks_.size() < kMaxBanks)
    {
      std::uint32_t entryOffset = 0;
      std::uint32_t recordCount = 0;
      if (!readU32(descriptorBlob, cursor, entryOffset) ||
          !readU32(descriptorBlob, cursor + 4, recordCount))
      {
        break;
      }
      cursor += 8;
      // A zero offset terminates the list.
      if (entryOffset == 0)
      {
        break;
      }

      // Double indirection: the list entry points at a word, and that word is
      // the offset of the bank's descriptor array.
      std::uint32_t sourceOffset = 0;
      if (!readU32(descriptorBlob, entryOffset, sourceOffset))
      {
        break;
      }

      std::vector<EntityModelRecord> records;
      records.reserve(recordCount);
      for (std::uint32_t index = 0; index < recordCount; ++index)
      {
        const std::size_t source = sourceOffset + static_cast<std::size_t>(index) * kSourceRecordStride;
        EntityModelRecord record;
        if (!readU16(descriptorBlob, source + 2, record.meshId0x00) ||
            !readU16(descriptorBlob, source + 4, record.texId0x02))
        {
          break;
        }
        record.flags0x04 = kDerivedFlags;
        // The rest of the 0x2C record is zeroed by FUN_00228e28:181 and filled
        // in later by the loader, so nothing here binds a texture statically.
        records.push_back(record);
      }

      banks_.push_back(std::move(records));
    }

    return !banks_.empty();
  }

  std::size_t MapPropDescriptorTable::recordCount(std::size_t bank) const
  {
    return bank < banks_.size() ? banks_[bank].size() : 0;
  }

  bool MapPropDescriptorTable::isMapPropType(std::uint32_t typeId)
  {
    return (typeId - kMapPropRangeBase) < kMapPropRangeSize ||
           (typeId - kMapPropRangeSecond) < kMapPropRangeSize ||
           (typeId - kMapPropRangeThird) < kMapPropRangeSize;
  }

  std::optional<EntityModelRecord> MapPropDescriptorTable::FUN_00229980_resolve(std::uint32_t typeId,
                                                                                int stageBank) const
  {
    // The original tests the ranges in this order and falls through to the
    // regular descriptor table when none matches.
    std::uint32_t index = 0;
    int bank = 0;
    if ((typeId - kMapPropRangeThird) < kMapPropRangeSize)
    {
      index = typeId - kMapPropRangeThird;
      bank = kMapPropThirdBank;
    }
    else if ((typeId - kMapPropRangeSecond) < kMapPropRangeSize)
    {
      index = typeId - kMapPropRangeSecond;
      bank = kMapPropSecondBank;
    }
    else if ((typeId - kMapPropRangeBase) < kMapPropRangeSize)
    {
      index = typeId - kMapPropRangeBase;
      bank = stageBank;
    }
    else
    {
      return std::nullopt;
    }

    if (bank < 0 || static_cast<std::size_t>(bank) >= banks_.size())
    {
      return std::nullopt;
    }
    const auto &records = banks_[static_cast<std::size_t>(bank)];
    if (index >= records.size())
    {
      return std::nullopt;
    }
    return records[index];
  }

} // namespace orphen::ported::entity

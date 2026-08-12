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

    // FUN_00229688 lines 22-24: `(float)*(int *)(src + n) / 1000.0`. Signed --
    // the field is read as `int`, and a prop hanging below its origin needs the
    // negative.
    bool readMillimetres(std::span<const std::uint8_t> data, std::size_t offset, float &out)
    {
      std::uint32_t raw = 0;
      if (!readU32(data, offset, raw))
      {
        return false;
      }
      out = static_cast<float>(static_cast<std::int32_t>(raw)) / 1000.0f;
      return true;
    }
  } // namespace

  void MapPropDescriptorTable::reset()
  {
    banks_.clear();
    sourceBanks_.clear();
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
      std::vector<MapPropSourceRecord> sources;
      records.reserve(recordCount);
      sources.reserve(recordCount);
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

        // Kept alongside, for FUN_00229980's descriptor synthesis. Read from
        // the same 0x28-byte record; FUN_00228e28 simply does not carry these
        // across, because the model record has nowhere to put them.
        MapPropSourceRecord sourceRecord;
        if (!readU16(descriptorBlob, source + 0x00, sourceRecord.flags0x00) ||
            !readMillimetres(descriptorBlob, source + 0x0C, sourceRecord.radius0x0c) ||
            !readMillimetres(descriptorBlob, source + 0x10, sourceRecord.height0x10) ||
            !readMillimetres(descriptorBlob, source + 0x14, sourceRecord.value0x14))
        {
          break;
        }
        sources.push_back(sourceRecord);
      }

      banks_.push_back(std::move(records));
      sourceBanks_.push_back(std::move(sources));
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

  std::optional<std::pair<std::size_t, std::size_t>> MapPropDescriptorTable::locate(
      std::uint32_t typeId, int stageBank) const
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
    return std::make_pair(static_cast<std::size_t>(bank), static_cast<std::size_t>(index));
  }

  std::optional<EntityModelRecord> MapPropDescriptorTable::FUN_00229980_resolve(std::uint32_t typeId,
                                                                                int stageBank) const
  {
    const auto found = locate(typeId, stageBank);
    if (!found.has_value())
    {
      return std::nullopt;
    }
    const auto &records = banks_[found->first];
    if (found->second >= records.size())
    {
      return std::nullopt;
    }
    return records[found->second];
  }

  std::optional<EntityDescriptor> MapPropDescriptorTable::FUN_00229980_synthesizeDescriptor(
      std::uint32_t typeId, int stageBank) const
  {
    const auto found = locate(typeId, stageBank);
    if (!found.has_value() || found->first >= sourceBanks_.size())
    {
      return std::nullopt;
    }
    const auto &sources = sourceBanks_[found->first];
    if (found->second >= sources.size())
    {
      return std::nullopt;
    }
    const MapPropSourceRecord &source = sources[found->second];

    EntityDescriptor descriptor;
    descriptor.source = DescriptorSource::Streamed;
    descriptor.typeId = typeId;
    // DAT_0031c1d0 is scratch in the data segment, not a table entry, so there
    // is no address to report.
    descriptor.recordAddress = 0;
    descriptor.modelIndex0x00 = static_cast<std::int16_t>(found->second);
    descriptor.flags0x04 = 0x0080;
    descriptor.radius0x08 = source.radius0x0c;
    descriptor.height0x0c = source.height0x10;
    descriptor.word0x10 = 0;
    descriptor.byte0x14 = (source.flags0x00 & 0x4000u) != 0 ? 0 : 0x10;
    descriptor.halfword0x16 = 0;
    descriptor.halfword0x18 = (source.flags0x00 & 0x8000u) != 0 ? 0x00D0 : 0x00D8;
    // descriptor +0x02 is FUN_0030bd20(record +0x14 / fGpffff8548), an angle
    // quantisation whose destination -- entity byte +0x133 -- the port does not
    // model, so it stays at its default rather than being half-ported.
    descriptor.byte0x02 = 0;
    return descriptor;
  }

} // namespace orphen::ported::entity

#include "ported/resource/character_stats.h"

#include <cstring>

namespace orphen::ported::resource
{
  namespace
  {
    // FUN_00223268(1, 0xBF, ...): archive index 1 is SCR.BIN.
    constexpr std::uint32_t kScrCharacterStatsResource = 0xBF;
    // FUN_00228e28:149 relocates dword 8 alone -- `*(uGpffffadf8 + 0x20) +=
    // uGpffffadf8` -- which is FUN_00229688's `param_1 + 0x20`.
    constexpr std::size_t kGroupTableDword = 8;
    // FUN_00229688 lines 22-24: the file stores these three as thousandths.
    constexpr float kMilliScale = 1.0f / 1000.0f;

    std::uint32_t u32At(const std::vector<std::uint8_t> &blob, std::size_t offset)
    {
      if (offset + 4 > blob.size())
      {
        return 0;
      }
      std::uint32_t value = 0;
      std::memcpy(&value, blob.data() + offset, sizeof(value));
      return value;
    }

    std::int16_t i16At(const std::vector<std::uint8_t> &blob, std::size_t offset)
    {
      if (offset + 2 > blob.size())
      {
        return 0;
      }
      std::uint16_t value = 0;
      std::memcpy(&value, blob.data() + offset, sizeof(value));
      return static_cast<std::int16_t>(value);
    }
  } // namespace

  bool CharacterStats::load(const std::filesystem::path &discRoot)
  {
    blob_.clear();
    groupTableOffset_ = 0;

    orphen::harness::FlatBinArchive scr;
    if (!scr.open(discRoot / "SCR.BIN"))
    {
      return false;
    }
    blob_ = scr.decode(kScrCharacterStatsResource);
    if (blob_.size() < 0x40)
    {
      blob_.clear();
      return false;
    }

    // Everything stays file-relative here; the original's relocation only exists
    // because its copy lives at an absolute address.
    groupTableOffset_ = u32At(blob_, kGroupTableDword * 4);
    if (groupTableOffset_ == 0 || groupTableOffset_ + 0x18 > blob_.size())
    {
      blob_.clear();
      groupTableOffset_ = 0;
      return false;
    }
    return true;
  }

  std::optional<StatRecord> CharacterStats::FUN_00229688_record(std::size_t group,
                                                                std::int32_t index) const
  {
    // `piVar3 = blob + *(int *)(groupTable + group * 8)`: the group index is a
    // table of {offset, count} pairs whose offset points at that group's own
    // {records, names, descriptions} triple. Records are the first of the three.
    if (blob_.empty() || groupTableOffset_ == 0 || index < 0)
    {
      return std::nullopt;
    }
    const std::uint32_t triple = u32At(blob_, groupTableOffset_ + group * 8);
    if (triple == 0 || triple + 12 > blob_.size())
    {
      return std::nullopt;
    }
    const std::uint32_t records = u32At(blob_, triple);
    const std::size_t at = records + static_cast<std::size_t>(index) * 0x28;
    if (records == 0 || at + 0x28 > blob_.size())
    {
      return std::nullopt;
    }

    StatRecord record;
    record.halfword00 = i16At(blob_, at + 0x00);
    record.halfword02 = i16At(blob_, at + 0x02);
    record.halfword04 = i16At(blob_, at + 0x04);
    record.byte06 = blob_[at + 0x06];
    record.byte07 = blob_[at + 0x07];
    record.byte08 = blob_[at + 0x08];
    record.byte09 = blob_[at + 0x09];
    record.radius0c =
        static_cast<float>(static_cast<std::int32_t>(u32At(blob_, at + 0x0C))) * kMilliScale;
    record.height10 =
        static_cast<float>(static_cast<std::int32_t>(u32At(blob_, at + 0x10))) * kMilliScale;
    record.float14 =
        static_cast<float>(static_cast<std::int32_t>(u32At(blob_, at + 0x14))) * kMilliScale;
    std::memcpy(record.tail18.data(), blob_.data() + at + 0x18, record.tail18.size());
    return record;
  }

  std::optional<StatRecord> CharacterStats::FUN_0025bae8_record(std::size_t group,
                                                                std::int16_t id) const
  {
    if (blob_.empty() || groupTableOffset_ == 0)
    {
      return std::nullopt;
    }

    // `if (*(short *)(groupTable + 0xc) < id)` -- the count beside group 1's
    // offset, read as a halfword. Group 1 holds 27 records and the largest party
    // character id is 0x16, so the party always takes the direct path; the scan
    // is here because the original has it.
    const std::int16_t directCount = i16At(blob_, groupTableOffset_ + group * 8 + 4);
    if (directCount < id)
    {
      const std::uint32_t scanCount = u32At(blob_, groupTableOffset_ + 0x14);
      for (std::uint32_t index = 0; index < scanCount; ++index)
      {
        const auto candidate = FUN_00229688_record(2, static_cast<std::int32_t>(index));
        if (!candidate.has_value())
        {
          break;
        }
        if (candidate->halfword02 == id)
        {
          return candidate;
        }
      }
      // The original leaves the destination holding the last record it looked
      // at rather than reporting a miss; nothing reads it in that case.
      return std::nullopt;
    }
    return FUN_00229688_record(group, id);
  }

} // namespace orphen::ported::resource

#include "ported/resource/hit_parameter_table.h"

#include <cstring>

namespace orphen::ported::resource
{
  namespace
  {
    // FUN_00223268(1, 0xBE, ...): archive index 1 is SCR.BIN.
    constexpr std::uint32_t kScrHitParameterResource = 0xBE;
    // FUN_00228e28:158 relocates dword 9 alone -- `*(uGpffffadfc + 0x24) +=
    // uGpffffadfc`.
    constexpr std::size_t kTripleTableDword = 9;
    constexpr std::size_t kTripleStride = 12;
    constexpr std::size_t kRecordStride = 4;

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
  } // namespace

  bool HitParameterTable::load(const std::filesystem::path &discRoot)
  {
    blob_.clear();
    tripleTableOffset_ = 0;

    orphen::harness::FlatBinArchive scr;
    if (!scr.open(discRoot / "SCR.BIN"))
    {
      return false;
    }
    blob_ = scr.decode(kScrHitParameterResource);
    if (blob_.size() < 0x40)
    {
      blob_.clear();
      return false;
    }

    // File-relative throughout; the original's relocation exists only because
    // its copy sits at an absolute address.
    tripleTableOffset_ = u32At(blob_, kTripleTableDword * 4);
    if (tripleTableOffset_ == 0 || tripleTableOffset_ + kTripleStride > blob_.size())
    {
      blob_.clear();
      tripleTableOffset_ = 0;
      return false;
    }
    return true;
  }

  std::optional<HitParameters> HitParameterTable::FUN_00216078_record(std::int16_t typeId,
                                                                     std::uint32_t index) const
  {
    if (blob_.empty() || tripleTableOffset_ == 0)
    {
      return std::nullopt;
    }

    // `while (*piVar2 != 0) { if (*piVar2 == typeId) ...; piVar2 += 3; }` -- a
    // linear walk with the zero type id as the terminator, so a type that is
    // not in the list simply has no attacks.
    std::size_t at = tripleTableOffset_;
    while (at + kTripleStride <= blob_.size())
    {
      const std::int32_t entryType = static_cast<std::int32_t>(u32At(blob_, at));
      if (entryType == 0)
      {
        break;
      }
      if (entryType == static_cast<std::int32_t>(typeId))
      {
        const std::uint32_t records = u32At(blob_, at + 4);
        const std::uint32_t count = u32At(blob_, at + 8);
        if (index >= count)
        {
          return std::nullopt;
        }
        const std::size_t record = records + static_cast<std::size_t>(index) * kRecordStride;
        if (record + kRecordStride > blob_.size())
        {
          return std::nullopt;
        }
        HitParameters parameters;
        parameters.flags =
            static_cast<std::uint16_t>(blob_[record] | (blob_[record + 1] << 8));
        parameters.powerBonus = static_cast<std::int8_t>(blob_[record + 2]);
        parameters.reaction = blob_[record + 3];
        return parameters;
      }
      at += kTripleStride;
    }
    return std::nullopt;
  }

} // namespace orphen::ported::resource

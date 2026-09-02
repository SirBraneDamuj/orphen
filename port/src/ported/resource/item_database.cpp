#include "ported/resource/item_database.h"

#include <cstring>

namespace orphen::ported::resource
{
  namespace
  {
    // FUN_00223268(1, 1, ...): archive index 1 is SCR.BIN, resource id 1.
    constexpr std::uint32_t kScrItemDatabaseResource = 1;
    // FUN_00228e28:127 relocates dwords 0..10; dword 8 is FUN_00229688's
    // `param_1 + 0x20`.
    constexpr std::size_t kGroupTableDword = 8;
    // FUN_0025b9e8 reads its stream table out of dword 5.
    constexpr std::size_t kMessageTableDword = 5;

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

    std::uint16_t u16At(const std::vector<std::uint8_t> &blob, std::size_t offset)
    {
      if (offset + 2 > blob.size())
      {
        return 0;
      }
      std::uint16_t value = 0;
      std::memcpy(&value, blob.data() + offset, sizeof(value));
      return value;
    }
  } // namespace

  bool ItemDatabase::load(const std::filesystem::path &discRoot)
  {
    blob_.clear();
    recordTableOffset_ = 0;
    nameTableOffset_ = 0;
    descriptionTableOffset_ = 0;
    messageTableOffset_ = 0;

    orphen::harness::FlatBinArchive scr;
    if (!scr.open(discRoot / "SCR.BIN"))
    {
      return false;
    }
    blob_ = scr.decode(kScrItemDatabaseResource);
    if (blob_.size() < 0x40)
    {
      blob_.clear();
      return false;
    }

    // The original adds the load address to the first eleven dwords; the port
    // keeps everything as offsets into the blob, so nothing is relocated and
    // the reads below stay file-relative.
    const std::uint32_t groupTable = u32At(blob_, kGroupTableDword * 4);
    const std::uint32_t group0 = u32At(blob_, groupTable);
    if (group0 == 0 || group0 + 12 > blob_.size())
    {
      blob_.clear();
      return false;
    }

    recordTableOffset_ = u32At(blob_, group0);
    nameTableOffset_ = u32At(blob_, group0 + 4);
    descriptionTableOffset_ = u32At(blob_, group0 + 8);
    if (nameTableOffset_ == 0 || nameTableOffset_ >= blob_.size())
    {
      blob_.clear();
      return false;
    }
    messageTableOffset_ = u32At(blob_, kMessageTableDword * 4);
    if (messageTableOffset_ >= blob_.size())
    {
      messageTableOffset_ = 0;
    }
    return true;
  }

  std::span<const std::uint8_t> ItemDatabase::FUN_0025b9e8_message(std::size_t index) const
  {
    if (messageTableOffset_ == 0)
    {
      return {};
    }
    const std::uint32_t offset = u32At(blob_, messageTableOffset_ + index * 4);
    if (offset == 0 || offset >= blob_.size())
    {
      return {};
    }
    return std::span<const std::uint8_t>(blob_.data() + offset, blob_.size() - offset);
  }

  std::optional<ItemRecord> ItemDatabase::FUN_00229688_record(std::int32_t itemId) const
  {
    // `puVar1 = *piVar3 + param_1 + param_3 * 0x28`, then a field-by-field copy.
    if (blob_.empty() || recordTableOffset_ == 0 || itemId < 0)
    {
      return std::nullopt;
    }
    const std::size_t at = static_cast<std::size_t>(recordTableOffset_) +
                           static_cast<std::size_t>(itemId) * 0x28u;
    if (at + 0x28 > blob_.size())
    {
      return std::nullopt;
    }
    const std::uint8_t *src = blob_.data() + at;

    const auto s32 = [src](std::size_t offset) {
      std::int32_t value = 0;
      std::memcpy(&value, src + offset, sizeof(value));
      return value;
    };

    ItemRecord record;
    for (std::size_t i = 0; i < record.ids.size(); ++i)
    {
      std::memcpy(&record.ids[i], src + i * 2, sizeof(std::uint16_t));
    }
    record.byte06 = src[0x06];
    record.byte07 = src[0x07];
    record.byte08 = src[0x08];
    record.byte09 = src[0x09];
    record.value0c = static_cast<float>(s32(0x0C)) / 1000.0f;
    record.value10 = static_cast<float>(s32(0x10)) / 1000.0f;
    record.value14 = static_cast<float>(s32(0x14)) / 1000.0f;
    std::memcpy(record.elementTable.data(), src + 0x18, record.elementTable.size());
    return record;
  }

  std::string ItemDatabase::stringAt(std::uint32_t tableOffset, std::int32_t itemId) const
  {
    if (blob_.empty() || tableOffset == 0 || itemId < 0)
    {
      return {};
    }
    // `*(u16 *)(base + *(int *)(base + table + id * 4))`, then the string is at
    // base + that.
    const std::uint32_t entry = u32At(blob_, tableOffset + static_cast<std::uint32_t>(itemId) * 4);
    if (entry == 0 || entry >= blob_.size())
    {
      return {};
    }
    const std::uint32_t stringOffset = u16At(blob_, entry);
    if (stringOffset == 0 || stringOffset >= blob_.size())
    {
      return {};
    }

    std::string text;
    for (std::size_t at = stringOffset; at < blob_.size() && blob_[at] != 0; ++at)
    {
      text.push_back(static_cast<char>(blob_[at]));
    }
    return text;
  }

  std::string ItemDatabase::FUN_00229688_name(std::int32_t itemId) const
  {
    return stringAt(nameTableOffset_, itemId);
  }

  std::string ItemDatabase::FUN_00229688_description(std::int32_t itemId) const
  {
    return stringAt(descriptionTableOffset_, itemId);
  }

} // namespace orphen::ported::resource

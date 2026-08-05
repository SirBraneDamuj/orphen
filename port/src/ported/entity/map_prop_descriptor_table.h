#pragma once

// Model records for map-streamed props -- the entity types the ELF's descriptor
// table cannot answer for.
//
//   src/FUN_00228e28.c:150-193   builds the banks at boot
//   src/FUN_00229980.c:37-66     resolves a type id against them
//   src/FUN_0022a418.c:50        picks which bank the scene uses
//
// FUN_00228e28 loads SCR.BIN resource 0xBD -- a blob of 0x28-byte entity
// descriptors -- and derives a parallel set of 0x2C-byte *model* records from
// it. The word at blob+0x20 points at a list of (offset, count) pairs, one per
// bank, terminated by a zero offset. Each pair's offset points at another word
// inside the blob, which is the offset of that bank's descriptor array. Only
// three fields survive the derivation:
//
//   record.meshId  = *(u16 *)(descriptor + 2)
//   record.texId   = *(u16 *)(descriptor + 4)
//   record.flags   = 0x41            -- constant, written by the loop itself
//
// Bit 0 is the PSC3 path and bit 6 means the texture id is cached negated, which
// is why cache slot 18 reads -303 while holding tex 0x012F.
//
// FUN_00229980 then maps three ranges of 0x100 type ids onto a bank:
//
//   0x272..0x371  bank = DAT_00355208   0x373..0x472  bank = 0xF
//   0x474..0x573  bank = 0x10
//
// and DAT_00355208 is just the scene's stage number (FUN_0022a418:50 copies it
// from DAT_003551f4). s01_e024 is stage 1, so its props come from bank 1.
//
// Checked against s01_e24.bin: 20 banks, bank 1 holding 99 records, record 0
// reading mesh=0x009F tex=0x012F -- which is what entity slot 10's +0x160
// points at in the dump.

#include "ported/entity/entity_descriptor_table.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace orphen::ported::entity
{

  // FUN_00229980's three ranges.
  inline constexpr std::uint32_t kMapPropRangeBase = 0x272;
  inline constexpr std::uint32_t kMapPropRangeSecond = 0x373;
  inline constexpr std::uint32_t kMapPropRangeThird = 0x474;
  inline constexpr std::uint32_t kMapPropRangeSize = 0x100;
  inline constexpr int kMapPropSecondBank = 0xF;
  inline constexpr int kMapPropThirdBank = 0x10;

  class MapPropDescriptorTable
  {
  public:
    // `descriptorBlob` is SCR.BIN resource 0xBD, already decompressed.
    bool FUN_00228e28_build(std::span<const std::uint8_t> descriptorBlob);
    void reset();

    bool valid() const { return !banks_.empty(); }
    std::size_t bankCount() const { return banks_.size(); }
    std::size_t recordCount(std::size_t bank) const;

    // True when the id falls in one of the three ranges, whether or not a
    // record exists for it -- so callers can tell "not a map prop" from "map
    // prop this scene has no bank for".
    static bool isMapPropType(std::uint32_t typeId);

    // FUN_00229980:37-66. `stageBank` is DAT_00355208, used only by the first
    // range; the other two carry their own fixed bank.
    std::optional<EntityModelRecord> FUN_00229980_resolve(std::uint32_t typeId, int stageBank) const;

  private:
    std::vector<std::vector<EntityModelRecord>> banks_;
  };

} // namespace orphen::ported::entity

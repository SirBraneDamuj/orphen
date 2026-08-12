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
#include <utility>
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

  // The 0x28-byte source record, as FUN_00229688 copies it out. The derived
  // 0x2C model record above keeps only mesh and texture, but FUN_00229980 reads
  // these same source fields a second time to synthesise the *entity*
  // descriptor a streamed type spawns from -- the scratch block at
  // DAT_0031c1d0. That descriptor is the only one such a type ever has, so
  // without these fields a map prop spawns entirely on struct defaults.
  //
  // Offsets are FUN_00229688's, reading its destination stack frame backwards:
  // the three ints at +0x0C/+0x10/+0x14 are millimetres and become floats.
  struct MapPropSourceRecord
  {
    std::uint16_t flags0x00 = 0;
    float radius0x0c = 0.0f;
    float height0x10 = 0.0f;
    float value0x14 = 0.0f;
  };

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

    // FUN_00229980:47-63, the block that fills DAT_0031c1d0 before returning it
    // as the descriptor. Everything in it is either a constant or a bit test on
    // the source record's +0x00:
    //
    //   +0x04 = 0x80                          descriptor flags -> entity +0x02
    //   +0x08 = record +0x0C / 1000           collision radius
    //   +0x0C = record +0x10 / 1000           collision height
    //   +0x14 = (flags & 0x4000) ? 0 : 0x10   -> entity +0x06
    //   +0x16 = 0                             -> entity +0x08
    //   +0x18 = (flags & 0x8000) ? 0xD0 : 0xD8 -> entity +0x04
    //
    // The +0x14 bit is the load-bearing one. Entity +0x06 bit 0x10 makes
    // FUN_00225c90 return before it advances anything, so a prop whose record
    // clears 0x4000 is a *static* prop -- its animation never runs. Defaulting
    // that field to zero animates every prop in the scene forever, which is
    // what drove s01_e012's bar props into a pile of stretched geometry.
    std::optional<EntityDescriptor> FUN_00229980_synthesizeDescriptor(std::uint32_t typeId,
                                                                      int stageBank) const;

  private:
    // Index of (bank, typeId) into the two parallel bank arrays, or nullopt when
    // the id is out of range or this scene has no such bank.
    std::optional<std::pair<std::size_t, std::size_t>> locate(std::uint32_t typeId,
                                                              int stageBank) const;

    std::vector<std::vector<EntityModelRecord>> banks_;
    std::vector<std::vector<MapPropSourceRecord>> sourceBanks_;
  };

} // namespace orphen::ported::entity

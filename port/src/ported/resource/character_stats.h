#pragma once

// uGpffffadf8: the character stat table -- SCR.BIN resource **0xBF**, not
// resource 1. Same blob layout as the item database next to it, and the same
// FUN_00229688 reader, but a different resource and a different group.
//
//   src/FUN_00228e28.c:145  FUN_00223268(1, 0xBF, ...) -> uGpffffadf8, then
//                           only dword 8 (+0x20, the group index) is relocated
//   src/FUN_0025bae8.c      the lookup: group 1 indexed by character id, with a
//                           linear scan of group 2 as the fallback
//   src/FUN_002294d0.c      fills DAT_00343688 from it, one record per party
//                           slot, keyed by DAT_0031c1f0
//
// Group 1 holds 27 records; the seven party character ids (1, 3, 4, 5, 6, 7,
// 0x16) all index it directly. Each record's +0x0C and +0x10 are the collision
// radius and body height a follower gets from FUN_0023a518 -- Orphen 0.25/0.80,
// Cleo 0.15/0.70, Magnus 0.15/0.70 -- which match eeMemory.bin's copy of
// DAT_00343688 exactly.

#include "harness/flat_bin_archive.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace orphen::ported::resource
{

  // FUN_00229688's destination frame: 0x28 bytes, and not a straight copy of
  // the source record -- three of its dwords are integer thousandths in the
  // file and floats here. Offsets are the original's.
  struct StatRecord
  {
    std::int16_t halfword00 = 0;
    std::int16_t halfword02 = 0;
    std::int16_t halfword04 = 0;
    std::uint8_t byte06 = 0;
    std::uint8_t byte07 = 0;
    std::uint8_t byte08 = 0;
    std::uint8_t byte09 = 0;
    // +0x0A is deliberately absent: FUN_00229688 does not copy it, and the
    // party table's copy of this record uses that halfword for the bound pool
    // slot (DAT_00343692).
    float radius0c = 0.0f; // +0x0C -> entity +0x54
    float height10 = 0.0f; // +0x10 -> entity +0x58
    float float14 = 0.0f;  // +0x14
    std::array<std::uint8_t, 0x10> tail18{}; // +0x18..+0x27
  };

  class CharacterStats
  {
  public:
    // `scrResource` is the SCR.BIN id the blob comes from. 0xBF is uGpffffadf8,
    // the character and enemy table; FUN_00228e28 loads 0xBD the same way into
    // uGpffffadf4, and FUN_002334e8 reads that one for the object type ranges
    // above 0x272.
    bool load(const std::filesystem::path &discRoot, std::uint32_t scrResource = 0xBF);
    bool loaded() const { return !blob_.empty(); }

    // FUN_0025bae8(group, id, dest) for a non-zero group: index group `group`
    // directly when the id is inside its count, otherwise scan group 2 and
    // match on the record's own +0x02.
    std::optional<StatRecord> FUN_0025bae8_record(std::size_t group, std::int16_t id) const;

    // FUN_00229688 with only its destination set.
    std::optional<StatRecord> FUN_00229688_record(std::size_t group, std::int32_t index) const;

    // FUN_0025BA98(type, dest), the *object* table's three-way split. Only
    // meaningful on the 0xBD blob (uGpffffadf4):
    //
    //   0x272..0x372  group iGpffffb298 (DAT_00355208, the map-prop bank),
    //                 indexed by `type - 0x272`
    //   0x373..0x473  group 15, indexed by `type - 0x373`
    //   0x474 and up  group 16, indexed by `type - 0x474`
    //
    // FUN_002334E8 splits the same three ways, which is why a streamed prop and
    // the target readout agree about what a placement is.
    std::optional<StatRecord> FUN_0025ba98_record(int DAT_00355208_mapPropBank,
                                                  std::int16_t typeId) const;

    // FUN_00229688's param_5: the row's name. The group's second dword is a
    // table of u32 offsets, each pointing at a u16 that in turn gives the
    // string's offset in the blob -- the same two-step the item database uses.
    std::string FUN_00229688_name(std::size_t group, std::int32_t index) const;

    // The count beside a group's offset in the index table. FUN_002334e8 walks
    // group 2 with it looking for a matching type id.
    std::uint32_t groupCount(std::size_t group) const;

  private:
    std::vector<std::uint8_t> blob_;
    std::uint32_t groupTableOffset_ = 0;
  };

} // namespace orphen::ported::resource

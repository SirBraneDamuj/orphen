#pragma once

// uGpffffadfc: the attack parameter table -- SCR.BIN resource **0xBE**, the
// blob loaded immediately after the character stats (0xBF) and the shared
// enemy table (0xBD).
//
//   src/FUN_00228e28.c:155  FUN_00223268(1, 0xBE, ...) -> uGpffffadfc, then
//                           dword 9 (+0x24) alone is relocated
//   src/FUN_00216078.c      the lookup
//
// The blob's +0x24 points at a list of twelve-byte triples, terminated by a
// zero type id:
//
//     +0x00 s32 entity type id
//     +0x04 u32 byte offset, from the blob base, of that type's records
//     +0x08 s32 record count
//
// A record is four bytes and describes one *attack*, not one attacker -- the
// player has two, index 0 for the sword and index 1 for the magic projectile,
// and FUN_00256130 and FUN_002d2e00 ask for them by that index. The spawner
// copies the record onto the effect entity that will carry the hit, and the
// swept hit tests are its only readers.
//
// Read out of eeMemory.bin, the player's two records are `01 00 1e 00` and
// `10 00 0a 00`: element bit 0 at +30% for the sword, element bit 4 at +10%
// for the bolt.

#include "harness/flat_bin_archive.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace orphen::ported::resource
{

  // The four bytes FUN_00216078 copies. FUN_00216140 is the only thing that
  // decodes them.
  struct HitParameters
  {
    // +0x00: the element/behaviour bit set, stored on the victim at +0xC2.
    // FUN_00216140 picks the element as the *lowest set bit*, so 0x0001 is
    // element 0 and 0x0010 is element 4; that index selects one byte of the
    // victim's sixteen-byte resistance table. Bit 0x4000 additionally skips the
    // guard-arc test.
    std::uint16_t flags = 0;
    // +0x02: a percentage bonus, signed. The damage scale is (bonus + 100)/100,
    // so 30 means the attack lands at 1.3x the attacker's +0x12C.
    std::int8_t powerBonus = 0;
    // +0x03: the reaction the victim plays, stored at +0xBC. 0x1B and 0x1D pick
    // the second hit-spark colour, 0x1E suppresses the follow-up effect.
    std::uint8_t reaction = 0;

    // The packed form the original stores at the effect entity's +0x198.
    std::uint32_t packed() const
    {
      return static_cast<std::uint32_t>(flags) |
             (static_cast<std::uint32_t>(static_cast<std::uint8_t>(powerBonus)) << 16) |
             (static_cast<std::uint32_t>(reaction) << 24);
    }

    static HitParameters unpack(std::uint32_t value)
    {
      HitParameters parameters;
      parameters.flags = static_cast<std::uint16_t>(value & 0xFFFFu);
      parameters.powerBonus = static_cast<std::int8_t>((value >> 16) & 0xFFu);
      parameters.reaction = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
      return parameters;
    }
  };

  class HitParameterTable
  {
  public:
    bool load(const std::filesystem::path &discRoot);
    bool loaded() const { return !blob_.empty(); }

    // FUN_00216078(typeId, index, dest). Returns nothing when the type has no
    // entry, which is what the original's `return 0` means; the caller leaves
    // the destination as it was.
    //
    // The original asserts when `index` is outside the type's count and then
    // reads anyway. The port returns nothing instead: an out-of-range index is
    // a bug in the caller, not a behaviour to reproduce, and nothing in
    // s01_e024 reaches it.
    std::optional<HitParameters> FUN_00216078_record(std::int16_t typeId,
                                                     std::uint32_t index) const;

    // DAT_00354C64 (iGpffffacf4): the type's attack count, which FUN_00216078
    // publishes as a side effect of every successful lookup. FUN_0023f8b8 reads
    // it back to bound its own walk over the same records, so the port exposes
    // it directly rather than reproducing the global.
    std::uint32_t FUN_00216078_count(std::int16_t typeId) const;

  private:
    std::vector<std::uint8_t> blob_;
    std::uint32_t tripleTableOffset_ = 0;
  };

} // namespace orphen::ported::resource

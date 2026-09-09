#pragma once

// DAT_003253C0 -- **the boss-battle target marker table**, and the second of
// the battle module's two targeting modes.
//
// `FUN_002462C8` branches on `DAT_00354EC0` and the two arms are nothing alike:
//
//   null      the ordinary encounter. Targets come from the encounter blob's
//             actor records, a D-pad tap arms DAT_00354E96 for 120 frames, and
//             FUN_0023C340 freezes the field for as long as that timer runs.
//             That is the pause-and-pick display.
//   non-null  a boss. Targets come from *this* table, every one of them already
//             wearing its own 0x192 cursor, and a D-pad tap moves between them
//             on the frame it is read. DAT_00354E96 is written to zero on the
//             way in, so the display never arms and nothing freezes.
//
// Which mode a scene runs in is decided at load. `FUN_0022A360` picks a scene
// module out of `PTR_LAB_003252B8` by the descriptor's `+0x02`, and ten of the
// twenty-eight modules open their load hook with
//
//     FUN_00267E78(0x3253C0, 400);      // twenty entries of 0x14, cleared
//     FUN_00247F18(0x3253C0, 0x14);     // DAT_00354EC0 / DAT_00354EC4
//
// which is what puts that scene into marker mode. s14_e001 is module 10,
// `FUN_0026C0F0`. Section 14's descriptor list is indexed by position rather
// than by the entry number in its own +0x00, and the two only line up for the
// first eleven rows, so a scene further down it is not assumed to be one of
// the ten until there is something to check the answer against.
//
//   src/FUN_00247f18.c   the registration, two stores
//   src/FUN_00247f28.c   add: stamp an entry, raise the entity's +0x96 bit 0
//                        and, for kind 2, spawn its cursor and become kind 3
//   src/FUN_00248040.c   remove: drop the bit, destroy the cursor, zero the row
//   src/FUN_00248108.c   service: kind 1 rows are released, kind 2 rows added
//   src/FUN_002481f0.c   cycle: step to the next row that still names a live
//                        entity, with the same half-wrap Up/Down uses
//
// An entry is 0x14 bytes:
//
//   +0x00  s16  kind. 0 empty, 1 "release me", 2 "add me", 3 live.
//   +0x02  s16  the pool slot being aimed at
//   +0x04  s16  the pool slot of its type 0x192 cursor
//   +0x08  f32  cursor offset x   (FUN_002D86B0's second argument)
//   +0x0C  f32  cursor offset y
//   +0x10  f32  cursor offset z

#include "ported/entity/entity_pool.h"

#include <array>
#include <cstdint>
#include <functional>

namespace orphen::ported::battle
{

  inline constexpr std::uint32_t kDAT_003253c0_markerTable = 0x003253C0;
  inline constexpr std::int16_t kMarkerStride = 0x14;
  inline constexpr std::size_t kMarkerCount = 20; // 400 / 0x14

  struct TargetMarker
  {
    std::int16_t kind00 = 0;
    std::int16_t slot02 = 0;
    std::int16_t cursor04 = 0;
    float offsetX08 = 0.0f;
    float offsetY0c = 0.0f;
    float offsetZ10 = 0.0f;
  };

  class TargetMarkerTable
  {
  public:
    // FUN_002D86B0 and FUN_00265EC0, bound once by the runtime. The cursor is a
    // pool entity and the table is not allowed to know how one is made.
    std::function<std::int32_t(std::int32_t targetSlot, float x, float y, float z,
                               std::int8_t depthOverride)>
        FUN_002d86b0_spawn_cursor;
    std::function<void(std::int32_t slot)> FUN_00265ec0_destroy;

    // FUN_00267E78(0x3253C0, 400) -- the clear that precedes every
    // registration, and what a scene change leaves behind.
    void FUN_00267e78_clear();

    // FUN_00247F18(table, count). The port has one table, so only the count
    // travels; a zero count is the field-encounter mode.
    void FUN_00247f18_register(std::int16_t count);

    // DAT_00354EC0 / DAT_00354EC4 as the rest of the module reads them.
    bool registered() const { return count_ != 0; }
    std::int16_t sGpffffaf54_count() const { return count_; }

    TargetMarker &entry(std::size_t index) { return entries_[index]; }
    const TargetMarker &entry(std::size_t index) const { return entries_[index]; }

    // FUN_00247F28(entity, index, kind, depthOverride). Returns the index, or
    // -1 when it is past the registered count. Kind 2 spawns the cursor here
    // and the row is left as kind 3.
    std::int32_t FUN_00247f28_mark(orphen::ported::entity::EntityPool &pool,
                                   std::int32_t entitySlot,
                                   std::int16_t index,
                                   std::int16_t kind,
                                   std::int8_t depthOverride);

    // FUN_00248040(entity). Drops +0x96 bit 0 always; the row and its cursor go
    // only if one names this entity with a kind above 1.
    void FUN_00248040_unmark(orphen::ported::entity::EntityPool &pool, std::int32_t entitySlot);

    // FUN_00248108. Runs the kind 1 and kind 2 requests other code has left in
    // the table -- FUN_002462C8 calls it once a frame in marker mode.
    void FUN_00248108_service(orphen::ported::entity::EntityPool &pool);

    // FUN_002481F0(current, direction, halfWrap). `current` and the answer are
    // both pool slots, not row indices. -1 is "nothing left to aim at".
    std::int32_t FUN_002481f0_cycle(const orphen::ported::entity::EntityPool &pool,
                                    std::int32_t current,
                                    std::int16_t direction,
                                    bool halfWrap) const;

  private:
    std::array<TargetMarker, kMarkerCount> entries_{};
    std::int16_t count_ = 0;
  };

} // namespace orphen::ported::battle

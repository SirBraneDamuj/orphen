#pragma once

// The elemental field objects -- the smoke clouds, flame pillars and healing
// motes a battle arena is dressed with, which the player can lock onto and
// destroy like any enemy.
//
//   src/FUN_002f0608.c  the spawn hook: it *retypes* the entity
//   src/FUN_002f08f8.c  the per-frame behaviour (not ported yet)
//   src/FUN_0025e7c0.c  the caller, from the map's object placement table
//
// == Why one of these is not the type it was spawned as ==
//
// FUN_0025E7C0 turns a group-4 placement into type `(id - 1) + 0x373`, which
// for s14_e012's record #104 is 0x37C -- a plain streamed prop as far as the
// spawn is concerned. FUN_002F0608 then looks that type up in SCR.BIN 0xBD
// (FUN_0025BA98) and reads the row's `+0x27` **kind** byte, and if the kind is
// 1..9 it overwrites the entity's own type id with `kind + 0x6B`:
//
//   kind 1 -> 0x6C Candlestick     kind 5 -> 0x70 Electric Element
//   kind 2 -> 0x6D Lamp            kind 6 -> 0x71 Wind Element
//   kind 3 -> 0x6E Fire Element    kind 7 -> 0x72 Darkness Element
//   kind 4 -> 0x6F Water Element   kind 8 -> 0x73 Healing Element
//                                  kind 9 -> 0x74 the elemental field effects
//
// Those are the names in SCR.BIN 0xBF **group 2**, and 0x6C..0x73 is exactly
// the band FUN_002334E8 scans that group for. So the retype is what makes the
// target readout say "Darkness Element" rather than 'ankokudamege', the
// internal name of the 0xBD row it came from. The original type is kept at
// +0x19E; nothing in the port reads it back yet.
//
// A kind of 0, 10..13, 0x0E or 0x0F is not an element at all: FUN_002F0608
// returns false and its caller falls through to the ordinary streamed-prop
// setup.
//
// == The damage table ==
//
// DAT_0058B970 is four bytes per type id, and FUN_002F0608 is the only thing
// that fills it. FUN_002F08F8 passes `&DAT_0058B970 + type * 4` straight to the
// hit test as the parameter block for the damage this object deals, so the
// entity carries no copy of it -- two objects of the same type share one row.

#include "ported/entity/actor_frame_update.h"
#include "ported/entity/original_entity.h"
#include "ported/resource/character_stats.h"

#include <array>
#include <cstdint>

namespace orphen::ported::entity
{

  // DAT_0058B970. The array runs up to the entity pool at 0x0058BEB0, which is
  // 0x540 bytes -- 336 rows -- and the retyped ids all land near 0x70, so the
  // reachable part is tiny. Sized for the whole thing anyway, because the index
  // is the entity's type id and nothing clamps it.
  struct ElementDamageTable
  {
    struct Row
    {
      std::uint16_t elementMask = 0; // +0x00: 1 << the element index
      std::uint8_t power = 0;        // +0x02: that element's byte in the stat tail
      std::uint8_t byte03 = 0;       // +0x03: the row's +0x08
    };
    static constexpr std::size_t kRowCount = 0x540 / 4;
    std::array<Row, kRowCount> rows{};

    void clear() { rows.fill(Row{}); }
  };

  // FUN_002F0608 (0x002f0608). `record` is what FUN_0025BA98 fetched for the
  // entity's *current* type id. Returns true when the entity was an element and
  // has been set up -- the caller then skips its own descriptor pass, which is
  // what the original's `goto` does.
  bool FUN_002f0608_element_object(OriginalEntity &entity,
                                   const orphen::ported::resource::StatRecord &record,
                                   ElementDamageTable &damage);

} // namespace orphen::ported::entity

#pragma once

// The type 0x68 health bar -- the segmented gauge that slides on screen when
// something takes a hit.
//
//   src/FUN_002d5630.c   0x002d5630  arm it: pick the bank, count the pips
//   (no src file)        0x002d5748  its per-frame behaviour, a bare LAB_ block
//   src/FUN_0022a418.c   0x0022a418  builds both of them on every scene load
//
// **There are exactly two, and they are pool slots 2 and 3.** FUN_0022a418
// builds them in place at 0x0058C260 and 0x0058C438, which are `DAT_0058BEB0 +
// 2 * 0x1D8` and `+ 3 * 0x1D8` -- the entity stride, not two loose structs. The
// bank is picked by the victim's descriptor +0x04:
//
//   (+0x02 & 0x48) == 0   slot 2, the **lower** bar. Starts at screen row 456
//                         and slides up 2.667 a frame for fifteen frames, so it
//                         settles at 416. Animation base 0.
//   (+0x02 & 0x48) != 0   slot 3, the **upper** bar. Starts at -8 and slides
//                         down to 32. Animation base 0x14.
//
// Every enemy descriptor in the game carries flags 0x0008, so an enemy always
// gets the upper bar; the party types 3..7 carry 0x4004 and match neither the
// 0x4B gate nor the 0x48 bank test, which is why a party member's hit does not
// raise one -- the player's own readout is FUN_00230E50's panel instead. The
// lower bar belongs to the descriptor flags 0x01/0x02 band and to the two class
// 1 states, FUN_0024BD30 and FUN_0024CBA0, that arm it by hand.
//
// ------------------------------------------------------------- the five pips
//
// FUN_002d5630 is handed the victim's hit points **before** the wrapper drains
// them, its maximum, and the damage still sitting in +0xBE. It bands both the
// before and the after into five pips, rounding up:
//
//   +0x1A4 = (clamp(hp, 0, max) * 5 + max - 1) / max      where the bar starts
//   +0x1A8 = (clamp(hp - min(dmg, max), 0, max) * 5 + max - 1) / max   where it stops
//
// so a hit that takes nothing off still shows the bar, and one that empties it
// walks all five pips out. The walk is what +0x198 counts: fifteen frames of
// sliding on, one frame that drops the first pip, then one pip per animation
// loop until +0x1A4 reaches +0x1A8, forty-eight frames of holding, fifteen of
// sliding back off, and +0x08 bit 0 to stop drawing.
//
// The pip count is spelled as the animation, not as geometry. Three banks of
// five sit 0x14 apart per bar: `base - segments + 21` is the one FUN_002d5630
// stamps as it arms, `base - segments + 6` the draining frame, and
// `base - segments + 11` the settled one. All three are written straight to
// +0xA0 rather than through FUN_00225BC8, so the timeline cursor carries across
// the change -- that is the original, and it is why a bar mid-drain does not
// restart its clip.

#include "ported/entity/entity_descriptor_table.h"
#include "ported/entity/entity_pool.h"
#include "ported/entity/original_entity.h"

#include <cstddef>
#include <cstdint>

namespace orphen::ported::entity
{

  // 0x0058C260 and 0x0058C438.
  inline constexpr std::size_t kDAT_0058c260_lowerBarSlot = 2;
  inline constexpr std::size_t kDAT_0058c438_upperBarSlot = 3;
  inline constexpr std::int32_t kHealthBarTypeId = 0x68;
  inline constexpr std::uint32_t kFUN_002d5748_healthBar = 0x002D5748;

  // FUN_0022a418:378-383. Both bars, built and parked hidden.
  void FUN_0022a418_build_health_bars(EntityPool &pool, const EntityDescriptorTable &descriptors);

  // FUN_002d5630. `upperBank` is the victim's `(+0x02 & 0x48) != 0`.
  void FUN_002d5630_arm_health_bar(EntityPool &pool,
                                   bool upperBank,
                                   std::int32_t hitPoints,
                                   std::int32_t maxHitPoints,
                                   std::int32_t damage,
                                   bool DAT_003555d3_groupEScene);

  // 0x002d5748, the type 0x68 behaviour.
  void FUN_002d5748_health_bar(OriginalEntity &entity, std::uint32_t frameTicks);

} // namespace orphen::ported::entity

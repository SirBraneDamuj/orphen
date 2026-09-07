#pragma once

// Type 0x10C, the bubble column the crab's stamp throws up out of the water.
//
// Source map:
//   src/FUN_002ea7f0.c  0x002EA7F0  the behaviour -- PTR_LAB_0031CAB0's entry
//                                   for 0x10C, confirmed out of the executable
//   src/FUN_002eac48.c  0x002EAC48  the spawner, five modes
//   src/FUN_0027a440.c  lines 58-95, the crab's stamp: ten, fifteen or twenty
//                       of mode 0 off bone 6, then three of mode 4 in a ring
//
// A bubble is an ordinary pool entity with its own two-stage rise. Stage one
// (`+0x198` = 0) runs its spawn timer down while it travels along `+0x5C` at
// `+0x19C` and climbs at `+0x1A0`; when the timer expires stage two starts,
// the climb gains five units and the speed is scaled by however much of the
// *second* timer is left, so it slows as it goes. Either stage ends the same
// way: hit anything solid, or run out, and the bubble switches to animation 2
// and state 2, which is the pop.
//
// The two `+0x0C` masks are what decide which. `0x4006` -- a wall, a ceiling or
// the map -- destroys it outright with no pop. `0x60`, another entity, pops it
// *and* applies the crab's attack record through FUN_002EF510, but only once:
// `DAT_0035529C` latches so a column of twenty bubbles cannot land twenty hits.
//
// The state 1 path is the same machine with the ceiling test dropped, which is
// what makes a mode-1 bubble able to leave the water.

#include "ported/entity/actor_frame_update.h"
#include "ported/entity/original_entity.h"
#include "ported/resource/hit_parameter_table.h"

#include <cstddef>
#include <cstdint>

namespace orphen::ported::entity
{

  inline constexpr std::int32_t kBubbleTypeId = 0x10C;
  inline constexpr std::uint32_t kFUN_002ea7f0_bubble = 0x002EA7F0;

  // DAT_0035529C. One latch for the whole column: the first bubble that reaches
  // the player is the only one that can hurt them. Nothing clears it but a
  // scene load, which is what the original does too.
  std::uint8_t &DAT_0035529c_bubbleHitLatch();

  // FUN_002EA7F0.
  void FUN_002ea7f0_bubble(OriginalEntity &entity,
                           std::size_t slot,
                           const ActorEnvironment &environment);

  // FUN_002EAC48(source, mode, position, spread, attack).
  //
  // Modes 0 and 1 are the rising bubble -- 1 leaves the water, 0 does not --
  // and take a random heading within `spread * 20` degrees either side of the
  // source's facing. Modes 2 and 4 are the flat burst that sits where it is put
  // at double scale. Any other mode allocates the entity and leaves it as the
  // pool clear made it, which is what the original does.
  void FUN_002eac48_spawn_bubble(const OriginalEntity &source,
                                 std::int16_t mode,
                                 const orphen::ported::psm2::Vec3 &position,
                                 std::uint32_t spread,
                                 const orphen::ported::resource::HitParameters *attack,
                                 const ActorEnvironment &environment);

} // namespace orphen::ported::entity

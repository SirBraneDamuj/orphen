#pragma once

// The burning ship: three entity types that between them are every flame and
// puff of smoke on the wreck in s01_e013's monster animatic.
//
//   src/FUN_002ed3e0.c   0x002ED3E0, type 0x1AA -- the root and its two halves
//   (no src file)        0x002ED980, type 0x1AB -- eight instructions, below
//   src/FUN_002ed9a0.c   0x002ED9A0, type 0x1AC -- one link of a chained ring
//
// The three sit next to each other in the tertiary handler table:
// PTR_LAB_0031CAB0 + (id - 0xFC) * 4 reads 0x002ED3E0, 0x002ED980, 0x002ED9A0
// and then 0x00239E78 -- the no-op -- for 0x1AD.
//
// == Why this was worth porting ==
//
// The scene script spawns two type 0x1AA entities with opcode 0x52 and then
// leaves them alone: everything they do, they do to themselves. Without the
// behaviour they never spawn their children, never fade and, above all, never
// call FUN_00265EC0 on themselves -- so in the port they stood at the ship for
// the whole animatic drawing their own model at full opacity, two screen-filling
// white clouds over the monster shots.
//
// On hardware they are gone by the time the beam shot plays: at that frame the
// allocation bytes at DAT_005A96B0 read 0 for their slots and every field but
// the type is still the stale data FUN_00265EC0 left behind.
//
// == The shape of it ==
//
// A type 0x1AA is one of three things, told apart by +0x60:
//
//   0  the root the script spawned. Every time its animation comes round it
//      showers five type 0x1AB puffs into a 2..6 unit disc about itself, and on
//      that same beat it spawns the other two halves of itself -- a state 1 on
//      animation 2 and a state 2 on animation 3, both at seven tenths its own
//      scale. It never fades; it goes when its own animation ends.
//   1  the smoke half. It opens one chained type 0x1AC ring and fades out over
//      0xC80 ticks once animation 2 reaches timeline entry 2.
//   2  the flame half. Same, over 0x780 ticks at entry 4.
//
// A type 0x1AC carries the chain: each link spawns the next one and stops when
// the counter it inherits at +0x198 runs out, three links in all. The ring
// turns by 120 degrees a link, because the angle is `+0x19A * 0x78` degrees and
// +0x19A is stepped once per link, so the three do not sit on top of each other.
// Each link fades over 2240 ticks and then waits for its animation to end.
//
// A type 0x1AB is the simplest thing in the file:
//
//     lhu v0, 6(a0); andi v0, v0, 1; beq v0, zero, +3; nop
//     j 0x00265EC0; nop; jr ra; nop
//
// -- destroy me when my animation has run out, and nothing else. It is a puff
// of smoke with a lifetime and no behaviour.
//
// == Two details that are easy to get wrong ==
//
// **The `|= 0x80` on a freshly spawned child is dead.** Every spawn in
// FUN_002ED3E0 raises bit 0x80 of the child's +0x08 and then, four stores
// later, assigns the parent's whole +0x08 over it. The port reproduces both
// writes in the original's order rather than tidying them, because the parent's
// +0x08 is what actually lands and a reader who only saw the OR would expect
// otherwise.
//
// **The fade divides by zero on its last frame.** The ramp reloads +0x19C
// *after* the same block may have zeroed it, so the final frame evaluates
// `124 - (t / 0) * 124` = -inf and hands that to FUN_0030BDB0, which is
// `__fixunssfsi` and answers 0 for anything negative. The level therefore lands
// on 3, not on some large number, and the port takes that branch explicitly
// instead of reproducing the division.
//
// == Where the water is ==
//
// Everything here is placed at DAT_003556FC -- `fGpffffb78c`, the float opcode
// 0xE2 writes as `expr / 100000`. It is the animatic's sea level, and it is the
// z every fire and puff is pinned to, along with both of its ground heights at
// +0x4C and +0x50. s01_e013 sets it to 0.

#include "ported/entity/actor_frame_update.h"
#include "ported/entity/original_entity.h"

#include <cstddef>
#include <cstdint>

namespace orphen::ported::entity
{

  // The three type ids, in the order the handler table holds them.
  inline constexpr std::int32_t kShipFireRootType = 0x1AA;
  inline constexpr std::int32_t kShipFirePuffType = 0x1AB;
  inline constexpr std::int32_t kShipFireRingType = 0x1AC;

  // FUN_002ED3E0. The root and both of its halves.
  void FUN_002ed3e0_ship_fire(OriginalEntity &entity,
                              std::size_t slot,
                              const ActorEnvironment &environment);

  // 0x002ED980. `if (+0x06 & 1) FUN_00265EC0(this);`
  void LAB_002ed980_ship_fire_puff(OriginalEntity &entity,
                                   std::size_t slot,
                                   const ActorEnvironment &environment);

  // FUN_002ED9A0. One link of the chained ring.
  void FUN_002ed9a0_ship_fire_ring(OriginalEntity &entity,
                                   std::size_t slot,
                                   const ActorEnvironment &environment);

} // namespace orphen::ported::entity

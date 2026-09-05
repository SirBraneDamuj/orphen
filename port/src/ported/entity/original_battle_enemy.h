#pragma once

// The battle enemies -- the actors on the other side of a section-14 encounter.
// s14_e012 fields two of type 0x80 and three of type 0x8A; every other enemy
// type in the game is built to the same shape.
//
// Each one is a wrapper plus a state table:
//
//   src/FUN_0027f288.c   type 0x80's entry in the primary dispatch table
//   src/FUN_0027f4b0.c   its action check: take the actor record's pending
//                        byte, dispatch it, and clear it -- or, when there is
//                        none and the record is not already busy, run the idle
//                        default
//   src/FUN_0027f5c0.c   that idle default: aim at the target and wait
//   src/FUN_0027f5c8.c   the action dispatcher proper (actions 1..8)
//   src/FUN_0027f978.c   state 0, the one-shot init
//   src/FUN_0027fa88.c   state 1, turn toward +0x19C
//   src/FUN_00280850.c   the four-bone idle wobble, run after the state
//   0x00325970           PTR_FUN_00325970, its nine state handlers
//
//   src/FUN_0028a958.c   type 0x8A's entry, and FUN_0028ab28 / FUN_0028ac38 /
//   src/FUN_0028ae10.c   FUN_0028ac40 / FUN_0028ae10 / FUN_0028af28 behind it,
//   src/FUN_0028af28.c   which are the same five functions with different
//   0x00325B40           animation numbers and a twenty-entry state table
//
// ------------------------------------------------------------- what it is for
//
// **This is where an enemy's facing comes from.** The placement record gives it
// one at spawn (`+0x0C * 45 degrees + 90`, FUN_0025e7c0), state 0 copies that
// into +0x19C, and then FUN_0027f5c0 -- which runs on the first frame the
// record is idle, with or without a battle script driving it -- overwrites
// +0x19C with the angle to the actor record's target. **With no target that is
// pool slot 0**, because FUN_0023a958 falls back to `&DAT_0058beb0` and
// FUN_0023a480 reads DAT_0058bed0/DAT_0058bed4 outright. So an enemy standing
// in an arena with nothing else happening still turns to face Orphen, and holds
// that facing while a randomised 100..199-tick timer runs down.
//
// Nothing here needs the PTR_LAB_0031d118 battle VM. The VM's job is to write
// the record's pending action byte; when it writes nothing, the idle default is
// what the enemy does, and it is the whole of the behaviour the port was
// missing.
//
// ------------------------------------------------------------ the record bias
//
// FUN_0023f8b8 returns the actor record **plus 0x0C**, and that is what lands in
// entity +0x198. Every offset an enemy uses off it is biased by that: `+2` is
// the record's +0x0E, `+3` its +0x0F, `+0x20` its +0x2C and `+0x2C` its +0x38.
// ActorEnvironment::BattleActorView names them by the record's own offsets.

#include "ported/entity/actor_frame_update.h"
#include "ported/entity/original_entity.h"

#include <cstddef>

namespace orphen::ported::entity
{

  // FUN_0027f288 (0x0027f288), type 0x80.
  void FUN_0027f288_enemy80(OriginalEntity &entity,
                            std::size_t slot,
                            const ActorEnvironment &environment,
                            ActorTrace &trace);

  // FUN_0028a958 (0x0028a958), type 0x8A.
  void FUN_0028a958_enemy8a(OriginalEntity &entity,
                            std::size_t slot,
                            const ActorEnvironment &environment,
                            ActorTrace &trace);

} // namespace orphen::ported::entity

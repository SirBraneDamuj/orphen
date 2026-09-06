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
// None of that needs the PTR_LAB_0031d118 battle VM. The VM's job is to write
// the record's pending action byte; when it writes nothing, the idle default is
// what the enemy does.
//
// ----------------------------------------------------------- and what it does
//
// When the VM *is* running -- BattleEncounter::FUN_0023fd30_step_actor_scripts
// -- the pending byte arrives and the action table runs instead. s14_e012's
// five AI scripts are five overlapping tails of one body, and between them they
// only ever ask for three of the eight actions:
//
//   6  go idle and face the target -- the same code as the idle default
//   2  close: type 0x80 leaps along a Bezier arc that ends two units past the
//      target; type 0x8A, which is rooted, simply bites where it stands
//   4  strike: 0x80 lunges to a point two units *short* of the target and
//      slams down; 0x8A winds up for 100 ticks and spits
//
// The handshake between the two halves is entity +0x198's +0x2C -- record
// +0x38 bit 0. FUN_00244248 refuses a new order while it is set and the current
// action is not 6; the attack states hold it up; and each type releases it in
// its own place -- 0x80 when state 6 has walked it home to its spawn point,
// 0x8A when the attack animation comes round with nothing left in flight. The
// script's own `gate` opcode then waits for the current action byte to be 6
// again before it asks for the next one, which is why enemies take turns.
//
// **The damage an enemy deals is not here.** FUN_002ebde0, FUN_002ebad8,
// FUN_002ec920, FUN_002ecc68 and FUN_00280698 are the five calls that would
// spawn the hit volume; each attack plays through to its damage frame, counts
// itself in ActorTrace::recordEnemyAttackHit, and lands on nobody.
//
// ------------------------------------------------------------- taking damage
//
// Damage *to* an enemy arrives as +0xBE, and each wrapper drains it against
// +0x12A before it dispatches: survived goes to the stagger, killed to the
// death. The two types spell those states differently -- 0x80 uses 8 and 7,
// 0x8A uses 6 and 5 -- and 0x80 makes much more of it, because it has a spawn
// point to be knocked back to:
//
//   8  FUN_00280628, the reel. Holds the busy bit and hands over to
//   -> FUN_00280728, which publishes current action 8 and lays a Bezier home
//   -> 5  FUN_00280288, the flight, which releases the busy bit as it lands
//   7  FUN_00280560, the death: the cue and the 0.001 nudge on the clip that
//      carries them, then +0x04 bit 0 -- fade and free -- on the one that does
//      not
//
//   6  FUN_0028b698, the reel: hold the bit, key the cue, and on the frame the
//      clip ends release the bit and go straight back to state 1
//   5  FUN_0028b568, the death: latch, count 0x3C0 ticks of corpse, then fade
//      and free. It deliberately does *not* release the busy bit for a plain
//      Maneater -- only one grown by FUN_0028b740's spit does -- but the record
//      unbinds when the entity is freed, so the fight moves on either way
//
// Without state 8 an enemy hit mid-attack sat in it forever, holding both the
// busy bit and its script's gate: the freeze that pulled this block in.
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

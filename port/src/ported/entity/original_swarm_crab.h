#pragma once

// The small crab -- type 0x7E, the swarm the boss's corpse lets out.
//
//   src/FUN_00276c30.c   the wrapper, in the secondary dispatch table
//   src/FUN_00276d50.c   the action check: read the actor record's pending
//                        byte, act on 10 and 11, clear everything else
//   0x00325868           PTR_FUN_00325868, its state handlers
//
// It is built exactly like the other two battle enemies -- a wrapper, an action
// check, a state table -- and its state 0 is `FUN_0027F978` line for line, down
// to the three `FUN_00216078` calls that fill its own attack bank. What it does
// with the states is different: it has no battle-AI orders to wait on beyond
// "stop" and "hold", and it spends its whole life on a two-mark wander with a
// leap at the end.
//
//   0  FUN_00276DE0  init: the stat record, the three attack records into
//                    DAT_00573778, the actor bind, and a size scale rolled
//                    between 2.00 and 2.99
//   1  FUN_00276F50  walk to the mark at +0x3C/+0x40 at 10..29, then state 2
//   2  FUN_00277110  roll a fresh mark -- x in -11..-6, z in 3.2..4.8, which is
//                    the far end of the beach -- walk to it, then state 3
//   3  FUN_00277410  mill about: a random heading held 50..149 beats, a one in
//                    a hundred chance a frame of a little hop, and one in ten of
//                    those of a bubble. Drifts its mark 0.05 closer to the
//                    player every time the hold runs out and it is more than a
//                    unit away. **This is the state FUN_0027B918 wakes one of.**
//   4  FUN_00277860  the flinch: hold +0x62 down to zero, then back to state 2
//   5  LAB_00277828  the death. Thirteen instructions, no `src/` file: once the
//                    clip has run it raises +0x06 bit 0x10 and +0x04 bit 0x800,
//                    which is the fade path FUN_0023A568 takes instead of this
//                    handler from the next frame on
//   6  FUN_002778B0  the leap. Three phases on +0x94: close to a unit and a half
//                    of the player, then cue 0x11C and build a quadratic Bezier
//                    from where it stands to 0.3 short of him by way of a point
//                    2.5 above him, then walk that arc over 0xA00 ticks with its
//                    pitch swept a quarter turn. A wall (+0x0C bit 1) resets the
//                    arc timer rather than ending it.
//
// **State 5 is a `LAB_`, and Ghidra has no function there.** It is the four
// halfwords at 0x00277828 and it reads:
//
//   if (entity[0x94] != 0) return;
//   if ((entity[0x06] & 1) == 0) return;
//   entity[0x06] |= 0x10;
//   entity[0x04] |= 0x800;
//
// ------------------------------------------------------------- where it comes
//
// Nothing spawns a 0x7E from a placement. `FUN_0027C950` -- the crab's state 11,
// its death -- allocates a hundred of them around the corpse and numbers them
// off in +0x95, and `FUN_0027BA20` (state 15) wakes one to five of them at a
// time on a 300..599 beat timer by putting them into state 6. So the swarm only
// ever exists after the boss is dead, and every one of them starts by wandering
// to the far end of the beach before it comes back.

#include "ported/entity/actor_frame_update.h"
#include "ported/entity/original_enemy_attack.h"
#include "ported/entity/original_entity.h"

#include <cstddef>
#include <cstdint>

namespace orphen::ported::entity
{

  inline constexpr std::int32_t kSwarmCrabTypeId = 0x7E;
  inline constexpr std::uint32_t kFUN_00276c30_swarmCrab = 0x00276C30;

  // PTR_FUN_00325868. Seven entries; the eighth word is zero, so the table ends
  // where the crab's own state table does not.
  inline constexpr std::uint32_t kPTR_FUN_00325868_swarmStates = 0x00325868;
  inline constexpr std::size_t kSwarmStateCount = 7;

  // DAT_00573778 / 7C / 80 -- this type's three attack records, filled by state
  // 0. Record 0 is the one the leap sweeps its body box with.
  EnemyAttackRecords &DAT_00573778_swarmAttacks();

  // FUN_00276c30 (0x00276c30), type 0x7E.
  void FUN_00276c30_swarm_crab(OriginalEntity &entity,
                               std::size_t slot,
                               const ActorEnvironment &environment,
                               ActorTrace &trace);

} // namespace orphen::ported::entity

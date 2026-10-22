#pragma once

// Type 0x93, the s14_e004 tentacle monster -- the thing that rises out of the
// gear room's floor in the intermission and then fights alongside the three
// type 0x8B.
//
//   src/FUN_00296030.c   its entry in the primary dispatch table
//   src/FUN_00297348.c   the action check: the record's pending byte -> state
//   0x00325C80           PTR_FUN_00325c80, seventeen state handlers:
//
//      0, 1  FUN_00296218  init: stats, attack records, bind, path, cursor
//      2     FUN_00296398  the grab: lunge at the target, seize, hold, drop
//      3     FUN_002968c8  a drain that heals the weakest ally -- unreachable:
//                          FUN_00297348 folds actions 2..4 into 2, and nothing
//                          else writes state 3
//      4, 5  FUN_00296338  play animation 6 once, then go idle
//      6     FUN_00296c88  idle: when the animation comes round, rejoin the path
//      8     FUN_00296dc8  fly back to where the path is now
//      12    FUN_00297088  hit: take the damage, flinch, rejoin the path
//      13    FUN_00297148  death: fade over ~124 frames and free itself
//      14    FUN_00297200  ride the path
//      7, 9, 10, 11, 15, 16 are `jr ra; move v0, zero` -- verified in memory
//
// ------------------------------------------------------------------ the path
//
// **It has no home spot; it has a lap.** State 0 asks FUN_002475b8 for the
// path the encounter ships for (type 0x93, +0x95), and from then on its rest
// state is 14, which every frame writes the whole step to the path's current
// point into +0x30/+0x34/+0x38 and advances +0x1C0 by the frame's ticks. In
// s14_e004 that is a 13-point loop round the room at about head height, 0x6900
// ticks -- 840 frames -- a lap. States 2 and 12 knock it off the path, and
// state 8 flies it back to wherever the lap has got to.
//
// ----------------------------------------------------------------- the grab
//
// Action 2, the only order its AI script gives besides 6. It waits until it is
// facing the target within 30 degrees (the wrapper turns it 4.8 degrees a
// frame), flies at it on animation 2, then plays 0x13. If the target is within
// 2.0, not already held and not riding anything, it snaps onto the target's
// x/y, raises the victim's +0x96 bit 2 and makes it a child of its own bone 20
// (+0x192 / +0x194) for the length of animation 0x12. On release the victim is
// put back at bone 18 and FUN_00216128 charges attack record 0. Its own +0x04
// bit 0x10 is up while it holds, which is what the wrapper's first lines read
// as "drop any damage taken".

#include "ported/entity/actor_frame_update.h"
#include "ported/entity/actor_trace.h"
#include "ported/entity/original_entity.h"

#include <cstddef>

namespace orphen::ported::entity
{

  // FUN_00296030 (0x00296030), type 0x93.
  void FUN_00296030_tentacle93(OriginalEntity &entity,
                               std::size_t slot,
                               const ActorEnvironment &environment,
                               ActorTrace &trace);

} // namespace orphen::ported::entity

#pragma once

// Type 0x37, the party follower -- the behaviour that makes Cleo and Magnus
// walk after Orphen once a cutscene has bound them into party slots.
//
//   src/FUN_00258ab8.c   the type's entry in the primary dispatch table, which
//                        is a freeze gate, a hit reaction, and a second
//                        dispatch on +0x60 through PTR_FUN_0031e1a0
//   src/FUN_002596c8.c   state 0, the one-shot init
//   src/FUN_002597d0.c   state 1, idle: look at the lead, then decide whether
//                        to start walking. Shared with the *player's* table
//                        (PTR_LAB_0031e1d0 state 1), which is why it keeps
//                        testing `type == 0x37`
//   src/FUN_00259d00     state 2, turn to a chosen angle then push through
//                        (not in src/; disassembled)
//   src/FUN_00259e50.c   state 3, turn in place
//   src/FUN_0025a450.c   state 7, walk blind on the current facing for a while
//   src/FUN_0025a500.c   state 8, the follow walk itself
//   src/FUN_0025aa48.c   state 9, sidestep away from a crowd
//   0x0025ab48           state 10, wait for the floor after a stagger
//   src/FUN_00259520.c   where the formation spot beside the lead comes from
//
//   src/FUN_00259ec0.c   state 4, walking the navigation graph
//   src/FUN_0025a298.c   state 6, the bail-out: teleport to a breadcrumb when
//                        off camera, otherwise retry the pathfinder
//   src/FUN_00259378.c   the pathfinder itself; see ported/entity/
//                        follower_navmesh.* for the graph behind it
//
// **State 5 is dead code in the retail build.** FUN_00259378 only reaches it
// through FUN_00258b80, and FUN_00258b80's body is a 511-iteration loop over
// the breadcrumb ring whose result is discarded, followed by an unconditional
// `addiu $v0, $zero, -1`. Ghidra shows the same thing ("Removing unreachable
// block"). The -1 always sends the follower to state 6 instead, so
// FUN_0025a0c8 is the one part of this subsystem left unported -- deliberately,
// because nothing can call it.

#include "ported/entity/actor_frame_update.h"
#include "ported/entity/actor_trace.h"
#include "ported/entity/original_entity.h"

#include <cstdint>

namespace orphen::ported::entity
{

  // FUN_00258ab8. Runs the freeze gate, the hit reaction and one state.
  void FUN_00258ab8_party_follower(OriginalEntity &entity,
                                   const ActorEnvironment &environment,
                                   ActorTrace &trace);

  // PTR_FUN_0031e1a0, the eleven state handlers. Exposed so the trace can name
  // the one it could not run.
  inline constexpr std::uint32_t kPTR_FUN_0031e1a0_followerStates = 0x0031E1A0;
  inline constexpr std::size_t kFollowerStateCount = 12;

} // namespace orphen::ported::entity

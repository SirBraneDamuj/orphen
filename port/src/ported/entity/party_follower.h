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
// States 4, 5 and 6 are the recovery paths -- navmesh cell walking
// (FUN_00259ec0 / FUN_00259378 / FUN_00258c70) and the "teleport to a nearby
// waypoint" bail-out (FUN_0025a0c8 / FUN_0025a298). They read the map's
// collision cell graph and its waypoint array, neither of which the port
// publishes, so they are left to the ActorTrace to report rather than guessed
// at. A follower only reaches them when the flat-ground path has already given
// up.

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

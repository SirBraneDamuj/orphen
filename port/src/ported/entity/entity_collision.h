#pragma once

// Entity-vs-entity collision: the four pool-walking clamps FUN_002262c0 runs
// once it has a movement request to spend.
//
//   src/FUN_00228380.c   +X   called when the request's X is positive
//   src/FUN_002285d8.c   -X   ... negative
//   src/FUN_00228838.c   +Y   called when the request's Y is positive
//   src/FUN_00228a90.c   -Y   ... negative
//
// They are **clamps, not resolvers**. Each narrows how far the entity is
// allowed to travel this frame so it stops flush against the first blocker in
// its path; none of them ever pushes the moving entity out of an overlap. What
// they *do* push is the *other* entity -- a shove of 0.02 along the mover's
// heading, applied only when the two are already touching. So a character walks
// into a crate and stops, and the crate drifts.
//
// Driven from FUN_002262c0 at 0x002267C4/E4/0x00226800/0x00226820, gated on the
// request being non-zero on that axis. The whole solve is reached through
// FUN_002261e0, which is a walk of all 256 pool slots -- not a per-behaviour
// call -- so this applies to every live entity, including ones whose type
// handler is FUN_00239e78's `jr ra; nop`.

#include "ported/entity/entity_pool.h"
#include "ported/psm2/psm2_runtime.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

namespace orphen::ported::entity
{

  // Runs whichever of the four apply to this entity's current +0x30/+0x34, in
  // FUN_002262c0's order (X axis first, then Y), and writes the narrowed
  // request back into the entity. Also clears and maybe sets +0x64.
  //
  // A no-op when the entity is not asking to move, which is the common case.
  void FUN_002262c0_clamp_movement_against_entities(EntityPool &pool, std::size_t slot);

  // The same four clamps on one candidate step rather than on +0x30/+0x34.
  // FUN_002262c0 runs them at the top of every pass of its retry loop, against
  // the workspace step (+0x140/+0x144) that pass is about to try, while the
  // shove heading stays the request's original one (+0x154). Does not touch
  // +0x64; the caller clears it once per solve.
  void FUN_002262c0_clamp_step_against_entities(EntityPool &pool, std::size_t slot, float heading,
                                                float &stepX, float &stepZ);

  // Counters so a run can say whether any of this executed. Dead collision code
  // and correct collision code look identical from the outside when nothing in
  // the scene happens to be in anyone's way.
  struct EntityCollisionStats
  {
    std::uint32_t sweeps = 0; // entities that asked to move and walked the pool
    std::uint32_t clamps = 0; // times a request was narrowed by a blocker
    std::uint32_t shoves = 0; // times a blocker was nudged along the mover
  };
  const EntityCollisionStats &entityCollisionStats();
  void resetEntityCollisionStats();

  // FUN_002262c0:41-91, how an actor rides a moving collision group -- a lift.
  //
  // It runs ahead of the freeze test and gravity, and only while the primitive
  // the actor last stood on (+0x0A) is flagged at +0x13 and belongs to a group
  // (+0x14) whose dirty byte is live. Then it resamples the floor with
  // FUN_00227070 where the actor stands and moves +0x4C to it. A floor that
  // dropped away takes a settled actor down with it when the drop is under
  // DAT_00352424 (0.3); a floor that rose lifts the actor's +0x28 onto it, but
  // only while the sample still landed on the same group.
  //
  // `FUN_00227070_sample` is the caller's floor sample at the actor's own
  // position. It returns the height, or nothing for the 128 sentinel, and it
  // must leave +0x0A (and the terrain words) as FUN_00227070 does, because the
  // rising branch re-reads +0x0A after it.
  void FUN_002262c0_ride_collision_group(OriginalEntity &entity,
                                         const orphen::ported::psm2::Psm2RuntimeState &map,
                                         const std::function<std::optional<float>()> &FUN_00227070_sample);

} // namespace orphen::ported::entity

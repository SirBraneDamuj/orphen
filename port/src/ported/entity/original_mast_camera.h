#pragma once

// FUN_00298160 (0x00298160), the s14_e002 boss camera director.
//
// Source map:
//   src/FUN_00298160.c   the whole thing
//
// It is the same machine as FUN_00277d30 (original_boss_camera.h) built for a
// different fight: one function, twelve numbered shots plus a release, a
// priority gate, and a latch that installs its own manual camera the first time
// it runs after a release. The state block is five words at
// 0x00355310..0x00355324 rather than the crab's nine, and the shots are poses
// off the player, off the boss, off a bone, or off a curve.
//
//   DAT_00355310  the installed latch
//   DAT_00355314  the shot
//   DAT_00355318  the priority
//   DAT_0035531C  the sub-shot
//   DAT_00355320  the sub-shot the last one-shot setup was built for
//   DAT_00355324  the running angle shots 4, 7 and 10 sweep
//
// ------------------------------------------------------------ what is here
//
// **All twelve shots and the release.** The six the intro and the orbit passes
// reach (1, 2, 5, 9, 11 and the release) went in first; 3, 4, 6, 7, 8, 10 and
// 12 followed with the fight states that ask for them.
//
// Three things about the set are worth knowing before reading the switch:
//
// **DAT_0058B190 is an anchor, not the eye.** Eight of the twelve stamp a
// point there on the frame their sub-shot changes -- the boss's position, a
// bone, a table row -- and build every later frame's pose off it, which is
// what makes a shot a fixed frame the creature flies through rather than a
// follow cam. Shots 1, 2, 3 and 12 never touch it.
//
// **Shots 6 and 8 are camera paths, not poses.** Each installs a spline
// through FUN_00217E88 -- an eye curve and a one-point look-at curve, no
// roll/zoom curve at all -- and walks it on its own DAT_0035532E counter.
// 6 authors its four points in the boss's bone 8 space and rotates them by the
// boss's facing; 8 authors three points as a plain offset from a table row and
// does not rotate. When the counter runs out both hold the curve's last point.
//
// **Shots 1, 2, 3 and 5 turn the player.** They write DAT_0058BF0C -- pool
// slot 0's +0x5C -- to the bearing from the player to the boss before they
// place the eye. That is the director's job in this fight, not the player's own
// state machine's, and leaving it out leaves Orphen facing wherever the last
// carry dropped him.
//
// ------------------------------------------------------- what is still out
//
// Nothing structural. Two details are reproduced rather than corrected and are
// marked at their arm:
//
//   - shot 5's limit tests read DAT_00355324, which shot 5 never writes.
//   - shot 8's look-at adds DAT_003556FC twice, once in the anchor and once
//     again on the way out.
//
// FUN_0023BBD8, the pad rumble, is not called from here at all -- the boss
// states are its only callers.
//

#include "ported/entity/actor_frame_update.h"
#include "ported/entity/original_entity.h"

#include <cstdint>

namespace orphen::ported::entity
{

  inline constexpr std::uint32_t kFUN_00298160_mastCamera = 0x00298160;

  // DAT_0035532C, at the top of the director's state block: which side the
  // next shot comes in on, 1 or 2. FUN_0029C468 rerolls it every time the boss
  // picks a move and states 4 and 6 hand it straight back as their sub-shot.
  std::uint8_t &DAT_0035532c_mastCameraSide();

  // FUN_00298160. `target` is the entity the shot is about -- the boss, in
  // every call made so far. A negative `shot` releases the director and ignores
  // every other argument.
  void FUN_00298160_mast_camera(std::int16_t shot,
                                std::int16_t subShot,
                                std::int16_t priority,
                                const OriginalEntity *target,
                                const ActorEnvironment &environment);

} // namespace orphen::ported::entity

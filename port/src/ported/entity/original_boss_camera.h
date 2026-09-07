#pragma once

// FUN_00277d30 (0x00277d30), the boss camera director.
//
// Source map:
//   src/FUN_00277d30.c   the whole thing
//   src/FUN_00279180.c   the crab's own wrapper, which picks mode 1, 2 or 3
//
// One function, fifteen numbered shots, and a priority gate. Every boss
// behaviour that wants the camera calls it with a shot number, a sub-shot, a
// priority and the entity the shot is about; a call whose priority is below the
// one already running is dropped. Mode -1 releases the director. The shots
// themselves are hard-coded poses -- offsets off the player, off the boss, off
// a bone, or a three-point spline out of the data segment -- and every one of
// them ends on FUN_00217d10 (look-at) and FUN_00217d40 (eye).
//
// The director installs *its own* manual camera the first time it is called
// after a release: FUN_00217e18(1) drops whatever camera is there and
// FUN_00217d70(0,0,0, 0,0,0) puts a fresh one at the origin, which the shot
// then moves the same frame. Releasing does not put the field camera back --
// it only clears the latch, so the next call re-installs.
//
// The state block is nine gp words at 0x00355274..0x0035529B, and one of them,
// `uGpffffb31c` at 0x0035528C, is the byte the crab reads as `DAT_0035528C`
// to decide which side its shots come from. Mode 3 -- the orbit -- is what
// writes it: the camera swings around the player until it passes a limit, then
// flips the side for the next shot.
//
// Not ported: `uGpffffb6ec`. FUN_00216968 and script opcodes 0x6A/0x6B write
// it and nothing in `src/` reads it back, so the port has no field for it; the
// three places this function zeroes it are noted where they occur.

#include "ported/entity/actor_frame_update.h"
#include "ported/entity/original_entity.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace orphen::ported::entity
{

  inline constexpr std::uint32_t kFUN_00277d30_bossCamera = 0x00277d30;

  // DAT_00325900 / 04 / 08: the three entities the crab's throw stands up for
  // its cinematic, found by actor tag 0x2E, 0x2F and 0x30. Shots 12 and 13
  // frame the first of them, so they live here rather than in the crab; state
  // 14 is what gives them back. -1 is "none", which is the original's 0.
  //
  // Tag 0x2E is the type 0x28 close-up mount s14_e001 places at load and hides;
  // 0x2F and 0x30 are the bust and hair pool slots opcode 0x13F published.
  std::array<std::int32_t, 3> &DAT_00325900_cinematicSlots();

  // uGpffffb31c, at 0x0035528C -- which is also DAT_0035528C, the byte the
  // crab reads to decide which side its next shot comes from. Shot 3 is the
  // only writer here; FUN_00279940 seeds it to 1 and FUN_0027C7B8 rerolls it.
  std::uint8_t &DAT_0035528c_cameraSide();

  // FUN_00277d30. `target` is the entity the shot is about -- the boss, in
  // every call the crab makes. A negative `mode` releases the director and
  // ignores every other argument.
  void FUN_00277d30_boss_camera(std::int16_t mode,
                                std::int16_t subMode,
                                std::int16_t priority,
                                const OriginalEntity *target,
                                const ActorEnvironment &environment);

} // namespace orphen::ported::entity

#pragma once

// The water splash, type 0x10D, and the two ring spawners that fill it.
//
// Source map:
//   src/FUN_002eb180.c  0x002EB180  the type 0x10D behaviour -- PTR_LAB_0031CAB0
//                                   entry (0x10D - 0xFC), confirmed out of the
//                                   executable
//   src/FUN_002eb278.c  0x002EB278  spawn one, at a point, on a heading
//   src/FUN_002eb398.c  0x002EB398  a ring of them around an entity
//   src/FUN_002eb500.c  0x002EB500  the wading spray, a 90-degree fan behind an
//                                   entity's facing
//
// A 0x10D is a flat quad that sits *on the water surface*: FUN_002eb278 writes
// DAT_00354A3C (-0.7) into its height and takes only x and y from the caller, so
// the spawner never has to know how deep the thing making the splash is. It
// lives for its own timer and shrinks to nothing as that timer runs out -- both
// scales are the spawn scale multiplied by the fraction of the timer left.
//
// The mode byte lands in +0x60, and it is the only difference between the two
// kinds. Mode 0 is a burst: it stays where it was put. Mode 1 is spray: it
// drifts outward along its own heading at ten units per 32000 ticks. The ring
// spawner makes bursts, the wading spawner makes spray.
//
// Two callers in the crab fight, and between them they are most of the water in
// the scene:
//
//   FUN_0027CE48  every 0x140 ticks while the crab is in the water and playing
//                 one of the six clips that move it -- the spray it kicks up
//                 wading in
//   FUN_0027CFE0  when the thrown pair hits the water: one big burst at scale
//                 (3, 5) for 100 ticks and ten small ones at (2, 3) for 50

#include "ported/entity/actor_frame_update.h"
#include "ported/entity/original_entity.h"

#include <cstddef>
#include <cstdint>

namespace orphen::ported::entity
{

  inline constexpr std::int32_t kWaterSplashTypeId = 0x10D;
  inline constexpr std::uint32_t kFUN_002eb180_waterSplash = 0x002EB180;

  // FUN_002EB180. The behaviour: shrink toward the end of the timer, die on
  // zero, and drift outward while the mode byte is non-zero.
  void FUN_002eb180_water_splash(OriginalEntity &entity,
                                 std::size_t slot,
                                 const ActorEnvironment &environment);

  // FUN_002EB398(radius, scaleA, scaleB, at, count, lifeTicks). A ring of
  // `count` bursts at `radius` around the entity, starting from a random angle.
  // A negative `lifeTicks` gives each one its own random life instead.
  void FUN_002eb398_splash_ring(float radius,
                                float scaleA,
                                float scaleB,
                                const OriginalEntity &at,
                                int count,
                                std::int16_t lifeTicks,
                                const ActorEnvironment &environment);

  // FUN_002EB500(at, count). The wading spray: a ninety-degree fan starting
  // 135 degrees off the entity's facing, each one at a random half to one unit
  // out. Does nothing unless the entity is at or below the water line.
  void FUN_002eb500_wade_spray(const OriginalEntity &at,
                               int count,
                               const ActorEnvironment &environment);

} // namespace orphen::ported::entity

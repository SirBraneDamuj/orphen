#pragma once

// The two camera movers FUN_0023C340 uses while the target display is up:
// src/FUN_0023db98.c (0x0023DB98) and src/FUN_0023de20.c (0x0023DE20).
//
// `FUN_0023C340` is the whole battle camera and most of it is out of scope
// here -- the encounter's own spline pairs, the per-action framing at
// `DAT_00355C90`/`C94`/`C98`, and the reaction shots. What *is* ported is the
// branch that runs for the 120 frames after a D-pad step, which is the half the
// player actually drives:
//
//   DAT_00354E96 > 0x780   FUN_0023DB98, mode 6:  swing the look direction onto
//                          the target from behind the player's head
//   DAT_00354E96 <= 0x780  FUN_0023DE20, mode 10: sit 2.5 units off the target
//                          and orbit it slowly
//
// Both are called with the *player* as the frame of reference and the target as
// the subject, and both go through the manual-camera path (`cGpffffb6e1` 0x23),
// so the field camera's follow logic is out of the way while they run.
//
// Ghidra's prototypes for both are shifted, because the first two arguments are
// floats in $f12/$f14 and it numbered from $a0. The real ones, read off the
// call sites at 0x0023C808 and 0x0023C848:
//
//   FUN_0023DB98(entity *target, s16 steps, u8 mode, u8 *work, ..., f12, f13, f14)
//   FUN_0023DE20(entity *target, s16 distanceMilli, u8 mode, u8 *work, f12, f13, f14)
//
// and in `FUN_0023DB98` all three float arguments are dead -- the disassembly
// never reads $f12/$f14 before overwriting them.

#include "ported/camera/original_field_camera.h"
#include "ported/entity/original_entity.h"

#include <cstdint>

namespace orphen::ported::battle
{

  // fGpffff877c and fGpffff8780 (0x3526EC / 0x3526F0), both 2.3. The first is
  // FUN_0023DB98's give-up distance -- inside it the swing reports -1 and
  // FUN_0023C340 drops straight to the hold -- and the second is FUN_0023DE20's
  // "already on top of it" test.
  inline constexpr float kfGpffff877c_swingMinDistance = 2.3f;
  inline constexpr float kfGpffff8780_orbitCloseDistance = 2.3f;
  // fGpffff8784 (0x3526F4), 178 degrees. Added to the entry angle when the
  // player is already inside that distance, so the camera comes round the far
  // side rather than sitting in the player's face.
  inline constexpr float kfGpffff8784_orbitCloseFlip = 3.1066854f;
  // fGpffff8778 (0x3526E8), one degree. The bias on the angle the swing's pivot
  // is placed along.
  inline constexpr float kfGpffff8778_pivotBias = 0.0174533f;

  // DAT_00352668 (0x352668), 0.000192 radians a tick -- about 21 degrees a
  // second at the nominal 0x20 ticks. The hold's orbit rate.
  inline constexpr float kDAT_00352668_orbitRate = 0.000192f;
  // The two immediate arguments at 0x0023C808 / 0x0023C848: sixteen steps for
  // the swing, and 2500 thousandths -- 2.5 units -- for the hold's radius.
  inline constexpr std::int16_t kSwingSteps = 0x10;
  inline constexpr std::int16_t kOrbitDistanceMilli = 0x9C4;
  inline constexpr std::uint8_t kSwingMode = 6;
  inline constexpr std::uint8_t kOrbitMode = 10;

  // DAT_00571B80, the 0x70-byte camera workspace FUN_0023C340 clears with
  // FUN_00267E78 when it picks a new spline. Only the four fields the two
  // movers touch are modelled; the rest belongs to the parts of the battle
  // camera that are not ported.
  struct TargetCameraWork
  {
    // +0x00. The mode byte, and the first half of "is this request new". Both
    // movers write it: FUN_0023DB98 always clears it, so leaving the swing and
    // coming back to the orbit re-arms the orbit.
    std::uint8_t byte00_mode = 0;
    // +0x2C, read as a short against 1. 1 counts the orbit angle down, 2 up.
    std::uint16_t half2c_direction = 0;
    // +0x04. The orbit angle, in radians about the target.
    float float04_angle = 0.0f;
    // +0x34. The entity the mode was armed for -- the original keeps the
    // pointer, the port keeps the pool slot. A different target re-arms.
    std::int32_t int34_target = -1;

    // FUN_00267E78(0x571b80, 0x70).
    void FUN_00267e78_clear() { *this = TargetCameraWork{}; }
  };

  // FUN_0023DB98. Puts the eye half a unit behind the player's head and turns
  // the look point one `steps`-th of the way onto the target, so a repeated
  // call converges. Returns -1 without moving the camera when the target is
  // closer than 2.3 units, which is FUN_0023C340's signal to skip the swing.
  std::int32_t FUN_0023db98_swing_look_to_target(
      orphen::ported::camera::OriginalFieldCamera &camera,
      const orphen::ported::entity::OriginalEntity &player,
      const orphen::ported::entity::OriginalEntity &target,
      std::int16_t steps,
      TargetCameraWork &work);

  // FUN_0023DE20. Installs a manual camera `distanceMilli / 1000` units from
  // the target, at one and a half times the player's height, looking at the
  // target's middle, and walks the angle by `rate` a tick. The direction is
  // chosen once, when the request is new, from which side of the player the
  // target is on.
  void FUN_0023de20_orbit_target(orphen::ported::camera::OriginalFieldCamera &camera,
                                 const orphen::ported::entity::OriginalEntity &player,
                                 const orphen::ported::entity::OriginalEntity &target,
                                 std::int32_t targetSlot,
                                 float entryOffset,
                                 float rate,
                                 std::int16_t distanceMilli,
                                 std::uint8_t mode,
                                 TargetCameraWork &work,
                                 std::uint32_t frameTicks);

} // namespace orphen::ported::battle

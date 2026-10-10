#include "ported/battle/battle_camera.h"

#include "ported/model/psc3_skeleton.h"

#include <cmath>

namespace orphen::ported::battle
{
  namespace
  {
    using orphen::ported::psm2::Vec3;

    // FUN_00216608 and FUN_00216648: the two and three component lengths.
    float FUN_00216608_length(float x, float y) { return std::sqrt(x * x + y * y); }
    float FUN_00216648_length3(float x, float y, float z)
    {
      return std::sqrt(x * x + y * y + z * z);
    }

    // FUN_002166e8(a, b) is FUN_00216690(b - a): the signed difference, wrapped
    // into (-pi, pi]. Note the argument order -- it is the *second* minus the
    // first, which is easy to get backwards.
    float FUN_002166e8_angle_difference(float from, float to)
    {
      return orphen::ported::model::FUN_00216690_wrap_angle(to - from);
    }

    // The entity fields both movers read, by their PS2 offsets.
    Vec3 position(const orphen::ported::entity::OriginalEntity &entity)
    {
      return {entity.positionX20, entity.positionZ24, entity.positionY28};
    }
  } // namespace

  std::int32_t FUN_0023db98_swing_look_to_target(
      orphen::ported::camera::OriginalFieldCamera &camera,
      const orphen::ported::entity::OriginalEntity &player,
      const orphen::ported::entity::OriginalEntity &target,
      std::int16_t steps,
      TargetCameraWork &work)
  {
    const float divisor = static_cast<float>(steps);
    const Vec3 playerAt = position(player);
    const Vec3 targetAt = position(target);
    const Vec3 lookAt = camera.DAT_0058be90_lookAt();

    // 0x0023DC04-0x0023DC4C. The pivot: half a unit from the player, on the
    // side away from the target, at the player's full height. That is roughly
    // where the head is, and it is where the eye goes.
    const float pivotAngle = std::atan2(playerAt.y - targetAt.y, playerAt.x - targetAt.x) +
                             kfGpffff8778_pivotBias;
    const float pivotX = playerAt.x - 0.5f * std::cos(pivotAngle);
    const float pivotY = playerAt.y - 0.5f * std::sin(pivotAngle);
    const float pivotZ = playerAt.z + player.height58;

    // 0x0023DC84-0x0023DCB4. One step of the height, toward the target's
    // middle. The matching x/y interpolation the same block computes is dead --
    // 0x0023DD4C and 0x0023DD80 overwrite both stack slots with the polar
    // result below -- so only the angle and the height actually move.
    const float lookZStep =
        ((targetAt.z + target.height58 * 0.5f) - lookAt.z) / divisor;

    // 0x0023DCDC-0x0023DD38. Turn the look point about the pivot: one
    // `steps`-th of the difference between where it points now and where the
    // target is, so a run of frames converges on the target rather than
    // snapping to it.
    const float currentAngle = std::atan2(lookAt.y - pivotY, lookAt.x - pivotX);
    const float desiredAngle = std::atan2(targetAt.y - pivotY, targetAt.x - pivotX);
    const float delta = FUN_002166e8_angle_difference(currentAngle, desiredAngle);

    // The radius is the *player*-to-target distance, not the pivot's, so the
    // look point lands beyond the target rather than on it.
    const float radius =
        FUN_00216608_length(targetAt.x - playerAt.x, targetAt.y - playerAt.y);
    const float newAngle = currentAngle + delta / divisor;

    const Vec3 look{pivotX + radius * std::cos(newAngle),
                    pivotY + radius * std::sin(newAngle),
                    lookAt.z + lookZStep};

    // 0x0023DDA4-0x0023DDC8. Too close to frame from behind the player's head:
    // report it and leave the camera alone. FUN_0023C340 answers by dropping
    // the display timer to 0x780, which is the hold.
    const float distance = FUN_00216648_length3(targetAt.x - playerAt.x,
                                                targetAt.y - playerAt.y,
                                                targetAt.z - playerAt.z);
    if (distance <= kfGpffff877c_swingMinDistance)
    {
      work.byte00_mode = 0;
      return -1;
    }

    camera.FUN_00217d40_set_eye({pivotX, pivotY, pivotZ});
    camera.FUN_00217d10_set_look_at(look);
    work.byte00_mode = 0;
    return 0;
  }

  void FUN_0023de20_orbit_target(orphen::ported::camera::OriginalFieldCamera &camera,
                                 const orphen::ported::entity::OriginalEntity &player,
                                 const orphen::ported::entity::OriginalEntity &target,
                                 std::int32_t targetSlot,
                                 float entryOffset,
                                 float rate,
                                 std::int16_t distanceMilli,
                                 std::uint8_t mode,
                                 TargetCameraWork &work,
                                 std::uint32_t frameTicks)
  {
    const Vec3 playerAt = position(player);
    const Vec3 targetAt = position(target);

    // :3-30. A new request -- a different mode, or the same mode on a different
    // entity -- picks the entry angle and the direction of travel once.
    if (mode != work.byte00_mode || work.int34_target != targetSlot)
    {
      const float horizontal =
          FUN_00216608_length(targetAt.x - playerAt.x, targetAt.y - playerAt.y);
      work.byte00_mode = mode;
      work.int34_target = targetSlot;

      // Start from the target's own line to the player, so the shot opens over
      // the player's shoulder rather than from an arbitrary bearing.
      float angle = std::atan2(playerAt.y - targetAt.y, playerAt.x - targetAt.x);
      const float side = FUN_002166e8_angle_difference(player.facingRadians5c, angle);
      if (horizontal < kfGpffff8780_orbitCloseDistance)
      {
        // Already on top of the target: come round the far side instead.
        angle += kfGpffff8784_orbitCloseFlip;
      }

      // Which way the target lies off the player's facing decides which way the
      // orbit runs, so the camera always sweeps *past* the player rather than
      // away behind them.
      if (side > 0.0f)
      {
        work.half2c_direction = 1;
        angle = angle - entryOffset;
      }
      else
      {
        work.half2c_direction = 2;
        angle = angle + entryOffset;
      }
      work.float04_angle = angle;

      // :31-35. cGpffffb656 overrides the entry angle with the bearing from
      // DAT_0031D0A8/AC. Nothing in the port sets that flag, so the branch is
      // named rather than written.
    }

    // :37-46.
    work.float04_angle = orphen::ported::model::FUN_00216690_wrap_angle(work.float04_angle);
    const float step = rate * static_cast<float>(frameTicks);
    work.float04_angle += work.half2c_direction == 1 ? -step : step;

    const float radius = static_cast<float>(distanceMilli) / 1000.0f;
    const Vec3 eye{targetAt.x + radius * std::cos(work.float04_angle),
                   targetAt.y + radius * std::sin(work.float04_angle),
                   playerAt.z + player.height58 * 1.5f};
    const Vec3 look{targetAt.x, targetAt.y, targetAt.z + target.height58 * 0.5f};

    // FUN_00246F40 is FUN_00217E18(0) then FUN_00217D70, so every frame drops
    // the installed manual camera and puts a fresh one in its place. Without
    // the release the second call would be refused.
    camera.FUN_00217e18_release_manual_camera(false);
    camera.FUN_00217d70_set_manual_camera(eye, look);
  }

  bool FUN_0023e790_close_up(orphen::ported::camera::OriginalFieldCamera &camera,
                             const orphen::ported::entity::OriginalEntity &subject,
                             float angle,
                             float rate,
                             std::int16_t steps,
                             std::uint8_t mode,
                             TargetCameraWork &work,
                             std::int32_t DAT_003551f8_sceneEntry,
                             float DAT_0058bf0c_leadFacing,
                             std::int32_t frameTicks)
  {
    // DAT_0031D0F8/FC and DAT_0031D108/10C: the two arena points the angle is
    // aimed from in section-14 entries 0x11 and 0x29.
    constexpr float kDAT_0031d0f8_x = -1.9500000476837158f;
    constexpr float kDAT_0031d0fc_y = 10.71500015258789f;
    constexpr float kDAT_0031d108_x = -6.2769999504089355f;
    constexpr float kDAT_0031d10c_y = 5.203999996185303f;

    // 0x0023E7DC-0x0023E89C. A new mode arms the workspace.
    if (static_cast<std::int8_t>(work.byte00_mode) != static_cast<std::int8_t>(mode))
    {
      work.byte00_mode = mode;
      work.float04_angle = angle;
      work.float14_facing = subject.facingRadians5c;
      work.half2c_direction = static_cast<std::uint16_t>(subject.typeId00);
      work.float18_playerFacing = DAT_0058bf0c_leadFacing;
      work.float20_x = subject.positionX20;
      work.float24_y = subject.positionZ24;
      work.float28_z = subject.positionY28;
      work.float0c_lookHeight = subject.positionY28;
      if (DAT_003551f8_sceneEntry == 0x11)
      {
        work.float04_angle = std::atan2(kDAT_0031d0fc_y - subject.positionZ24, kDAT_0031d0f8_x - subject.positionX20);
        work.float14_facing = 0.0f;
      }
      if (DAT_003551f8_sceneEntry == 0x29)
      {
        work.float04_angle = std::atan2(kDAT_0031d10c_y - subject.positionZ24, kDAT_0031d108_x - subject.positionX20);
        work.float14_facing = 0.0f;
      }
    }

    // 0x0023E8A0-0x0023E8EC. The divisor is `steps * (ticks / 32)` through a
    // short, and the plain step count when that comes out zero.
    work.float04_angle = orphen::ported::model::FUN_00216690_wrap_angle(work.float04_angle);
    const std::int32_t whole = (frameTicks < 0 ? frameTicks + 0x1F : frameTicks) >> 5;
    const auto scaled = static_cast<std::int16_t>(static_cast<std::int32_t>(steps) * whole);
    const float divisor = static_cast<float>(scaled != 0 ? scaled : steps);

    // 0x0023E8F0-0x0023E9C8.
    const Vec3 eye = camera.DAT_0058c0a8_eye();
    const float wantX = std::cos(work.float14_facing + work.float04_angle) * 2.5f + work.float20_x;
    const float wantY = std::sin(work.float14_facing + work.float04_angle) * 2.5f + work.float24_y;
    const float wantZ = work.float28_z + 1.0f;
    const float dx = wantX - eye.x;
    const float dy = wantY - eye.y;
    const float dz = wantZ - eye.z;
    const float distance = FUN_00216648_length3(dx, dy, dz);
    Vec3 nextEye{dx / divisor + eye.x, dy / divisor + eye.y, dz / divisor + eye.z};

    // 0x0023E9CC-0x0023EA24. Mode 7 orbits: the angle moves on and x/y snap to
    // the circle, leaving only the height eased.
    if (mode == 7)
    {
      work.float04_angle += rate * static_cast<float>(frameTicks);
      nextEye.x = std::cos(work.float14_facing + work.float04_angle) * 2.5f + work.float20_x;
      nextEye.y = std::sin(work.float14_facing + work.float04_angle) * 2.5f + work.float24_y;
    }

    // 0x0023EA28-0x0023EA40. The look height follows the subject only while it
    // is still the type it was armed on.
    if (subject.typeId00 == static_cast<std::int16_t>(work.half2c_direction))
    {
      work.float0c_lookHeight = subject.positionY28;
    }

    // FUN_00246F40: FUN_00217E18(0), then FUN_00217D70(eye, look).
    camera.FUN_00217e18_release_manual_camera(false);
    camera.FUN_00217d70_set_manual_camera(nextEye, Vec3{work.float20_x, work.float24_y, work.float0c_lookHeight + 0.5f});
    return kDAT_00352708_arrivedLow < distance && distance < kDAT_0035270c_arrivedHigh;
  }

} // namespace orphen::ported::battle

#include "ported/camera/original_field_camera.h"

#include "ported/original_frame_timing.h"

#include <cmath>

namespace orphen::ported::camera
{
  namespace
  {

    // Camera constant block, DAT_00352194..DAT_00352260. Values read out of
    // eeMemory.bin; almost all are round degree counts. See the table in
    // analyzed/update_field_camera.c.
    constexpr float fGpffff8224_defaultPitch = 0.36651909f;    // 21 deg.
    constexpr float fGpffff8228_eyeHeightOffset = -0.2f;
    constexpr float fGpffff8234_freeLookRelease = 0.1f;
    constexpr float iGpffff8230_targetHeight = 0.8f;
    constexpr float iGpffff8238_defaultYawAccel = 0.0043633222f; // 0.25 deg.
    constexpr float iGpffff823c_defaultYawMax = 0.069813155f;    // 4 deg.
    constexpr float fGpffff8240_yawDeadzoneLow = 1.0471973f;     // 60 deg.
    constexpr float fGpffff8244_yawDeadzoneHigh = 2.0943947f;    // 120 deg.

    // Target-Z follow ladder, negative then positive error.
    constexpr float fGpffff8248 = -0.01f;
    constexpr float fGpffff824c = -0.6f;
    constexpr float fGpffff8250 = -0.02f;
    constexpr float fGpffff8254 = -0.04f;
    constexpr float fGpffff8258 = 0.04f;
    constexpr float fGpffff825c = 0.01f;
    constexpr float fGpffff8260 = 0.6f;
    constexpr float fGpffff8264 = 0.02f;
    constexpr float fGpffff8268 = 0.04f;
    constexpr float fGpffff826c = 0.04f;

    // Eye-Z follow ladder.
    constexpr float fGpffff8270 = -0.02f;
    constexpr float fGpffff8274 = -0.3f;
    constexpr float fGpffff8278 = -0.03f;
    constexpr float fGpffff827c = -0.05f;
    constexpr float fGpffff8280 = 0.05f;
    constexpr float fGpffff8284 = 0.02f;
    constexpr float fGpffff8288 = 0.3f;
    constexpr float fGpffff828c = 0.03f;
    constexpr float fGpffff8290 = 0.05f;
    constexpr float fGpffff8294 = 0.05f;

    constexpr float fGpffff8298_horizontalDeadzone = 0.016f;
    constexpr float fGpffff829c_horizontalAccel = 0.04f;
    constexpr float fGpffff82a0_horizontalDecel = 0.08f;

    constexpr float fGpffff82a4_mode1YawAccel = 0.0065449835f; // 0.375 deg.
    constexpr float fGpffff82a8_mode1YawGoal = -0.52359867f;   // -30 deg.
    constexpr float fGpffff82ac_mode2YawAccel = 0.0065449835f;
    constexpr float fGpffff82b0_mode2YawGoal = 0.52359867f; // 30 deg.
    constexpr float fGpffff82b4_mode12YawMax = 0.13962631f; // 8 deg.

    constexpr float fGpffff82b8_mode5YawStep = -0.017453289f; // -1 deg.

    constexpr float fGpffff82c4_autoFocusAccel = 0.0043633222f; // 0.25 deg.
    constexpr float fGpffff82c8_autoFocusMax = 0.10471974f;     // 6 deg.
    constexpr float fGpffff82cc_autoFocusGoalPositive = 0.52359867f;
    constexpr float fGpffff82d0_autoFocusGoalNegative = -0.52359867f;
    constexpr float fGpffff82d4_idleYawAccel = 0.00087266444f; // 0.05 deg.
    constexpr float fGpffff82d8_idleYawMax = 0.026179934f;     // 1.5 deg.

    constexpr float fGpffff82dc_verticalAccel = 0.03f;
    constexpr float fGpffff82e0_verticalDecel = 0.06f;
    constexpr float fGpffff82e4_orientationEyeOffset = 0.40000004f;

    // The speed accumulators are clamped to a quarter unit per frame.
    constexpr float kSpeedAccumulatorCap = 0.25f;

    float wrapAngle(float radians)
    {
      // FUN_00216690.
      constexpr float kTwoPi = 6.28318530718f;
      while (radians > 3.14159265359f)
      {
        radians -= kTwoPi;
      }
      while (radians < -3.14159265359f)
      {
        radians += kTwoPi;
      }
      return radians;
    }

    // FUN_002166e8: signed shortest angular difference from `from` to `to`.
    float shortestAngleDelta(float from, float to)
    {
      return wrapAngle(to - from);
    }

    // The two follow ladders in FUN_00216aa0 share this shape: pick the largest
    // fixed step whose threshold the error clears, and fall back to a
    // proportional term past the outermost band.
    float ladderStep(float error,
                     float negThreshold0, float negThreshold1, float negOuter,
                     float negStep0, float negStep1, float negStep2, float negTail,
                     float posThreshold0, float posThreshold1, float posOuter,
                     float posStep0, float posStep1, float posStep2, float posTail)
    {
      if (error < 0.0f)
      {
        if (error > negThreshold0)
        {
          return error;
        }
        if (error > negThreshold1)
        {
          return negStep0;
        }
        if (error > negOuter)
        {
          return negStep1;
        }
        if (error > negOuter - 1.0f)
        {
          return negStep2;
        }
        return (error - (negOuter - 1.0f)) - negTail;
      }

      if (error > 0.0f)
      {
        if (error < posThreshold0)
        {
          return error;
        }
        if (error < posThreshold1)
        {
          return posStep0;
        }
        if (error < posOuter)
        {
          return posStep1;
        }
        if (error < posOuter + 1.0f)
        {
          return posStep2;
        }
        return (error - (posOuter + 1.0f)) + posTail;
      }

      return 0.0f;
    }

    // Shared accel/decel limiter used for the horizontal and vertical follow.
    float limitSpeed(float desired, float &accumulator, float accel, float decel)
    {
      float step = desired;
      if (accumulator < step)
      {
        if (accumulator + accel < step)
        {
          step = accumulator + accel;
        }
        if (step > kSpeedAccumulatorCap)
        {
          step = kSpeedAccumulatorCap;
        }
      }
      else if (step < accumulator)
      {
        if (step < accumulator - decel)
        {
          step = accumulator - decel;
        }
        if (step < 0.0f)
        {
          step = 0.0f;
        }
      }
      accumulator = step;
      return step;
    }

  } // namespace

  OriginalFieldCamera::OriginalFieldCamera()
  {
    FUN_00216930_install_normal_field_defaults();
  }

  void OriginalFieldCamera::FUN_00216930_install_normal_field_defaults()
  {
    fGpffffad28_distance_ = 3.0f;
    uGpffffad24_pitchSetting_ = fGpffff8224_defaultPitch;
    FUN_00216968_set_follow_distance(3.0f);
  }

  void OriginalFieldCamera::FUN_00216968_set_follow_distance(float distance)
  {
    fGpffffad28_distance_ = distance;
    fGpffffbaf8_horizontalFollow_ = distance * std::cos(uGpffffad24_pitchSetting_);
    fGpffffbafc_verticalFollow_ = distance * std::sin(uGpffffad24_pitchSetting_) + fGpffff8228_eyeHeightOffset;
    cGpffffb6e3_snapRequest_ = 1;
  }

  void OriginalFieldCamera::snapToTarget(const orphen::ported::psm2::Vec3 &target)
  {
    DAT_0055f8c8_targetWork_ = {target.x, target.y, target.z + fGpffffbafc_verticalFollow_};
    DAT_0058be90_lookAt_ = {target.x, target.y, target.z + iGpffff8230_targetHeight};

    DAT_0058c0a8_eye_.x = target.x - fGpffffbaf8_horizontalFollow_ * std::cos(fGpffffb6d4_yaw_);
    DAT_0058c0a8_eye_.y = target.y - fGpffffbaf8_horizontalFollow_ * std::sin(fGpffffb6d4_yaw_);
    DAT_0058c0a8_eye_.z = target.z + fGpffffbafc_verticalFollow_;

    fGpffffacfc_horizontalSpeed_ = 0.0f;
    fGpffffad00_verticalSpeed_ = 0.0f;
    fGpffffad04_yawSpeed_ = 0.0f;
    uGpffffad0c_idleTimer_ = 0;
    cGpffffb6e3_snapRequest_ = 0;

    publishOrientation();
  }

  void OriginalFieldCamera::FUN_00216aa0_update(std::uint32_t frameTicks,
                                                const FieldCameraInput &input,
                                                const orphen::ported::psm2::Vec3 &target,
                                                const CameraGroundSampler &groundSampler)
  {
    const float tickScale = orphen::ported::movementScaleForFrameTicks(frameTicks);

    // --- idle timeout ----------------------------------------------------
    if (cGpffffb6e2_cutsceneGate_ == 0 && cGpffffb6e1_subMode_ == 0)
    {
      if (input.rawHeldPad == 0 && input.previousStickMagnitude == 0.0f && input.stickMagnitude == 0.0f)
      {
        uGpffffad0c_idleTimer_ += frameTicks;
      }
      else
      {
        uGpffffad0c_idleTimer_ = 0;
      }
      // The original hands off to FUN_002184e8 here. That auto-camera is not
      // ported yet, so the timer is tracked and exposed but not acted on.
    }
    else
    {
      uGpffffad0c_idleTimer_ = 0;
    }

    // --- free-look acquire / release (FUN_00218270 not ported) ------------
    if ((input.rawPressedPad & kRawPadR3) != 0)
    {
      if (cGpffffb6e4_freeLook_ == 0)
      {
        cGpffffb6e4_freeLook_ = 1;
        DAT_0058bf0c_freeLookEntryYaw_ = fGpffffb6d4_yaw_;
      }
      else
      {
        // Release nudges the eye back along the view direction by 0.1.
        DAT_0058c0a8_eye_.x -= pose_.forward.x * fGpffff8234_freeLookRelease;
        DAT_0058c0a8_eye_.y -= pose_.forward.y * fGpffff8234_freeLookRelease;
        DAT_0058c0a8_eye_.z -= pose_.forward.z * fGpffff8234_freeLookRelease;
        cGpffffb6e4_freeLook_ = 0;
      }
    }

    // --- camera mode from the raw pad ------------------------------------
    // FUN_00251ed8 clears uGpffffb6e0 to 0 earlier in the frame, and
    // FUN_00216aa0 only ever raises it. Releasing the button therefore drops
    // straight back to the default branch, which decays the yaw speed to rest.
    bGpffffb6e0_mode_ = FieldCameraMode::None;
    if ((input.rawHeldPad & kRawPadR1) != 0)
    {
      bGpffffb6e0_mode_ = FieldCameraMode::RotateNegative;
    }
    else if ((input.rawHeldPad & kRawPadL1) != 0)
    {
      bGpffffb6e0_mode_ = FieldCameraMode::RotatePositive;
    }

    // A snap was requested (scene load, distance change): place the camera and
    // skip every smoothing path this frame.
    if (cGpffffb6e3_snapRequest_ != 0)
    {
      snapToTarget(target);
      return;
    }

    // --- yaw deadzone ------------------------------------------------------
    bool clampYawBySpeed = false;
    if (input.stickMagnitude < 40.0f)
    {
      clampYawBySpeed = true;
    }
    else
    {
      const float absAngle = std::abs(input.stickAngle);
      if (absAngle > fGpffff8240_yawDeadzoneLow && absAngle < fGpffff8244_yawDeadzoneHigh)
      {
        clampYawBySpeed = true;
      }
    }

    followTargetHeight(frameTicks, target);

    DAT_0055f8c8_targetWork_.x = target.x;
    DAT_0055f8c8_targetWork_.y = target.y;
    DAT_0058be90_lookAt_.x = target.x;
    DAT_0058be90_lookAt_.y = target.y;

    float eyeDeltaX = 0.0f;
    float eyeDeltaY = 0.0f;
    float currentYaw = 0.0f;
    float horizontalDistance = 0.0f;
    followHorizontal(frameTicks, eyeDeltaX, eyeDeltaY, currentYaw, horizontalDistance);

    // --- mode switch: this frame's yaw accel / max / goal -------------------
    float yawAccel = iGpffff8238_defaultYawAccel;
    float yawMaxSpeed = iGpffff823c_defaultYawMax;
    float yawStep = 0.0f;

    switch (bGpffffb6e0_mode_)
    {
    case FieldCameraMode::RotateNegative:
      cGpffffad08_autoFocusDirection_ = -1;
      yawAccel = tickScale * fGpffff82a4_mode1YawAccel;
      yawMaxSpeed = tickScale * fGpffff82b4_mode12YawMax;
      yawStep = tickScale * fGpffff82a8_mode1YawGoal;
      clampYawBySpeed = false;
      break;

    case FieldCameraMode::RotatePositive:
      cGpffffad08_autoFocusDirection_ = 1;
      yawAccel = tickScale * fGpffff82ac_mode2YawAccel;
      yawMaxSpeed = tickScale * fGpffff82b4_mode12YawMax;
      yawStep = tickScale * fGpffff82b0_mode2YawGoal;
      clampYawBySpeed = false;
      break;

    case FieldCameraMode::FaceTarget:
      yawStep = shortestAngleDelta(currentYaw, fGpffffb6d4_yaw_);
      cGpffffad08_autoFocusDirection_ = 0;
      break;

    case FieldCameraMode::FixedStep:
      yawStep = tickScale * fGpffff82b8_mode5YawStep;
      cGpffffad08_autoFocusDirection_ = -1;
      clampYawBySpeed = false;
      break;

    case FieldCameraMode::EaseToGoal:
      // Reachable only from paths that are not ported (script camera goals).
      // Preserved as a documented no-op so the switch keeps its shape.
      cGpffffad08_autoFocusDirection_ = 0;
      break;

    case FieldCameraMode::DecayFast:
    case FieldCameraMode::None:
    default:
      if (bGpffffb6e0_mode_ == FieldCameraMode::DecayFast)
      {
        fGpffffad04_yawSpeed_ -= yawAccel + yawAccel;
        if (fGpffffad04_yawSpeed_ < 0.0f)
        {
          fGpffffad04_yawSpeed_ = 0.0f;
        }
      }

      if (cGpffffad08_autoFocusDirection_ == 0)
      {
        // Idle auto-focus: ease the camera back behind the player, very slowly.
        const float toEntity = shortestAngleDelta(currentYaw, input.autoFocusGoalYaw);
        yawAccel = tickScale * fGpffff82d4_idleYawAccel;
        yawMaxSpeed = tickScale * fGpffff82d8_idleYawMax;
        yawStep = toEntity * 0.125f * tickScale;
      }
      else if (fGpffffad04_yawSpeed_ == 0.0f)
      {
        cGpffffad08_autoFocusDirection_ = 0;
        yawStep = 0.0f;
      }
      else
      {
        yawAccel = tickScale * fGpffff82c4_autoFocusAccel;
        yawMaxSpeed = tickScale * fGpffff82c8_autoFocusMax;
        yawStep = tickScale * (cGpffffad08_autoFocusDirection_ >= 0 ? fGpffff82cc_autoFocusGoalPositive
                                                                   : fGpffff82d0_autoFocusGoalNegative);
        clampYawBySpeed = true;
      }
      break;
    }

    // --- apply the yaw step, accel- and max-speed-limited -------------------
    if (yawStep == 0.0f)
    {
      fGpffffad04_yawSpeed_ = 0.0f;
    }
    else
    {
      float yawStepMagnitude = std::abs(yawStep);

      if (clampYawBySpeed)
      {
        // Leave room to decelerate to a stop from the current speed.
        const float decelBudget = fGpffffad04_yawSpeed_ - 2.0f * (yawAccel * tickScale);
        if (decelBudget < yawStepMagnitude)
        {
          yawStepMagnitude = decelBudget < 0.0f ? 0.0f : decelBudget;
        }
      }

      if (fGpffffad04_yawSpeed_ < yawStepMagnitude)
      {
        fGpffffad04_yawSpeed_ += yawAccel * tickScale;
        if (fGpffffad04_yawSpeed_ < yawStepMagnitude)
        {
          yawStepMagnitude = fGpffffad04_yawSpeed_;
        }
        const float maxThisFrame = yawMaxSpeed * tickScale;
        if (maxThisFrame < yawStepMagnitude)
        {
          yawStepMagnitude = maxThisFrame;
        }
      }

      fGpffffad04_yawSpeed_ = yawStepMagnitude;
      const float signedStep = yawStep < 0.0f ? -yawStepMagnitude : yawStepMagnitude;

      if (signedStep != 0.0f)
      {
        // Orbit the eye to the new yaw at the current horizontal distance.
        const float newYaw = currentYaw + signedStep;
        const float desiredEyeX = target.x - horizontalDistance * std::cos(newYaw);
        const float desiredEyeY = target.y - horizontalDistance * std::sin(newYaw);
        eyeDeltaX += desiredEyeX - DAT_0058c0a8_eye_.x;
        eyeDeltaY += desiredEyeY - DAT_0058c0a8_eye_.y;
      }
    }

    if (eyeDeltaX != 0.0f || eyeDeltaY != 0.0f)
    {
      DAT_0058c0a8_eye_.x += eyeDeltaX;
      DAT_0058c0a8_eye_.y += eyeDeltaY;
      if (groundSampler)
      {
        const auto ground = groundSampler(DAT_0058c0a8_eye_.x, DAT_0058c0a8_eye_.y, DAT_0058c0a8_eye_.z);
        if (ground.has_value())
        {
          DAT_0058c0d4_groundUnderEye_ = *ground;
        }
      }
    }

    followVertical(frameTicks, groundSampler);
    publishOrientation();

    cGpffffb6e3_snapRequest_ = 0;
  }

  void OriginalFieldCamera::followTargetHeight(std::uint32_t frameTicks,
                                               const orphen::ported::psm2::Vec3 &target)
  {
    (void)frameTicks; // The height ladders are per-frame steps, not tick-scaled.

    // First ladder: the work target, which trails the player by fGpffffbafc.
    {
      const float error = (target.z + fGpffffbafc_verticalFollow_) - DAT_0055f8c8_targetWork_.z;
      DAT_0055f8c8_targetWork_.z += ladderStep(error,
                                               fGpffff8248, fGpffff824c, -2.0f,
                                               fGpffff8248, fGpffff8250, fGpffff8254, fGpffff8258,
                                               fGpffff825c, fGpffff8260, 2.0f,
                                               fGpffff825c, fGpffff8264, fGpffff8268, fGpffff826c);
    }

    // Second ladder: the published look-at height.
    {
      const float error = (target.z + iGpffff8230_targetHeight) - DAT_0058be90_lookAt_.z;
      DAT_0058be90_lookAt_.z += ladderStep(error,
                                           fGpffff8270, fGpffff8274, -1.0f,
                                           fGpffff8270, fGpffff8278, fGpffff827c, fGpffff8280,
                                           fGpffff8284, fGpffff8288, 1.0f,
                                           fGpffff8284, fGpffff828c, fGpffff8290, fGpffff8294);
    }
  }

  void OriginalFieldCamera::followHorizontal(std::uint32_t frameTicks,
                                             float &eyeDeltaX,
                                             float &eyeDeltaY,
                                             float &currentYaw,
                                             float &horizontalDistance)
  {
    const float tickScale = orphen::ported::movementScaleForFrameTicks(frameTicks);

    const float toEyeX = DAT_0055f8c8_targetWork_.x - DAT_0058c0a8_eye_.x;
    const float toEyeY = DAT_0055f8c8_targetWork_.y - DAT_0058c0a8_eye_.y;

    horizontalDistance = std::sqrt(toEyeX * toEyeX + toEyeY * toEyeY);
    currentYaw = std::atan2(toEyeY, toEyeX);

    float distanceError = horizontalDistance - fGpffffbaf8_horizontalFollow_;
    if (distanceError == 0.0f)
    {
      return;
    }

    // A quarter of the remaining error per nominal frame, accel/decel limited.
    distanceError = distanceError * tickScale * 0.25f;

    float magnitude = std::abs(distanceError);
    if (magnitude < fGpffff8298_horizontalDeadzone)
    {
      magnitude = 0.0f;
    }
    magnitude = limitSpeed(magnitude,
                           fGpffffacfc_horizontalSpeed_,
                           fGpffff829c_horizontalAccel,
                           fGpffff82a0_horizontalDecel);

    const float signedStep = distanceError < 0.0f ? -magnitude : magnitude;
    if (signedStep != 0.0f)
    {
      eyeDeltaX = signedStep * std::cos(currentYaw);
      eyeDeltaY = signedStep * std::sin(currentYaw);
    }
  }

  void OriginalFieldCamera::followVertical(std::uint32_t frameTicks, const CameraGroundSampler &groundSampler)
  {
    const float tickScale = orphen::ported::movementScaleForFrameTicks(frameTicks);

    float goalZ = DAT_0055f8c8_targetWork_.z;

    // Lift the eye out of the floor: when the goal is below the terrain under
    // the eye but within two units of it, follow the terrain instead.
    if (groundSampler && cGpffffb6e6_disableGroundClamp_ == 0 &&
        goalZ < DAT_0058c0d4_groundUnderEye_ && DAT_0058c0d4_groundUnderEye_ - 2.0f < goalZ)
    {
      goalZ = DAT_0058c0d4_groundUnderEye_;
    }

    float error = goalZ - DAT_0058c0a8_eye_.z;
    if (error == 0.0f)
    {
      fGpffffad00_verticalSpeed_ = 0.0f;
      return;
    }

    error = error * tickScale * 0.25f;

    const float magnitude = limitSpeed(std::abs(error),
                                       fGpffffad00_verticalSpeed_,
                                       fGpffff82dc_verticalAccel,
                                       fGpffff82e0_verticalDecel);

    DAT_0058c0a8_eye_.z += error < 0.0f ? -magnitude : magnitude;
  }

  void OriginalFieldCamera::publishOrientation()
  {
    const float dirX = DAT_0058be90_lookAt_.x - DAT_0058c0a8_eye_.x;
    const float dirY = DAT_0058be90_lookAt_.y - DAT_0058c0a8_eye_.y;
    const float dirZ = DAT_0058be90_lookAt_.z - (DAT_0058c0a8_eye_.z + fGpffff82e4_orientationEyeOffset);

    const float horizontal = std::sqrt(dirX * dirX + dirY * dirY);

    fGpffffb6d4_yaw_ = std::atan2(dirY, dirX);

    // FUN_00216aa0 eases pitch toward the goal while yaw snaps.
    const float pitchGoal = std::atan2(dirZ, horizontal);
    fGpffffb6d8_pitch_ += shortestAngleDelta(fGpffffb6d8_pitch_, pitchGoal);

    pose_.eye = {DAT_0058c0a8_eye_.x,
                 DAT_0058c0a8_eye_.y,
                 DAT_0058c0a8_eye_.z + fGpffff82e4_orientationEyeOffset};
    pose_.target = DAT_0058be90_lookAt_;
    pose_.forward = {dirX, dirY, dirZ};
    pose_.yawRadians = fGpffffb6d4_yaw_;
    pose_.pitchRadians = fGpffffb6d8_pitch_;
    pose_.horizontalDistance = horizontal;

    const float length = std::sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ);
    if (length > 0.0f)
    {
      pose_.forward = {dirX / length, dirY / length, dirZ / length};
    }
  }

} // namespace orphen::ported::camera

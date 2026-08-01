#pragma once

// Native counterpart of the field camera driver: src/FUN_00216aa0.c (0x00216aa0),
// with the manual modes from src/FUN_00218710.c (0x00218710) and the follow
// geometry from src/FUN_00216968.c (0x00216968).
//
// See analyzed/update_field_camera.c, analyzed/update_manual_camera.c and
// analyzed/camera_orientation_and_defaults.c for the reading these are built
// from. The original keeps all of its state in globals; the DAT_/gp names are
// preserved as field-name suffixes so the mapping stays checkable.

#include "ported/camera/original_camera_state.h"
#include "ported/psm2/psm2_runtime.h"

#include <cstdint>
#include <functional>
#include <optional>

namespace orphen::ported::camera
{

  // Raw pad bits, post-CONCAT11 inversion in FUN_0023b5d8. The low byte is
  // byte-swapped relative to the usual PS2 constants.
  constexpr std::uint16_t kRawPadL1 = 0x0004;
  constexpr std::uint16_t kRawPadR1 = 0x0008;
  constexpr std::uint16_t kRawPadR3 = 0x0400;

  // bGpffffb6e0. The original switches on `mode - 1`, so 0 lands in `default`.
  //
  // FUN_00251ed8 (the player update, which runs earlier in the frame) clears
  // this to 0 every frame; FUN_00216aa0 then re-raises it to 1 or 2 only while
  // a shoulder button is held. So the mode is per-frame, and what persists
  // across frames after a release is cGpffffad08 plus the yaw speed
  // accumulator, which the default branch decays back to rest.
  enum class FieldCameraMode : std::uint8_t
  {
    None = 0,           // default branch: auto-focus / decay
    RotateNegative = 1, // R1 held; switch case 0
    RotatePositive = 2, // L1 held; switch case 1
    FaceTarget = 3,     // case 2
    DecayFast = 4,      // case 3, falls through to default
    FixedStep = 5,      // case 4
    EaseToGoal = 6,     // case 5
  };

  struct FieldCameraInput
  {
    std::uint16_t rawHeldPad = 0;    // uGpffffb684 / DAT_003555f4
    std::uint16_t rawPressedPad = 0; // uGpffffb686 / DAT_003555f6
    float stickAngle = 0.0f;         // fGpffffb674
    float stickMagnitude = 0.0f;     // fGpffffb678
    float previousStickMagnitude = 0.0f; // fGpffffb680

    // DAT_0058bf0c: the yaw the idle auto-focus eases toward. The original
    // sources it from an entity's facing (+0x5C); see FUN_00234468.
    float autoFocusGoalYaw = 0.0f;
  };

  // FUN_00227798: terrain height under a world position, if any.
  using CameraGroundSampler = std::function<std::optional<float>(float x, float y, float z)>;

  class OriginalFieldCamera
  {
  public:
    OriginalFieldCamera();

    // FUN_00216930: distance 3.0, pitch 0.36651909 (21 degrees).
    void FUN_00216930_install_normal_field_defaults();

    // FUN_00216968: recompute the derived follow geometry and request a snap.
    void FUN_00216968_set_follow_distance(float distance);

    // Place the camera behind a target immediately, skipping all smoothing.
    // Used on scene load and on reset, where the original relies on the
    // cGpffffb6e3 snap flag having been set by FUN_00216968.
    void snapToTarget(const orphen::ported::psm2::Vec3 &target);

    // One nominal frame of FUN_00216aa0. frameTicks is DAT_003555bc.
    void FUN_00216aa0_update(std::uint32_t frameTicks,
                             const FieldCameraInput &input,
                             const orphen::ported::psm2::Vec3 &target,
                             const CameraGroundSampler &groundSampler = {});

    const CameraPose &pose() const { return pose_; }

    float yawRadians() const { return fGpffffb6d4_yaw_; }
    float pitchRadians() const { return fGpffffb6d8_pitch_; }
    float followDistance() const { return fGpffffad28_distance_; }
    FieldCameraMode mode() const { return bGpffffb6e0_mode_; }
    bool freeLookActive() const { return cGpffffb6e4_freeLook_ != 0; }
    std::uint32_t idleTicks() const { return uGpffffad0c_idleTimer_; }

    // uGpffffad0c > 0x1c200 is where the original hands off to the idle
    // auto-camera (FUN_002184e8). That camera is not ported, so this is
    // reported rather than acted on. At the nominal 0x20 ticks it is one minute.
    bool idleTimedOut() const { return uGpffffad0c_idleTimer_ > 0x1c200u; }

  private:
    // --- eye and target state ---------------------------------------------
    orphen::ported::psm2::Vec3 DAT_0058c0a8_eye_{};      // +a8/+ac/+b0
    orphen::ported::psm2::Vec3 DAT_0058be90_lookAt_{};   // +90/+94/+98
    orphen::ported::psm2::Vec3 DAT_0055f8c8_targetWork_{};
    float DAT_0058c0d4_groundUnderEye_ = 0.0f;

    // --- orientation -------------------------------------------------------
    float fGpffffb6d4_yaw_ = 0.0f;
    float fGpffffb6d8_pitch_ = 0.0f;

    // --- follow geometry, derived by FUN_00216968 --------------------------
    float fGpffffad28_distance_ = 3.0f;
    float uGpffffad24_pitchSetting_ = 0.0f;
    float fGpffffbaf8_horizontalFollow_ = 0.0f;
    float fGpffffbafc_verticalFollow_ = 0.0f;

    // --- mode and gates ----------------------------------------------------
    FieldCameraMode bGpffffb6e0_mode_ = FieldCameraMode::None;
    std::uint8_t cGpffffb6e1_subMode_ = 0;
    std::uint8_t cGpffffb6e2_cutsceneGate_ = 0;
    std::uint8_t cGpffffb6e3_snapRequest_ = 0;
    std::uint8_t cGpffffb6e4_freeLook_ = 0;
    std::uint8_t cGpffffb6e6_disableGroundClamp_ = 0;
    std::int8_t cGpffffad08_autoFocusDirection_ = 0;
    float DAT_0058bf0c_freeLookEntryYaw_ = 0.0f;

    // --- smoothing accumulators --------------------------------------------
    float fGpffffacfc_horizontalSpeed_ = 0.0f;
    float fGpffffad00_verticalSpeed_ = 0.0f;
    float fGpffffad04_yawSpeed_ = 0.0f;
    std::uint32_t uGpffffad0c_idleTimer_ = 0;

    CameraPose pose_;

    void followTargetHeight(std::uint32_t frameTicks, const orphen::ported::psm2::Vec3 &target);
    void followHorizontal(std::uint32_t frameTicks, float &eyeDeltaX, float &eyeDeltaY, float &currentYaw, float &horizontalDistance);
    void followVertical(std::uint32_t frameTicks, const CameraGroundSampler &groundSampler);
    void publishOrientation();
  };

} // namespace orphen::ported::camera

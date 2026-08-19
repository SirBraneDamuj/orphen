#pragma once

// Native counterpart of the field camera driver: src/FUN_00216aa0.c (0x00216aa0),
// with the manual modes from src/FUN_00218710.c (0x00218710) and the follow
// geometry from src/FUN_00216968.c (0x00216968).
//
// See analyzed/update_field_camera.c, analyzed/update_manual_camera.c and
// analyzed/camera_orientation_and_defaults.c for the reading these are built
// from. The original keeps all of its state in globals; the DAT_/gp names are
// preserved as field-name suffixes so the mapping stays checkable.

#include "ported/camera/original_camera_path.h"
#include "ported/camera/original_camera_state.h"
#include "ported/psm2/psm2_runtime.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>

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

    // FUN_00217d70: pin the camera to an explicit eye and look-at, the way a
    // cutscene does. Refuses while a manual camera is already installed
    // (cGpffffad2f), and saves the current pose so FUN_00217e18 can put it
    // back. Sets the submode to 0x23, which is the script camera -- with no
    // interpolation armed, FUN_00217b88 leaves the pose exactly here.
    void FUN_00217d70_set_manual_camera(const orphen::ported::psm2::Vec3 &eye,
                                        const orphen::ported::psm2::Vec3 &lookAt);

    // FUN_00217e18: drop the manual camera. `restore` is the original's
    // non-zero argument, which puts the saved eye, yaw and pitch back and asks
    // the follow camera to snap.
    void FUN_00217e18_release_manual_camera(bool restore);

    bool manualCameraActive() const { return cGpffffad2f_manualCamera_ != 0; }
    // cGpffffb6e1. 0x23 is the script camera; behaviours that want to take the
    // camera over test for it before they do, because a path can only be
    // installed on top of one that is already there.
    std::uint8_t cGpffffb6e1_subMode() const { return cGpffffb6e1_subMode_; }

    // FUN_00217d40 / FUN_00217d10: move an installed script camera's eye or
    // look-at. Both are no-ops unless the submode is 0x23.
    void FUN_00217d40_set_eye(const orphen::ported::psm2::Vec3 &eye);
    void FUN_00217d10_set_look_at(const orphen::ported::psm2::Vec3 &lookAt);

    // FUN_00217fe8: install a camera path. The caller has already dropped
    // whatever camera was there (FUN_00217e18(0)); this puts a fresh manual
    // camera at the first eye point and keeps the curves for
    // FUN_00218158_step_camera_path to sample.
    //
    // `zoomScales` are the pre-FUN_00218230 values, the way the caller writes
    // them into its scratch block.
    void FUN_00217fe8_set_camera_path(std::span<const orphen::ported::psm2::Vec3> eyePoints,
                                      std::span<const float> rollValues,
                                      std::span<const float> zoomScales,
                                      std::span<const orphen::ported::psm2::Vec3> lookAtPoints);

    // FUN_00218158: sample the path at elapsed/duration and publish the eye,
    // the look-at, the roll and the zoom.
    void FUN_00218158_step_camera_path(int elapsed, int duration);
    bool cameraPathActive() const { return cameraPath_.active(); }

    // uGpffffb6dc and fGpffffb6e8, which FUN_0020bec8 reads when it builds the
    // projection. FUN_002241d8 puts the zoom back to 1.0 when a cutscene ends.
    float uGpffffb6dc_roll() const { return uGpffffb6dc_roll_; }
    float fGpffffb6e8_zoomLog2() const { return fGpffffb6e8_zoomLog2_; }
    void FUN_002241d8_reset_zoom();
    // Script opcode 0x6C sets the zoom outright; 0x6A/0x6B ramp it. The value
    // is already through FUN_00218230, so it is a log2 scale, not a distance.
    void setZoomLog2(float zoomLog2) { fGpffffb6e8_zoomLog2_ = zoomLog2; }

    // The camera path is not the only writer of uGpffffb6dc. Opcode 0x4C
    // (FUN_0025e520) assigns it, and so does FUN_002676d8 -- the storm hook
    // opcode 0xBE reaches -- which rolls the camera by the same wave it rolls
    // the sea with. Both write the global directly, so whichever runs last in
    // the script's own order wins, exactly as here.
    void setRoll(float radians) { uGpffffb6dc_roll_ = radians; }

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
    // cGpffffad2f: a manual camera is installed. FUN_00217d70 latches it so a
    // second request cannot displace the first.
    std::uint8_t cGpffffad2f_manualCamera_ = 0;
    // DAT_0055f8d8 / DAT_0055f8e4 / DAT_0055f8e8: the pose FUN_00217d70 saved.
    orphen::ported::psm2::Vec3 DAT_0055f8d8_savedEye_{};
    float DAT_0055f8e4_savedYaw_ = 0.0f;
    float DAT_0055f8e8_savedPitch_ = 0.0f;
    std::uint8_t cGpffffb6e6_disableGroundClamp_ = 0;
    std::int8_t cGpffffad08_autoFocusDirection_ = 0;
    float DAT_0058bf0c_freeLookEntryYaw_ = 0.0f;

    // --- the scripted camera path ------------------------------------------
    // FUN_00217b88's interpolators are dead in the retail build -- nothing
    // ever writes iGpffffbb0c or iGpffffbb14 a non-zero duration -- so the only
    // thing that moves a 0x23 camera is FUN_00218158, driven by whoever
    // installed the path.
    CameraPath cameraPath_;
    float uGpffffb6dc_roll_ = 0.0f;
    float fGpffffb6e8_zoomLog2_ = 1.0f;

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

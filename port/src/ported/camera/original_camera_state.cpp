#include "ported/camera/original_camera_state.h"

#include <cmath>
#include <limits>

namespace orphen::ported::camera
{
  namespace
  {

    constexpr float kFieldFollowMode1eDistance = 5.0f;
    constexpr float kFieldFollowMode1ePitchRadians = -0.97738421f; // DAT_00352290.
    constexpr float kFieldFollowMode1eTargetHeight = 0.800000012f; // DAT_0035229c.
    constexpr float kFieldFollowMode1eEyeHeightOffset = 0.400000036f; // DAT_003522a0.
    constexpr float kNormalFieldCameraDistance = 3.0f; // FUN_00216930 -> FUN_00216968(3.0).
    constexpr float kNormalFieldCameraPitchRadians = 0.366519094f; // uGpffff8224.
    constexpr float kNormalFieldTargetHeight = 0.800000012f; // iGpffff8230.
    constexpr float kNormalFieldEyeHeightOffset = -0.200000003f; // fGpffff8228/fGpffff822c.
    constexpr float kNormalFieldOrientationEyeOffset = 0.400000036f; // fGpffff82e4/fGpffff82e8.

    orphen::ported::psm2::Vec3 normalize(const orphen::ported::psm2::Vec3 &value)
    {
      const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
      if (length <= std::numeric_limits<float>::epsilon())
      {
        return {};
      }
      return {value.x / length, value.y / length, value.z / length};
    }

  } // namespace

  void OriginalCameraState::setEye(const orphen::ported::psm2::Vec3 &eye)
  {
    // Original FUN_00217d40: writes DAT_0058c0a8/ac/b0, then calls FUN_00217a70 when active.
    pose_.eye = eye;
    recomputeOrientation();
  }

  void OriginalCameraState::setTarget(const orphen::ported::psm2::Vec3 &target)
  {
    // Original FUN_00217d10: writes DAT_0058be90/94/98, then calls FUN_00217a70 when active.
    pose_.target = target;
    recomputeOrientation();
  }

  void OriginalCameraState::setEyeAndTarget(const orphen::ported::psm2::Vec3 &eye, const orphen::ported::psm2::Vec3 &target)
  {
    // Original FUN_00217d70 initializes both camera triplets before the shared recompute.
    pose_.eye = eye;
    pose_.target = target;
    recomputeOrientation();
  }

  void OriginalCameraState::setNormalFieldFollow(const orphen::ported::psm2::Vec3 &leadPosition, float yawRadians)
  {
    // Original FUN_00216aa0 normal field camera after FUN_00216930 default initialization.
    const float horizontalDistance = kNormalFieldCameraDistance * std::cos(kNormalFieldCameraPitchRadians);
    const float eyeHeight = leadPosition.z +
                            kNormalFieldCameraDistance * std::sin(kNormalFieldCameraPitchRadians) +
                            kNormalFieldEyeHeightOffset;
    pose_.target = {leadPosition.x, leadPosition.y, leadPosition.z + kNormalFieldTargetHeight};
    pose_.eye = {leadPosition.x - std::cos(yawRadians) * horizontalDistance,
                 leadPosition.y - std::sin(yawRadians) * horizontalDistance,
                 eyeHeight};
    verticalEyeOffset_ = kNormalFieldOrientationEyeOffset;
    recomputeOrientation();
  }

  void OriginalCameraState::setFieldFollowMode1e(const orphen::ported::psm2::Vec3 &leadPosition, float yawRadians)
  {
    // Original FUN_00218710 mode 0x1e: fixed 5-unit field follow using DAT_00352290/9c/a0.
    const float horizontalScale = std::cos(kFieldFollowMode1ePitchRadians);
    const orphen::ported::psm2::Vec3 direction{horizontalScale * std::cos(yawRadians),
                                               horizontalScale * std::sin(yawRadians),
                                               std::sin(kFieldFollowMode1ePitchRadians)};
    pose_.target = {leadPosition.x, leadPosition.y, leadPosition.z + kFieldFollowMode1eTargetHeight};
    pose_.eye = {leadPosition.x - direction.x * kFieldFollowMode1eDistance,
                 leadPosition.y - direction.y * kFieldFollowMode1eDistance,
                 pose_.target.z - direction.z * kFieldFollowMode1eDistance - kFieldFollowMode1eEyeHeightOffset};
    recomputeOrientation();
  }

  void OriginalCameraState::setVerticalEyeOffset(float offset)
  {
    verticalEyeOffset_ = offset;
    recomputeOrientation();
  }

  void OriginalCameraState::recomputeOrientation()
  {
    // Original FUN_00217a70 derives yaw/pitch from target - eye and stores the normalized delta.
    const float deltaX = pose_.target.x - pose_.eye.x;
    const float deltaY = pose_.target.y - pose_.eye.y;
    const float deltaZ = pose_.target.z - (pose_.eye.z + verticalEyeOffset_);
    pose_.horizontalDistance = std::sqrt(deltaX * deltaX + deltaY * deltaY);

    if (pose_.horizontalDistance > std::numeric_limits<float>::epsilon())
    {
      pose_.yawRadians = std::atan2(deltaY, deltaX);
      pose_.pitchRadians = std::atan2(deltaZ, pose_.horizontalDistance);
    }

    pose_.forward = normalize({deltaX, deltaY, deltaZ});
  }

} // namespace orphen::ported::camera

#include "ported/camera/original_camera_state.h"

#include <cmath>
#include <limits>

namespace orphen::ported::camera
{
  namespace
  {

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

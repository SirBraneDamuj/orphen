#pragma once

#include "ported/psm2/psm2_runtime.h"

namespace orphen::ported::camera
{

  struct CameraPose
  {
    orphen::ported::psm2::Vec3 eye{};
    orphen::ported::psm2::Vec3 target{};
    orphen::ported::psm2::Vec3 forward{};
    float yawRadians = 0.0f;
    float pitchRadians = 0.0f;
    float horizontalDistance = 0.0f;
  };

  class OriginalCameraState
  {
  public:
    void setEye(const orphen::ported::psm2::Vec3 &eye);
    void setTarget(const orphen::ported::psm2::Vec3 &target);
    void setEyeAndTarget(const orphen::ported::psm2::Vec3 &eye, const orphen::ported::psm2::Vec3 &target);
    void setVerticalEyeOffset(float offset);

    const CameraPose &pose() const { return pose_; }

  private:
    CameraPose pose_;
    float verticalEyeOffset_ = 0.0f;

    void recomputeOrientation();
  };

} // namespace orphen::ported::camera

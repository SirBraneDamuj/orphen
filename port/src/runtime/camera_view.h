#pragma once

#include "ported/psm2/psm2_runtime.h"

namespace orphen::port
{

  struct RuntimeCameraView
  {
    orphen::ported::psm2::Vec3 eye{};
    orphen::ported::psm2::Vec3 target{};
    float verticalFovDegrees = 60.0f;
    float farPlaneHint = 1000.0f;
  };

} // namespace orphen::port

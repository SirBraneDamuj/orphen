#pragma once

#include "ported/camera/original_camera_state.h"
#include "ported/psm2/psm2_runtime.h"
#include "runtime/camera_view.h"
#include "runtime/player_debug_probe.h"

namespace orphen::port
{

  class ProbeFollowCamera
  {
  public:
    void resetToProbe(const PlayerDebugProbeState &probe, const orphen::ported::psm2::Psm2RuntimeState &map);
    void update(float deltaSeconds,
                float rotateX,
                float rotateY,
                float zoom,
                const PlayerDebugProbeState &probe,
                const orphen::ported::psm2::Psm2RuntimeState &map);

    orphen::ported::psm2::Vec3 movementVectorForInput(float moveX, float moveY) const;
    RuntimeCameraView view() const;

  private:
    orphen::ported::camera::OriginalCameraState camera_;
    float cameraHeadingRadians_ = 0.0f;
    float manualYawOffsetRadians_ = 0.0f;
    float pitchDegrees_ = 20.0f;
    float targetPitchDegrees_ = 20.0f;
    float distance_ = 20.0f;
    float farPlaneHint_ = 1000.0f;

    void refreshPose(const PlayerDebugProbeState &probe, const orphen::ported::psm2::Psm2RuntimeState &map);
  };

} // namespace orphen::port

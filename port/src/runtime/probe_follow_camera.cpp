#include "runtime/probe_follow_camera.h"

#include <algorithm>
#include <cmath>

namespace orphen::port
{
  namespace
  {

    constexpr double kPi = 3.14159265358979323846;
    constexpr float kDefaultPitchDegrees = 20.0f;
    constexpr float kDefaultDistance = 5.5f;
    constexpr float kMinPitchDegrees = 8.0f;
    constexpr float kMaxPitchDegrees = 42.0f;
    constexpr float kMinDistance = 2.0f;
    constexpr float kMaxDistance = 64.0f;
    constexpr float kYawSpeedDegrees = 45.0f;
    constexpr float kPitchSpeedDegrees = 22.5f;
    constexpr float kZoomSpeed = 2.0f;
    constexpr float kFollowYawSharpness = 2.2f;
    constexpr float kManualYawReturnSharpness = 0.85f;
    constexpr float kPitchSharpness = 3.0f;
    constexpr float kProbeEyeHeight = 1.0f;
    constexpr float kLookAheadDistance = 1.5f;
    constexpr float kShoulderOffset = 0.55f;
    constexpr float kTargetShoulderOffset = 0.2f;

    float degreesToRadians(float degrees)
    {
      return degrees * static_cast<float>(kPi / 180.0);
    }

    float wrapRadians(float radians)
    {
      return std::remainder(radians, static_cast<float>(kPi * 2.0));
    }

    float smoothAngle(float current, float target, float sharpness, float deltaSeconds)
    {
      const float factor = 1.0f - std::exp(-sharpness * deltaSeconds);
      return wrapRadians(current + wrapRadians(target - current) * factor);
    }

    float smoothValue(float current, float target, float sharpness, float deltaSeconds)
    {
      const float factor = 1.0f - std::exp(-sharpness * deltaSeconds);
      return current + (target - current) * factor;
    }

    orphen::ported::psm2::Vec3 normalizedHorizontal(float radians)
    {
      return {std::cos(radians), std::sin(radians), 0.0f};
    }

    float farPlaneForMap(const orphen::ported::psm2::Psm2RuntimeState &map, float cameraDistance)
    {
      if (!map.bounds.valid)
      {
        return std::max(1000.0f, cameraDistance * 16.0f);
      }

      const float spanX = map.bounds.max.x - map.bounds.min.x;
      const float spanY = map.bounds.max.y - map.bounds.min.y;
      const float spanZ = map.bounds.max.z - map.bounds.min.z;
      const float largestSpan = std::max({spanX, spanY, spanZ});
      return std::max(1000.0f, largestSpan * 2.0f + cameraDistance * 16.0f);
    }

  } // namespace

  void ProbeFollowCamera::resetToProbe(const PlayerDebugProbeState &probe, const orphen::ported::psm2::Psm2RuntimeState &map)
  {
    scriptPoseActive_ = false;
    cameraHeadingRadians_ = probe.facingRadians;
    manualYawOffsetRadians_ = 0.0f;
    pitchDegrees_ = kDefaultPitchDegrees;
    targetPitchDegrees_ = kDefaultPitchDegrees;
    distance_ = kDefaultDistance;
    refreshPose(probe, map);
  }

  void ProbeFollowCamera::setScriptDistance(float distance, const PlayerDebugProbeState &probe, const orphen::ported::psm2::Psm2RuntimeState &map)
  {
    distance_ = std::clamp(distance, kMinDistance, kMaxDistance);
    if (scriptPoseActive_)
    {
      farPlaneHint_ = farPlaneForMap(map, distance_);
    }
    else
    {
      refreshPose(probe, map);
    }
  }

  void ProbeFollowCamera::setScriptPose(const orphen::ported::psm2::Vec3 &eye,
                                        const orphen::ported::psm2::Vec3 &target,
                                        const orphen::ported::psm2::Psm2RuntimeState &map)
  {
    scriptPoseActive_ = true;
    camera_.setEyeAndTarget(eye, target);
    cameraHeadingRadians_ = camera_.pose().yawRadians;
    farPlaneHint_ = farPlaneForMap(map, distance_);
  }

  void ProbeFollowCamera::clearScriptPose(const PlayerDebugProbeState &probe, const orphen::ported::psm2::Psm2RuntimeState &map)
  {
    if (!scriptPoseActive_)
    {
      return;
    }

    scriptPoseActive_ = false;
    refreshPose(probe, map);
  }

  void ProbeFollowCamera::update(float deltaSeconds,
                                 float rotateX,
                                 float rotateY,
                                 float zoom,
                                 const PlayerDebugProbeState &probe,
                                 const orphen::ported::psm2::Psm2RuntimeState &map)
  {
    if (scriptPoseActive_)
    {
      farPlaneHint_ = farPlaneForMap(map, distance_);
      return;
    }

    manualYawOffsetRadians_ = wrapRadians(manualYawOffsetRadians_ + degreesToRadians(rotateX * kYawSpeedDegrees * deltaSeconds));
    if (std::abs(rotateX) < 0.001f)
    {
      manualYawOffsetRadians_ = smoothAngle(manualYawOffsetRadians_, 0.0f, kManualYawReturnSharpness, deltaSeconds);
    }
    cameraHeadingRadians_ = smoothAngle(cameraHeadingRadians_, probe.facingRadians + manualYawOffsetRadians_, kFollowYawSharpness, deltaSeconds);
    targetPitchDegrees_ = std::clamp(targetPitchDegrees_ + rotateY * kPitchSpeedDegrees * deltaSeconds, kMinPitchDegrees, kMaxPitchDegrees);
    pitchDegrees_ = smoothValue(pitchDegrees_, targetPitchDegrees_, kPitchSharpness, deltaSeconds);
    distance_ *= std::exp(-zoom * kZoomSpeed * deltaSeconds);
    distance_ = std::clamp(distance_, kMinDistance, kMaxDistance);
    refreshPose(probe, map);
  }

  orphen::ported::psm2::Vec3 ProbeFollowCamera::movementVectorForInput(float moveX, float moveY) const
  {
    const orphen::ported::psm2::Vec3 forward = normalizedHorizontal(cameraHeadingRadians_);

    const orphen::ported::psm2::Vec3 right{forward.y, -forward.x, 0.0f};
    return {right.x * moveX + forward.x * moveY,
            right.y * moveX + forward.y * moveY,
            0.0f};
  }

  RuntimeCameraView ProbeFollowCamera::view() const
  {
    const auto &pose = camera_.pose();
    return {pose.eye, pose.target, 65.0f, farPlaneHint_};
  }

  void ProbeFollowCamera::refreshPose(const PlayerDebugProbeState &probe, const orphen::ported::psm2::Psm2RuntimeState &map)
  {
    farPlaneHint_ = farPlaneForMap(map, distance_);

    const float pitchRadians = degreesToRadians(pitchDegrees_);
    const float horizontalDistance = std::cos(pitchRadians) * distance_;
    const float verticalDistance = std::sin(pitchRadians) * distance_;

    const orphen::ported::psm2::Vec3 forward = normalizedHorizontal(cameraHeadingRadians_);
    const orphen::ported::psm2::Vec3 shoulderOffset{-forward.y, forward.x, 0.0f};
    const orphen::ported::psm2::Vec3 shoulderTarget{probe.position.x + forward.x * kLookAheadDistance + shoulderOffset.x * kTargetShoulderOffset,
                                                    probe.position.y + forward.y * kLookAheadDistance + shoulderOffset.y * kTargetShoulderOffset,
                                                    probe.position.z + kProbeEyeHeight};
    const orphen::ported::psm2::Vec3 eye{probe.position.x - forward.x * horizontalDistance + shoulderOffset.x * kShoulderOffset,
                                         probe.position.y - forward.y * horizontalDistance + shoulderOffset.y * kShoulderOffset,
                                         shoulderTarget.z + verticalDistance};

    camera_.setEyeAndTarget(eye, shoulderTarget);
  }

} // namespace orphen::port

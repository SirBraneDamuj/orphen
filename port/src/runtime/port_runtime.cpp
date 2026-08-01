#include "runtime/port_runtime.h"

#include "runtime/psm2_ground_query.h"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include <algorithm>
#include <iostream>

namespace orphen::port
{
  namespace
  {

    constexpr std::uint32_t kHarnessFrameCounterAddress = 0x00001000;
    constexpr float kRadiansToDegrees = 57.2957795f;

    std::string formatNumber(float value, int precision = 2)
    {
      std::ostringstream stream;
      stream << std::fixed << std::setprecision(precision) << value;
      return stream.str();
    }

    // FUN_00256bb8 / FUN_002534d8 animation ids written into entity +0xA0.
    const char *animationName(std::uint16_t animationId)
    {
      switch (animationId)
      {
      case orphen::ported::player::kAnimationStand: return "STAND";
      case orphen::ported::player::kAnimationWalk: return "WALK";
      case orphen::ported::player::kAnimationRun: return "RUN";
      case orphen::ported::player::kAnimationJumpRise: return "RISE";
      case orphen::ported::player::kAnimationJumpFall: return "FALL";
      case orphen::ported::player::kAnimationLand: return "LAND";
      case orphen::ported::player::kAnimationIdleFidget: return "FIDGET";
      default: return "OTHER";
      }
    }

    // Entity +0x60.
    const char *stateName(std::uint16_t state)
    {
      switch (state)
      {
      case 0: return "IDLE";
      case 1: return "MOVING";
      case 2: return "AIR";
      default: return "OTHER";
      }
    }

    const char *cameraModeName(orphen::ported::camera::FieldCameraMode mode)
    {
      using Mode = orphen::ported::camera::FieldCameraMode;
      switch (mode)
      {
      case Mode::None: return "AUTO";
      case Mode::RotateNegative: return "R1";
      case Mode::RotatePositive: return "L1";
      case Mode::FaceTarget: return "FACE";
      case Mode::DecayFast: return "DECAY";
      case Mode::FixedStep: return "FIXED";
      case Mode::EaseToGoal: return "EASE";
      default: return "?";
      }
    }

  } // namespace

  void PortRuntime::initialize(const PortRuntimeConfig &config)
  {
    reset();
    spawnOverride_ = config.spawnOverride;

    if (!config.decodedPsm2Path.empty())
    {
      mapViewer_.loadDecodedPsm2(config.decodedPsm2Path);
    }
    else if (!config.discRoot.empty())
    {
      mapViewer_.loadDiscSceneMap(config.discRoot, config.discScene);
    }

    if (mapViewer_.loadedMap() != nullptr)
    {
      resetLeadPlayerForLoadedMap();
      if (config.printSceneTree)
      {
        mapViewer_.printLoadedSceneTree(std::cout);
      }
      const auto &stats = mapViewer_.loadedMap()->stats;
      std::cout << "[psm2] loaded " << mapViewer_.loadedSourceDescription()
                << " positions=" << stats.positionRecordCount
                << " sectionB=" << stats.sectionBRecordCount
                << " primitives=" << stats.primitiveRecordCount
                << " triangles=" << stats.triangleCount
                << " skipped=" << stats.skippedPrimitiveCount
                << " textures=" << mapViewer_.loadedTexturePageCount() << '\n';

      const auto &leadState = leadPlayer_.viewState();
      std::cout << "[player] spawn=(" << leadState.position.x << ", " << leadState.position.y
                << ", " << leadState.position.z << ")"
                << (config.spawnOverride.has_value() ? " (--spawn)" : " (map centre)")
                << " grounded=" << (leadState.grounded ? 1 : 0) << '\n';
    }
  }

  void PortRuntime::reset()
  {
    memory_.clear();
    frameCount_ = 0;
    mapViewer_.resetCamera();
    resetLeadPlayerForLoadedMap();
  }

  bool PortRuntime::update(const InputSnapshot &input, std::uint32_t frameTicks)
  {
    ++frameCount_;
    // The viewer's free camera and the follow-yaw easing still think in seconds.
    // One simulation step is one nominal frame, so hand them that directly
    // rather than wall-clock time -- this is what makes --frames deterministic.
    const float stepSeconds = orphen::ported::kNominalFrameSeconds *
                              (static_cast<float>(frameTicks) / static_cast<float>(orphen::ported::kNominalFrameTicks));
    mapViewer_.update(stepSeconds, input);

    if (mapViewer_.loadedMapGeneration() != trackedMapGeneration_)
    {
      resetLeadPlayerForLoadedMap();
    }
    auto *loadedMap = mapViewer_.loadedMap();
    if (loadedMap != nullptr)
    {
      // The player moves relative to the camera's yaw from the previous frame,
      // then the camera follows the new position -- the same one-frame ordering
      // the original has, since FUN_00251ed8 runs before FUN_00216aa0.
      const float cameraYaw = fieldCamera_.yawRadians();
      const orphen::ported::psm2::Vec3 forwardBasis{std::cos(cameraYaw), std::sin(cameraYaw), 0.0f};
      const orphen::ported::psm2::Vec3 rightBasis{std::sin(cameraYaw), -std::cos(cameraYaw), 0.0f};
      const orphen::ported::psm2::Vec3 movementRequest{
          rightBasis.x * input.moveX + forwardBasis.x * input.moveY,
          rightBasis.y * input.moveX + forwardBasis.y * input.moveY,
          0.0f};

      leadPlayer_.update(frameTicks, movementRequest, input.stickMagnitude, input.jumpRequested, loadedMap);

      const auto &leadState = leadPlayer_.viewState();
      orphen::ported::camera::FieldCameraInput cameraInput;
      cameraInput.rawHeldPad = input.rawHeldPad;
      cameraInput.rawPressedPad = input.rawPressedPad;
      cameraInput.stickAngle = input.stickAngle;
      cameraInput.stickMagnitude = input.stickMagnitude;
      cameraInput.previousStickMagnitude = previousStickMagnitude_;
      cameraInput.autoFocusGoalYaw = leadState.facingRadians;
      previousStickMagnitude_ = input.stickMagnitude;

      fieldCamera_.FUN_00216aa0_update(frameTicks, cameraInput, leadState.position, cameraGroundSampler());

      mapViewer_.setLeadPlayerView(leadState);
      mapViewer_.setFollowCameraPose(fieldCamera_.pose());
    }
    else
    {
      mapViewer_.setLeadPlayerView(std::nullopt);
    }
    reportLeadPlayerGroundChange();
    updateHud(input, frameTicks);

    memory_.write(kHarnessFrameCounterAddress, frameCount_);

    return true;
  }

  void PortRuntime::render(int framebufferWidth, int framebufferHeight) const
  {
    mapViewer_.render(framebufferWidth, framebufferHeight);
  }

  void PortRuntime::resetLeadPlayerForLoadedMap()
  {
    const auto *loadedMap = mapViewer_.loadedMap();
    trackedMapGeneration_ = mapViewer_.loadedMapGeneration();
    reportedGroundPrimitive_.reset();
    if (loadedMap == nullptr)
    {
      mapViewer_.setLeadPlayerView(std::nullopt);
      return;
    }

    leadPlayer_.resetToMap(*loadedMap, spawnOverride_);
    previousStickMagnitude_ = 0.0f;
    fieldCamera_.FUN_00216930_install_normal_field_defaults();
    fieldCamera_.snapToTarget(leadPlayer_.viewState().position);
    mapViewer_.setLeadPlayerView(leadPlayer_.viewState());
    mapViewer_.setFollowCameraPose(fieldCamera_.pose());
  }

  void PortRuntime::reportLeadPlayerGroundChange()
  {
    const auto &leadState = leadPlayer_.viewState();
    if (!leadState.groundHit.has_value())
    {
      if (reportedGroundPrimitive_.has_value())
      {
        std::cout << "[player] ground=none\n";
        reportedGroundPrimitive_.reset();
      }
      return;
    }

    const auto &groundHit = *leadState.groundHit;
    if (reportedGroundPrimitive_ == groundHit.primitiveIndex)
    {
      return;
    }

    reportedGroundPrimitive_ = groundHit.primitiveIndex;
    std::cout << "[player] primitive=" << groundHit.primitiveIndex
              << " triangle=" << groundHit.triangleIndex
              << " z=" << groundHit.height
              << " leading=0x" << std::hex << groundHit.leadingWord
              << " terrain=0x" << groundHit.terrainFlags << std::dec
              << (groundHit.sampledByOriginalTerrain ? " sampled" : " unsampled") << '\n';
  }

  void PortRuntime::updateHud(const InputSnapshot &input, std::uint32_t frameTicks)
  {
    const auto &lead = leadPlayer_.viewState();
    std::vector<std::string> lines;

    lines.push_back("ORPHEN PORT  " + mapViewer_.loadedSourceDescription() +
                    "  FRAME " + std::to_string(frameCount_) +
                    "  TICKS " + std::to_string(frameTicks));

    lines.push_back("POS  " + formatNumber(lead.position.x) + ", " + formatNumber(lead.position.y) +
                    ", " + formatNumber(lead.position.z) +
                    "   FACING " + formatNumber(lead.facingRadians * kRadiansToDegrees, 1));

    lines.push_back("STATE " + std::string(stateName(lead.state)) +
                    "  ANIM " + animationName(lead.animationId) +
                    " (" + std::to_string(lead.animationId) + ")" +
                    "  FRM " + std::to_string(lead.substateFrame));

    lines.push_back(std::string("GROUND ") + (lead.grounded ? "YES" : "NO") +
                    "  VZ " + formatNumber(lead.verticalVelocity, 4) +
                    "  FLAGS " + std::to_string(lead.collisionFlags));

    lines.push_back("STICK " + formatNumber(input.stickMagnitude, 1) +
                    "  GAIT " + std::string(lead.running ? "RUN" : "WALK") +
                    "  (RUN ABOVE 100)");

    lines.push_back("CAM  MODE " + std::string(cameraModeName(fieldCamera_.mode())) +
                    "  YAW " + formatNumber(fieldCamera_.yawRadians() * kRadiansToDegrees, 1) +
                    "  PITCH " + formatNumber(fieldCamera_.pitchRadians() * kRadiansToDegrees, 1) +
                    "  DIST " + formatNumber(fieldCamera_.followDistance()));

    if (lead.groundHit.has_value())
    {
      lines.push_back("TRI  PRIM " + std::to_string(lead.groundHit->primitiveIndex) +
                      "  Z " + formatNumber(lead.groundHit->height) +
                      "  TERRAIN " + std::to_string(lead.groundHit->terrainFlags));
    }

    lines.push_back("WASD MOVE  SPACE JUMP  J/L CAMERA  F WIRE  H HUD  R RESET");

    mapViewer_.setHudLines(std::move(lines));
  }

  orphen::ported::camera::CameraGroundSampler PortRuntime::cameraGroundSampler()
  {
    const auto *loadedMap = mapViewer_.loadedMap();
    if (loadedMap == nullptr)
    {
      return {};
    }

    // FUN_00227798, used by FUN_00216aa0 to keep the eye out of the floor.
    return [loadedMap](float x, float y, float z) -> std::optional<float>
    {
      const auto hit = queryPsm2GroundAt(*loadedMap, x, y, z);
      if (!hit.has_value())
      {
        return std::nullopt;
      }
      return hit->height;
    };
  }

} // namespace orphen::port

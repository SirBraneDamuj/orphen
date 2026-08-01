#include "runtime/port_runtime.h"

#include <algorithm>
#include <iostream>

namespace orphen::port
{
  namespace
  {

    constexpr std::uint32_t kHarnessFrameCounterAddress = 0x00001000;

  } // namespace

  void PortRuntime::initialize(const PortRuntimeConfig &config)
  {
    reset();

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
    }
  }

  void PortRuntime::reset()
  {
    memory_.clear();
    frameCount_ = 0;
    mapViewer_.resetCamera();
    resetLeadPlayerForLoadedMap();
  }

  bool PortRuntime::update(float deltaSeconds, const InputSnapshot &input)
  {
    ++frameCount_;
    const float clampedDelta = std::clamp(deltaSeconds, 0.0f, 0.1f);
    mapViewer_.update(clampedDelta, input);

    if (mapViewer_.loadedMapGeneration() != trackedMapGeneration_)
    {
      resetLeadPlayerForLoadedMap();
    }
    auto *loadedMap = mapViewer_.loadedMap();
    if (loadedMap != nullptr)
    {
      const auto movementRequest = mapViewer_.cameraRelativeMovement(input.moveX, input.moveY);
      leadPlayer_.update(clampedDelta, movementRequest, input.jumpRequested, loadedMap);
      mapViewer_.setLeadPlayerView(leadPlayer_.viewState(), clampedDelta);
    }
    else
    {
      mapViewer_.setLeadPlayerView(std::nullopt);
    }
    reportLeadPlayerGroundChange();

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

    leadPlayer_.resetToMap(*loadedMap);
    mapViewer_.setLeadPlayerView(leadPlayer_.viewState());
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

} // namespace orphen::port

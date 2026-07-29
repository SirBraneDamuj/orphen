#include "runtime/port_runtime.h"

#include <algorithm>
#include <iomanip>
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
      resetPlayerProbeForLoadedMap();
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
    resetPlayerProbeForLoadedMap();
  }

  bool PortRuntime::update(float deltaSeconds, const InputSnapshot &input)
  {
    ++frameCount_;
    const float clampedDelta = std::clamp(deltaSeconds, 0.0f, 0.1f);
    mapViewer_.update(clampedDelta, input);

    if (mapViewer_.loadedMapGeneration() != trackedMapGeneration_)
    {
      resetPlayerProbeForLoadedMap();
    }
    const auto *loadedMap = mapViewer_.loadedMap();
    if (loadedMap != nullptr)
    {
      const auto cameraRelativeMove = probeCamera_.movementVectorForInput(input.moveX, input.moveY);
      playerProbe_.update(clampedDelta, cameraRelativeMove.x, cameraRelativeMove.y, input.jumpRequested, loadedMap);
      probeCamera_.update(clampedDelta, input.rotateX, input.rotateY, input.zoom, playerProbe_.state(), *loadedMap);
      mapViewer_.setDebugPlayerProbe(playerProbe_.state());
      mapViewer_.setRuntimeCameraView(probeCamera_.view());
    }
    else
    {
      mapViewer_.setDebugPlayerProbe(std::nullopt);
      mapViewer_.setRuntimeCameraView(std::nullopt);
    }
    reportProbeGroundChange();

    memory_.write(kHarnessFrameCounterAddress, frameCount_);

    return true;
  }

  void PortRuntime::render(int framebufferWidth, int framebufferHeight) const
  {
    mapViewer_.render(framebufferWidth, framebufferHeight);
  }

  void PortRuntime::resetPlayerProbeForLoadedMap()
  {
    const auto *loadedMap = mapViewer_.loadedMap();
    trackedMapGeneration_ = mapViewer_.loadedMapGeneration();
    reportedGroundPrimitive_.reset();
    if (loadedMap == nullptr)
    {
      mapViewer_.setDebugPlayerProbe(std::nullopt);
      mapViewer_.setRuntimeCameraView(std::nullopt);
      return;
    }

    playerProbe_.resetToMap(*loadedMap);
    probeCamera_.resetToProbe(playerProbe_.state(), *loadedMap);
    mapViewer_.setDebugPlayerProbe(playerProbe_.state());
    mapViewer_.setRuntimeCameraView(probeCamera_.view());
    reportProbeGroundChange();
  }

  void PortRuntime::reportProbeGroundChange()
  {
    const auto &probeState = playerProbe_.state();
    if (!probeState.groundHit.has_value())
    {
      if (reportedGroundPrimitive_.has_value())
      {
        std::cout << "[probe] ground=none\n";
        reportedGroundPrimitive_.reset();
      }
      return;
    }

    const auto &groundHit = *probeState.groundHit;
    if (reportedGroundPrimitive_ == groundHit.primitiveIndex)
    {
      return;
    }

    reportedGroundPrimitive_ = groundHit.primitiveIndex;
    std::cout << "[probe] primitive=" << groundHit.primitiveIndex
              << " triangle=" << groundHit.triangleIndex
              << " z=" << groundHit.height
              << " leading=0x" << std::hex << groundHit.leadingWord
              << " terrain=0x" << groundHit.terrainFlags << std::dec
              << (groundHit.sampledByOriginalTerrain ? " sampled" : " unsampled") << '\n';
  }

} // namespace orphen::port

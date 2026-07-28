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
  }

  bool PortRuntime::update(float deltaSeconds, const InputSnapshot &input)
  {
    ++frameCount_;
    mapViewer_.update(std::clamp(deltaSeconds, 0.0f, 0.1f), input);

    memory_.write(kHarnessFrameCounterAddress, frameCount_);

    return true;
  }

  void PortRuntime::render(int framebufferWidth, int framebufferHeight) const
  {
    mapViewer_.render(framebufferWidth, framebufferHeight);
  }

} // namespace orphen::port

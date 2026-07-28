#pragma once

#include "platform/sdl_gl_window.h"
#include "harness/map_viewer.h"
#include "runtime/ps2_memory.h"

#include <cstdint>
#include <filesystem>

namespace orphen::port
{

  struct PortRuntimeConfig
  {
    std::filesystem::path decodedPsm2Path;
    bool exitAfterUsage = false;
    bool loadOnly = false;
  };

  class PortRuntime
  {
  public:
    void initialize(const PortRuntimeConfig &config);
    void reset();
    bool update(float deltaSeconds, const InputSnapshot &input);
    void render(int framebufferWidth, int framebufferHeight) const;

  private:
    Ps2Memory memory_;
    orphen::harness::MapViewer mapViewer_;
    std::uint32_t frameCount_ = 0;
  };

} // namespace orphen::port

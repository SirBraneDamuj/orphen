#pragma once

#include "platform/sdl_gl_window.h"
#include "runtime/ps2_memory.h"

#include <cstdint>

namespace orphen::port
{

  class PortRuntime
  {
  public:
    void initialize();
    void reset();
    bool update(float deltaSeconds, const InputSnapshot &input);
    void render(int framebufferWidth, int framebufferHeight) const;

  private:
    Ps2Memory memory_;
    std::uint32_t frameCount_ = 0;
    float elapsedSeconds_ = 0.0f;
    float markerX_ = 0.0f;
    float markerY_ = 0.0f;
  };

} // namespace orphen::port

#pragma once

#include "runtime/input_state.h"

#include <cstdint>

namespace orphen::port
{

  struct WindowConfig
  {
    const char *title;
    int width;
    int height;
  };

  class SdlGlWindow
  {
  public:
    explicit SdlGlWindow(WindowConfig config);
    ~SdlGlWindow();

    SdlGlWindow(const SdlGlWindow &) = delete;
    SdlGlWindow &operator=(const SdlGlWindow &) = delete;

    void pollEvents(InputSnapshot &input);
    void beginFrame(float red, float green, float blue);
    void swapBuffers();

    int width() const { return width_; }
    int height() const { return height_; }

  private:
    void *window_ = nullptr;
    void *controller_ = nullptr;
    std::uint16_t previousRawHeldPad_ = 0;
    void *glContext_ = nullptr;
    int width_ = 0;
    int height_ = 0;
  };

} // namespace orphen::port

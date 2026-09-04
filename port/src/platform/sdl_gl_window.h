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
    // Off makes swapBuffers return immediately instead of waiting for the
    // refresh. Only useful for benchmarking: with vsync on, a frame with
    // headroom still costs a full refresh interval, and the wait does not
    // always land in swapBuffers -- once the driver's queue is full it lands
    // in whichever GL call fills it, which makes render() look expensive.
    bool vsync = true;
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

    // Whether the driver honoured the requested swap interval. Windows
    // composites windowed surfaces through the DWM, which paces them to the
    // refresh whether or not SDL_GL_SetSwapInterval(0) succeeds -- so a
    // benchmark that shows exactly the refresh rate is measuring the compositor
    // and not the renderer, and the caller needs to be able to say so.
    int swapInterval() const;

    // The current front buffer as a binary PPM. Used to diff two builds'
    // output pixel for pixel, which is the only way a change to the draw path
    // can be shown not to have changed the picture.
    bool captureFramebuffer(const char *path) const;

    int width() const { return width_; }
    int height() const { return height_; }

  private:
    void *window_ = nullptr;
    void *controller_ = nullptr;
    std::uint16_t previousRawHeldPad_ = 0;
    // DAT_003555fe from the previous frame; FUN_0023b5d8 keeps it in uVar1 and
    // masks the new word with it to build DAT_00355600.
    std::uint16_t previousRawStickDirection_ = 0;
    void *glContext_ = nullptr;
    int width_ = 0;
    int height_ = 0;
  };

} // namespace orphen::port

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

    // While on, pollEvents routes WASD/Q/E and the mouse to the fly camera
    // fields instead of the game's movement stick, and the right mouse button
    // captures the pointer for mouse look. Off releases any capture.
    void setFlyCameraActive(bool active);
    void beginFrame(float red, float green, float blue);
    void swapBuffers();

    // The entity inspector: a second window on the same GL context, so it
    // draws with the textures the main window already uploaded. Opening it
    // hands focus straight back to the main window, so flying keeps working.
    // Its close button arrives as InputSnapshot::entityInspectorCloseRequested;
    // closing it is the caller's call.
    void setInspectorWindowOpen(bool open, const char *title);
    bool inspectorWindowOpen() const { return inspectorWindow_ != nullptr; }
    // Makes the inspector current and clears it. Every GL call until
    // endInspectorFrame lands in it.
    void beginInspectorFrame(float red, float green, float blue);
    // Presents it and makes the main window current again.
    void endInspectorFrame();
    // The inspector's front buffer, as captureFramebuffer writes the main one.
    bool captureInspector(const char *path) const;
    int inspectorWidth() const { return inspectorWidth_; }
    int inspectorHeight() const { return inspectorHeight_; }

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
    // Whichever window is current.
    static bool writeFrontBuffer(const char *path, int width, int height);

    void *window_ = nullptr;
    void *controller_ = nullptr;
    std::uint16_t previousRawHeldPad_ = 0;
    // DAT_003555fe from the previous frame; FUN_0023b5d8 keeps it in uVar1 and
    // masks the new word with it to build DAT_00355600.
    std::uint16_t previousRawStickDirection_ = 0;
    bool flyCameraActive_ = false;
    bool mouseLookHeld_ = false;
    void *glContext_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    bool vsync_ = true;
    std::uint32_t windowId_ = 0;
    void *inspectorWindow_ = nullptr;
    std::uint32_t inspectorWindowId_ = 0;
    int inspectorWidth_ = 0;
    int inspectorHeight_ = 0;
  };

} // namespace orphen::port

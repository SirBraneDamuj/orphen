#pragma once

namespace orphen::port
{

  struct WindowConfig
  {
    const char *title;
    int width;
    int height;
  };

  struct InputSnapshot
  {
    bool quitRequested = false;
    bool resetRequested = false;
    bool toggleWireframeRequested = false;
    bool previousMapRequested = false;
    bool nextMapRequested = false;
    float moveX = 0.0f;
    float moveY = 0.0f;
    float rotateX = 0.0f;
    float rotateY = 0.0f;
    float zoom = 0.0f;
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
    void *glContext_ = nullptr;
    int width_ = 0;
    int height_ = 0;
  };

} // namespace orphen::port

#include "runtime/port_runtime.h"

#include <SDL_opengl.h>

#include <algorithm>
#include <cmath>

namespace orphen::port
{
  namespace
  {

    constexpr std::uint32_t kHarnessFrameCounterAddress = 0x00001000;

    void setOrtho(int framebufferWidth, int framebufferHeight)
    {
      const float safeHeight = static_cast<float>(std::max(framebufferHeight, 1));
      const float aspect = static_cast<float>(std::max(framebufferWidth, 1)) / safeHeight;
      const float halfHeight = 6.0f;
      const float halfWidth = halfHeight * aspect;

      glMatrixMode(GL_PROJECTION);
      glLoadIdentity();
      glOrtho(-halfWidth, halfWidth, -halfHeight, halfHeight, -1.0, 1.0);

      glMatrixMode(GL_MODELVIEW);
      glLoadIdentity();
    }

    void drawGrid()
    {
      glLineWidth(1.0f);
      glBegin(GL_LINES);

      glColor3f(0.14f, 0.18f, 0.20f);
      for (int line = -12; line <= 12; ++line)
      {
        const float coordinate = static_cast<float>(line);
        glVertex2f(coordinate, -12.0f);
        glVertex2f(coordinate, 12.0f);
        glVertex2f(-12.0f, coordinate);
        glVertex2f(12.0f, coordinate);
      }

      glColor3f(0.75f, 0.18f, 0.16f);
      glVertex2f(-12.0f, 0.0f);
      glVertex2f(12.0f, 0.0f);

      glColor3f(0.16f, 0.56f, 0.82f);
      glVertex2f(0.0f, -12.0f);
      glVertex2f(0.0f, 12.0f);

      glEnd();
    }

    void drawMarker(float x, float y, float pulse)
    {
      const float radius = 0.35f + 0.05f * std::sin(pulse * 4.0f);

      glColor3f(0.92f, 0.78f, 0.24f);
      glBegin(GL_TRIANGLE_FAN);
      glVertex2f(x, y);
      for (int point = 0; point <= 32; ++point)
      {
        const float angle = static_cast<float>(point) / 32.0f * 6.28318530718f;
        glVertex2f(x + std::cos(angle) * radius, y + std::sin(angle) * radius);
      }
      glEnd();
    }

  } // namespace

  void PortRuntime::initialize()
  {
    reset();
  }

  void PortRuntime::reset()
  {
    memory_.clear();
    frameCount_ = 0;
    elapsedSeconds_ = 0.0f;
    markerX_ = 0.0f;
    markerY_ = 0.0f;
  }

  bool PortRuntime::update(float deltaSeconds, const InputSnapshot &input)
  {
    constexpr float kMarkerSpeed = 4.0f;

    ++frameCount_;
    elapsedSeconds_ += std::clamp(deltaSeconds, 0.0f, 0.1f);
    markerX_ += input.moveX * kMarkerSpeed * deltaSeconds;
    markerY_ += input.moveY * kMarkerSpeed * deltaSeconds;

    memory_.write(kHarnessFrameCounterAddress, frameCount_);

    return true;
  }

  void PortRuntime::render(int framebufferWidth, int framebufferHeight) const
  {
    setOrtho(framebufferWidth, framebufferHeight);
    drawGrid();
    drawMarker(markerX_, markerY_, elapsedSeconds_);
  }

} // namespace orphen::port

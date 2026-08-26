#include "platform/sdl_gl_window.h"

#include <SDL.h>
#include <SDL_opengl.h>

#include "ported/camera/original_field_camera.h"
#include "ported/input/original_analog_stick.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace orphen::port
{
  namespace
  {

    std::runtime_error sdlError(const char *action)
    {
      return std::runtime_error(std::string(action) + ": " + SDL_GetError());
    }

    float keyAxis(bool negativePressed, bool positivePressed)
    {
      return (positivePressed ? 1.0f : 0.0f) - (negativePressed ? 1.0f : 0.0f);
    }

    // FUN_0023b5d8's digital branch writes 0x43000000 into DAT_003555e8.
    constexpr float kFullStickMagnitude = 128.0f;

    // Raw pad face-button bits (see docs/debug_system.md and FUN_0023b5d8).
    constexpr std::uint16_t kRawPadTriangle = 0x0010;
    constexpr std::uint16_t kRawPadCircle = 0x0020;
    constexpr std::uint16_t kRawPadCross = 0x0040;
    constexpr std::uint16_t kRawPadSquare = 0x0080;

    float axisToUnit(int rawAxis)
    {
      return std::clamp(static_cast<float>(rawAxis) / 32767.0f, -1.0f, 1.0f);
    }

  } // namespace

  SdlGlWindow::SdlGlWindow(WindowConfig config)
      : width_(config.width), height_(config.height)
  {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0)
    {
      throw sdlError("SDL_Init failed");
    }

    for (int joystickIndex = 0; joystickIndex < SDL_NumJoysticks(); ++joystickIndex)
    {
      if (SDL_IsGameController(joystickIndex) == SDL_TRUE)
      {
        controller_ = SDL_GameControllerOpen(joystickIndex);
        if (controller_ != nullptr)
        {
          break;
        }
      }
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    auto *window = SDL_CreateWindow(
        config.title,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        config.width,
        config.height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);

    if (window == nullptr)
    {
      SDL_Quit();
      throw sdlError("SDL_CreateWindow failed");
    }

    void *context = SDL_GL_CreateContext(window);
    if (context == nullptr)
    {
      SDL_DestroyWindow(window);
      SDL_Quit();
      throw sdlError("SDL_GL_CreateContext failed");
    }

    if (SDL_GL_MakeCurrent(window, context) != 0)
    {
      SDL_GL_DeleteContext(context);
      SDL_DestroyWindow(window);
      SDL_Quit();
      throw sdlError("SDL_GL_MakeCurrent failed");
    }

    SDL_GL_SetSwapInterval(config.vsync ? 1 : 0);

    window_ = window;
    glContext_ = context;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glViewport(0, 0, width_, height_);
  }

  int SdlGlWindow::swapInterval() const
  {
    return SDL_GL_GetSwapInterval();
  }

  bool SdlGlWindow::captureFramebuffer(const char *path) const
  {
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width_) *
                                      static_cast<std::size_t>(height_) * 3u);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_FRONT);
    glReadPixels(0, 0, width_, height_, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    std::ofstream output(path, std::ios::binary);
    if (!output)
    {
      return false;
    }
    output << "P6\n" << width_ << ' ' << height_ << "\n255\n";
    // GL reads bottom-up; PPM is top-down.
    for (int row = height_ - 1; row >= 0; --row)
    {
      output.write(reinterpret_cast<const char *>(
                       pixels.data() + static_cast<std::size_t>(row) *
                                           static_cast<std::size_t>(width_) * 3u),
                   static_cast<std::streamsize>(width_) * 3);
    }
    return output.good();
  }

  SdlGlWindow::~SdlGlWindow()
  {
    if (glContext_ != nullptr)
    {
      SDL_GL_DeleteContext(glContext_);
    }

    if (window_ != nullptr)
    {
      SDL_DestroyWindow(static_cast<SDL_Window *>(window_));
    }

    if (controller_ != nullptr)
    {
      SDL_GameControllerClose(static_cast<SDL_GameController *>(controller_));
    }

    SDL_Quit();
  }

  void SdlGlWindow::pollEvents(InputSnapshot &input)
  {
    input = {};

    SDL_Event event;
    while (SDL_PollEvent(&event) != 0)
    {
      switch (event.type)
      {
      case SDL_QUIT:
        input.quitRequested = true;
        break;
      case SDL_WINDOWEVENT:
        if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
        {
          width_ = event.window.data1;
          height_ = event.window.data2;
          glViewport(0, 0, width_, height_);
        }
        break;
      case SDL_MOUSEBUTTONDOWN:
        if (event.button.button == SDL_BUTTON_LEFT)
        {
          input.probeRequested = true;
          input.probeX = event.button.x;
          input.probeY = event.button.y;
        }
        break;
      case SDL_KEYDOWN:
        if (event.key.repeat == 0 && event.key.keysym.sym == SDLK_r)
        {
          input.resetRequested = true;
        }
        if (event.key.repeat == 0 && event.key.keysym.sym == SDLK_f)
        {
          input.toggleWireframeRequested = true;
        }
        if (event.key.repeat == 0 && event.key.keysym.sym == SDLK_h)
        {
          input.toggleHudRequested = true;
        }
        if (event.key.repeat == 0 && event.key.keysym.sym == SDLK_b)
        {
          input.toggleDebugOverlayRequested = true;
        }
        if (event.key.repeat == 0 && event.key.keysym.sym == SDLK_p)
        {
          input.toggleSubprocDisplayRequested = true;
        }
        if (event.key.repeat == 0 && event.key.keysym.sym == SDLK_g)
        {
          input.captureSnapshotRequested = true;
        }
        if (event.key.repeat == 0 && event.key.keysym.sym == SDLK_LEFT)
        {
          input.previousMapRequested = true;
        }
        if (event.key.repeat == 0 && event.key.keysym.sym == SDLK_RIGHT)
        {
          input.nextMapRequested = true;
        }
        if (event.key.keysym.sym == SDLK_ESCAPE)
        {
          input.quitRequested = true;
        }
        break;
      default:
        break;
      }
    }

    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
    input.moveX = keyAxis(keys[SDL_SCANCODE_A] != 0, keys[SDL_SCANCODE_D] != 0);
    input.moveY = keyAxis(keys[SDL_SCANCODE_S] != 0, keys[SDL_SCANCODE_W] != 0);
    input.rotateX = keyAxis(keys[SDL_SCANCODE_J] != 0, keys[SDL_SCANCODE_L] != 0);
    input.rotateY = keyAxis(keys[SDL_SCANCODE_K] != 0, keys[SDL_SCANCODE_I] != 0);
    input.zoom = keyAxis(keys[SDL_SCANCODE_Q] != 0, keys[SDL_SCANCODE_E] != 0);

    // Keyboard camera rotate maps onto the shoulder bits the original reads.
    bool cameraLeftHeld = keys[SDL_SCANCODE_J] != 0;
    bool cameraRightHeld = keys[SDL_SCANCODE_L] != 0;
    // Raw pad low byte, post-CONCAT11 inversion in FUN_0023b5d8. SDL's face
    // buttons are positional and line up with the PS2 layout one for one, so
    // an Xbox pad maps by position: Y is Triangle, B is Circle, A is Cross and
    // X ("SDL Face West") is Square. Square is the jump binding.
    bool triangleHeld = false;
    // B stands in for Circle on the keyboard so the debug mid-air jump
    // (hold Circle, press jump) is reachable without a pad.
    bool circleHeld = keys[SDL_SCANCODE_B] != 0;
    // Return stands in for Cross, the confirm button, so the interaction probe
    // is reachable without a pad. On a pad it is the south face button (A on an
    // Xbox layout), which is what the PS2 game uses.
    bool crossHeld = keys[SDL_SCANCODE_RETURN] != 0;
    bool squareHeld = keys[SDL_SCANCODE_SPACE] != 0;

    // A gamepad, when present, overrides the keyboard for movement and camera.
    bool usingAnalogStick = false;
    auto *controller = static_cast<SDL_GameController *>(controller_);
    if (controller != nullptr && SDL_GameControllerGetAttached(controller) == SDL_TRUE)
    {
      // Feed the raw axes through the original conversion rather than a
      // hand-picked deadzone. FUN_0023b3f0 ignores anything below 60 of 128,
      // which is why the character does not start moving until the stick has
      // travelled almost halfway.
      const float rawRight = axisToUnit(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX)) *
                             orphen::ported::input::kRawAxisRange;
      const float rawUp = -axisToUnit(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY)) *
                          orphen::ported::input::kRawAxisRange; // SDL reports +Y downward.

      const auto stick = orphen::ported::input::FUN_0023b3f0_read_analog_stick(rawRight, rawUp);
      if (stick.magnitude > 0.0f)
      {
        usingAnalogStick = true;
        input.stickMagnitude = stick.magnitude;
        input.stickAngle = stick.angle;
        input.moveX = std::cos(stick.angle);
        input.moveY = std::sin(stick.angle);
      }
      else
      {
        // Inside the deadzone the original reports a hard zero on both, and
        // FUN_00256bb8 compares fGpffffb678 against 0.0 exactly.
        usingAnalogStick = true;
        input.stickMagnitude = 0.0f;
        input.stickAngle = 0.0f;
        input.moveX = 0.0f;
        input.moveY = 0.0f;
      }

      cameraLeftHeld = cameraLeftHeld ||
                       SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) != 0;
      cameraRightHeld = cameraRightHeld ||
                        SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) != 0;
      triangleHeld = triangleHeld || SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_Y) != 0;
      circleHeld = circleHeld || SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_B) != 0;
      crossHeld = crossHeld || SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A) != 0;
      squareHeld = squareHeld || SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_X) != 0;

      if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_RIGHTSTICK) != 0)
      {
        input.rawHeldPad |= orphen::ported::camera::kRawPadR3;
      }
    }

    if (cameraLeftHeld)
    {
      input.rawHeldPad |= orphen::ported::camera::kRawPadL1;
    }
    if (cameraRightHeld)
    {
      input.rawHeldPad |= orphen::ported::camera::kRawPadR1;
    }
    if (triangleHeld)
    {
      input.rawHeldPad |= kRawPadTriangle;
    }
    if (circleHeld)
    {
      input.rawHeldPad |= kRawPadCircle;
    }
    if (crossHeld)
    {
      input.rawHeldPad |= kRawPadCross;
    }
    if (squareHeld)
    {
      input.rawHeldPad |= kRawPadSquare;
    }

    // DAT_003555f6: newly pressed = held & ~previously held.
    input.rawPressedPad = static_cast<std::uint16_t>(input.rawHeldPad & ~previousRawHeldPad_);
    previousRawHeldPad_ = input.rawHeldPad;

    // FUN_00256bb8 jumps on the mapped action bit 0x80, taken from the newly
    // pressed set. Deriving it here means keyboard and pad share one path --
    // previously the pad set the raw bit but never this flag, so jumping did
    // not work on a controller at all.
    input.jumpRequested = (input.rawPressedPad & kRawPadSquare) != 0;

    if (!usingAnalogStick)
    {
      // Keyboard falls through the original's digital branch in FUN_0023b5d8,
      // which writes DAT_003555e8 = 0x43000000 (128.0) outright -- a held
      // direction on the d-pad always runs.
      const float moveLength = std::sqrt(input.moveX * input.moveX + input.moveY * input.moveY);
      if (moveLength > 0.0f)
      {
        input.stickMagnitude = kFullStickMagnitude;
        input.stickAngle = std::atan2(input.moveY, input.moveX);
        input.moveX /= moveLength;
        input.moveY /= moveLength;
      }
      else
      {
        input.stickMagnitude = 0.0f;
        input.stickAngle = 0.0f;
      }
    }
  }

  void SdlGlWindow::beginFrame(float red, float green, float blue)
  {
    glViewport(0, 0, width_, height_);
    glClearColor(red, green, blue, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  }

  void SdlGlWindow::swapBuffers()
  {
    SDL_GL_SwapWindow(static_cast<SDL_Window *>(window_));
  }

} // namespace orphen::port

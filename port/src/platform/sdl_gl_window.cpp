#include "platform/sdl_gl_window.h"

#include <SDL.h>
#include <SDL_opengl.h>

#include "ported/camera/original_field_camera.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

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

    // fGpffffb678 is compared against 40.0 by the camera and 100.0 by the
    // grounded player state, and FUN_00253488 multiplies by it directly, so
    // full stick deflection is 128.
    constexpr float kFullStickMagnitude = 128.0f;

    constexpr float kStickDeadzone = 0.18f;

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

    SDL_GL_SetSwapInterval(1);

    window_ = window;
    glContext_ = context;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glViewport(0, 0, width_, height_);
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
        if (event.key.repeat == 0 && event.key.keysym.sym == SDLK_LEFT)
        {
          input.previousMapRequested = true;
        }
        if (event.key.repeat == 0 && event.key.keysym.sym == SDLK_RIGHT)
        {
          input.nextMapRequested = true;
        }
        if (event.key.repeat == 0 && event.key.keysym.sym == SDLK_SPACE)
        {
          input.jumpRequested = true;
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
    bool jumpHeld = keys[SDL_SCANCODE_SPACE] != 0;

    // A gamepad, when present, overrides the keyboard for movement and camera.
    auto *controller = static_cast<SDL_GameController *>(controller_);
    if (controller != nullptr && SDL_GameControllerGetAttached(controller) == SDL_TRUE)
    {
      const float leftX = axisToUnit(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX));
      const float leftY = axisToUnit(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY));

      if (std::sqrt(leftX * leftX + leftY * leftY) > kStickDeadzone)
      {
        input.moveX = leftX;
        input.moveY = -leftY; // SDL reports +Y downward.
      }

      cameraLeftHeld = cameraLeftHeld ||
                       SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) != 0;
      cameraRightHeld = cameraRightHeld ||
                        SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) != 0;
      jumpHeld = jumpHeld || SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A) != 0;

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
    if (jumpHeld)
    {
      input.rawHeldPad |= 0x0080; // Square, the default jump binding.
    }

    input.rawPressedPad = static_cast<std::uint16_t>(input.rawHeldPad & ~previousRawHeldPad_);
    previousRawHeldPad_ = input.rawHeldPad;

    // fGpffffb674 / fGpffffb678. The magnitude drives the camera deadzone and
    // the walk/run split; the angle is measured so that pushing away from the
    // camera is zero, matching how FUN_00256ab0 composes facing from the
    // camera yaw. The exact original derivation lives in FUN_0023b5d8 and is
    // still to be ported.
    const float moveLength = std::sqrt(input.moveX * input.moveX + input.moveY * input.moveY);
    if (moveLength > 0.0f)
    {
      input.stickMagnitude = std::min(moveLength, 1.0f) * kFullStickMagnitude;
      input.stickAngle = std::atan2(input.moveX, input.moveY);
    }
    else
    {
      input.stickMagnitude = 0.0f;
      input.stickAngle = 0.0f;
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

#include "platform/sdl_gl_window.h"

#include <SDL.h>
#include <SDL_opengl.h>

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

  } // namespace

  SdlGlWindow::SdlGlWindow(WindowConfig config)
      : width_(config.width), height_(config.height)
  {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0)
    {
      throw sdlError("SDL_Init failed");
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
    input.moveX = keyAxis(keys[SDL_SCANCODE_A] != 0 || keys[SDL_SCANCODE_LEFT] != 0,
                          keys[SDL_SCANCODE_D] != 0 || keys[SDL_SCANCODE_RIGHT] != 0);
    input.moveY = keyAxis(keys[SDL_SCANCODE_S] != 0 || keys[SDL_SCANCODE_DOWN] != 0,
                          keys[SDL_SCANCODE_W] != 0 || keys[SDL_SCANCODE_UP] != 0);
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

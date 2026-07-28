#include "platform/sdl_gl_window.h"
#include "runtime/port_runtime.h"

#include <chrono>
#include <exception>
#include <iostream>

int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;

  try
  {
    orphen::port::SdlGlWindow window({"Orphen Native Port Harness", 1280, 720});
    orphen::port::PortRuntime runtime;

    runtime.initialize();

    auto previousTick = std::chrono::steady_clock::now();
    bool running = true;

    while (running)
    {
      const auto currentTick = std::chrono::steady_clock::now();
      const std::chrono::duration<float> delta = currentTick - previousTick;
      previousTick = currentTick;

      orphen::port::InputSnapshot input;
      window.pollEvents(input);

      if (input.quitRequested)
      {
        break;
      }

      if (input.resetRequested)
      {
        runtime.reset();
      }

      running = runtime.update(delta.count(), input);

      window.beginFrame(0.025f, 0.03f, 0.035f);
      runtime.render(window.width(), window.height());
      window.swapBuffers();
    }

    return 0;
  }
  catch (const std::exception &error)
  {
    std::cerr << "orphen_port failed: " << error.what() << '\n';
    return 1;
  }
}

#include "platform/sdl_gl_window.h"
#include "harness/map_viewer.h"
#include "runtime/port_runtime.h"

#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace
{

  void printUsage(const char *programName)
  {
    std::cout << "Usage: " << programName << " [--psm2 <decoded-map.psm2>] [--load-only]\n";
  }

  void printPsm2Stats(const std::filesystem::path &path, const orphen::ported::psm2::Psm2Stats &stats)
  {
    std::cout << "[psm2] loaded " << path.string()
              << " positions=" << stats.positionRecordCount
              << " sectionB=" << stats.sectionBRecordCount
              << " primitives=" << stats.primitiveRecordCount
              << " triangles=" << stats.triangleCount
              << " skipped=" << stats.skippedPrimitiveCount << '\n';
  }

  orphen::port::PortRuntimeConfig parseArgs(int argc, char **argv)
  {
    orphen::port::PortRuntimeConfig config;

    for (int argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
    {
      const std::string_view argument = argv[argumentIndex];
      if (argument == "--help" || argument == "-h")
      {
        printUsage(argv[0]);
        config.exitAfterUsage = true;
        return config;
      }
      if (argument == "--psm2")
      {
        if (argumentIndex + 1 >= argc)
        {
          throw std::runtime_error("--psm2 requires a path");
        }
        config.decodedPsm2Path = std::filesystem::path(argv[++argumentIndex]);
        continue;
      }
      if (argument == "--load-only")
      {
        config.loadOnly = true;
        continue;
      }

      throw std::runtime_error("unknown argument: " + std::string(argument));
    }

    return config;
  }

} // namespace

int main(int argc, char **argv)
{
  try
  {
    const orphen::port::PortRuntimeConfig config = parseArgs(argc, argv);
    if (config.exitAfterUsage)
    {
      return 0;
    }

    if (config.loadOnly)
    {
      if (config.decodedPsm2Path.empty())
      {
        throw std::runtime_error("--load-only requires --psm2 <decoded-map.psm2>");
      }

      orphen::harness::MapViewer loader;
      loader.loadDecodedPsm2(config.decodedPsm2Path);
      printPsm2Stats(config.decodedPsm2Path, loader.loadedMap()->stats);
      return 0;
    }

    orphen::port::SdlGlWindow window({"Orphen Native Port Harness", 1280, 720});
    orphen::port::PortRuntime runtime;

    runtime.initialize(config);

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

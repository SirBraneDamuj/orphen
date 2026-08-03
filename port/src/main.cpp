#include "platform/sdl_gl_window.h"
#include "harness/map_viewer.h"
#include "runtime/port_runtime.h"

#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace
{

  void printUsage(const char *programName)
  {
    std::cout << "Usage: " << programName
              << " [--psm2 <decoded-map.psm2> | --disc-root <dir> --scene sNN_eMMM]"
                 " [--load-only] [--scene-tree] [--frames <count>] [--spawn x,y,z]"
                 " [--elf <SLUS_200.11>] [--scr-report] [--scr-tick] [--actor-report]\n"
                 "  --elf           read static tables (entity descriptors, actor\n"
                 "                  behavior vtables) from the retail executable.\n"
                 "                  Defaults to SLUS_200.11 in the disc root.\n"
                 "  --scr-report    print the scene script inventory after loading.\n"
                 "  --scr-tick      run the scene script's per-frame entry and its\n"
                 "                  object-script slots every frame. Off by default.\n"
                 "  --actor-report  print which actor behavior each spawned entity\n"
                 "                  dispatches to, and which of them are ported.\n";
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
      if (argument == "--disc-root")
      {
        if (argumentIndex + 1 >= argc)
        {
          throw std::runtime_error("--disc-root requires a path");
        }
        config.discRoot = std::filesystem::path(argv[++argumentIndex]);
        continue;
      }
      if (argument == "--scene")
      {
        if (argumentIndex + 1 >= argc)
        {
          throw std::runtime_error("--scene requires sNN_eMMM");
        }
        config.discScene = orphen::harness::parseSceneName(argv[++argumentIndex]);
        config.hasDiscScene = true;
        continue;
      }
      if (argument == "--scr-tick")
      {
        config.runScriptTick = true;
        continue;
      }
      if (argument == "--actor-report")
      {
        config.printActorReport = true;
        continue;
      }
      if (argument == "--probe")
      {
        if (argumentIndex + 1 >= argc)
        {
          throw std::runtime_error("--probe needs x,y,z[,radius]");
        }
        const std::string value = argv[++argumentIndex];
        float parsed[4] = {0.0f, 0.0f, 0.0f, 2.0f};
        std::size_t start = 0;
        for (int field = 0; field < 4 && start <= value.size(); ++field)
        {
          const std::size_t comma = value.find(',', start);
          parsed[field] = std::stof(value.substr(start, comma - start));
          if (comma == std::string::npos)
          {
            break;
          }
          start = comma + 1;
        }
        config.probeCentre = orphen::ported::psm2::Vec3{parsed[0], parsed[1], parsed[2]};
        config.probeRadius = parsed[3];
        continue;
      }
      if (argument == "--render-report")
      {
        config.printRenderReport = true;
        continue;
      }
      if (argument == "--draw-distance")
      {
        if (argumentIndex + 1 >= argc)
        {
          throw std::runtime_error("--draw-distance needs a value");
        }
        config.drawDistanceOverride = std::stof(argv[++argumentIndex]);
        continue;
      }
      if (argument == "--scr-report")
      {
        config.printScriptReport = true;
        continue;
      }
      if (argument == "--elf")
      {
        if (argumentIndex + 1 >= argc)
        {
          throw std::runtime_error("--elf requires a path to SLUS_200.11");
        }
        config.executablePath = std::filesystem::path(argv[++argumentIndex]);
        continue;
      }
      if (argument == "--spawn")
      {
        if (argumentIndex + 1 >= argc)
        {
          throw std::runtime_error("--spawn requires x,y,z");
        }
        const std::string value = argv[++argumentIndex];
        const std::size_t firstComma = value.find(',');
        const std::size_t secondComma = firstComma == std::string::npos ? std::string::npos
                                                                       : value.find(',', firstComma + 1);
        if (firstComma == std::string::npos || secondComma == std::string::npos)
        {
          throw std::runtime_error("--spawn expects x,y,z");
        }
        orphen::ported::psm2::Vec3 spawn;
        spawn.x = std::stof(value.substr(0, firstComma));
        spawn.y = std::stof(value.substr(firstComma + 1, secondComma - firstComma - 1));
        spawn.z = std::stof(value.substr(secondComma + 1));
        config.spawnOverride = spawn;
        continue;
      }
      if (argument == "--load-only")
      {
        config.loadOnly = true;
        continue;
      }
      if (argument == "--scene-tree")
      {
        config.printSceneTree = true;
        continue;
      }
      if (argument == "--press-confirm")
      {
        if (argumentIndex + 1 >= argc)
        {
          throw std::runtime_error(std::string(argument) + " requires a frame number");
        }
        config.pressConfirmFrame = static_cast<std::uint32_t>(std::stoul(std::string(argv[++argumentIndex])));
        continue;
      }
      if (argument == "--frames" || argument == "--script-frames")
      {
        if (argumentIndex + 1 >= argc)
        {
          throw std::runtime_error(std::string(argument) + " requires a count");
        }
        config.headlessFrameCount = static_cast<std::uint32_t>(std::stoul(std::string(argv[++argumentIndex])));
        continue;
      }

      throw std::runtime_error("unknown argument: " + std::string(argument));
    }

    return config;
  }

  void validateSourceConfig(const orphen::port::PortRuntimeConfig &config)
  {
    const bool hasDecodedPsm2 = !config.decodedPsm2Path.empty();
    const bool hasDiscRoot = !config.discRoot.empty();
    if (hasDecodedPsm2 == hasDiscRoot)
    {
      throw std::runtime_error("provide exactly one source: --psm2 or --disc-root with --scene");
    }
    if (hasDiscRoot && !config.hasDiscScene)
    {
      throw std::runtime_error("--disc-root requires --scene sNN_eMMM");
    }
    if (!hasDiscRoot && config.hasDiscScene)
    {
      throw std::runtime_error("--scene requires --disc-root <dir>");
    }
    if (config.printSceneTree && !hasDiscRoot)
    {
      throw std::runtime_error("--scene-tree requires --disc-root <dir> --scene sNN_eMMM");
    }
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
    validateSourceConfig(config);

    if (config.loadOnly)
    {
      orphen::port::PortRuntime runtime;
      runtime.initialize(config);
      runtime.printExitReports();
      return 0;
    }

    if (config.headlessFrameCount != 0)
    {
      orphen::port::PortRuntime runtime;
      runtime.initialize(config);

      orphen::port::InputSnapshot input;
      for (std::uint32_t frameIndex = 0; frameIndex < config.headlessFrameCount; ++frameIndex)
      {
        // --press-confirm fires Cross on one frame, edge-triggered the same way
        // the window path does, so the interaction probe can be exercised
        // without a pad or a window.
        constexpr std::uint16_t kRawPadCross = 0x0040;
        const bool pressThisFrame = config.pressConfirmFrame != 0 &&
                                    frameIndex + 1 == config.pressConfirmFrame;
        input.rawPressedPad = pressThisFrame ? kRawPadCross : 0;
        input.rawHeldPad = input.rawPressedPad;
        if (!runtime.update(input))
        {
          break;
        }
      }
      runtime.printExitReports();
      return 0;
    }

    orphen::port::SdlGlWindow window({"Orphen Native Port Harness", 1280, 720});
    orphen::port::PortRuntime runtime;

    runtime.initialize(config);

    // The original is a 60 Hz title and every ported constant is per-frame, so
    // the simulation runs on a fixed step decoupled from the render rate. See
    // ported/original_frame_timing.h for how the original derives its own
    // timestep; on hardware that holds 60 fps it is exactly one step per frame.
    constexpr float kFixedStepSeconds = orphen::ported::kNominalFrameSeconds;
    constexpr int kMaxStepsPerFrame = 5;

    auto previousTick = std::chrono::steady_clock::now();
    float accumulatedSeconds = 0.0f;
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
        accumulatedSeconds = 0.0f;
      }

      accumulatedSeconds += delta.count();

      // Drop simulation time rather than spiral after a stall (window drag,
      // breakpoint, scene load). The original degrades by stretching its own
      // timestep instead; we prefer to stay deterministic and skip.
      const float maxAccumulated = kFixedStepSeconds * static_cast<float>(kMaxStepsPerFrame);
      if (accumulatedSeconds > maxAccumulated)
      {
        accumulatedSeconds = maxAccumulated;
      }

      // Edge-triggered inputs must fire on exactly one simulation step even when
      // a render frame drives several. Held axes carry across every step.
      orphen::port::InputSnapshot stepInput = input;
      while (running && accumulatedSeconds >= kFixedStepSeconds)
      {
        running = runtime.update(stepInput);
        accumulatedSeconds -= kFixedStepSeconds;

        stepInput.jumpRequested = false;
        stepInput.toggleWireframeRequested = false;
        stepInput.previousMapRequested = false;
        stepInput.nextMapRequested = false;
      }

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

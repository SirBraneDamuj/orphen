#include "platform/sdl_gl_window.h"
#include "harness/map_viewer.h"
#include "harness/audio_device.h"
#include "runtime/port_runtime.h"
#include "ported/entity/actor_frame_update.h"
#include "ported/psm2/psm2_collision_groups.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iomanip>
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
                 "  --voice-index   VOICE.BIN, or an EE dump holding the copy of its\n"
                 "                  table the game loads at boot. Sets how long each\n"
                 "                  line of dialogue holds. Defaults to searching the\n"
                 "                  disc root for either.\n"
                 "  --scr-report    print the scene script inventory after loading.\n"
                 "  --scr-dump <p>  write the decoded scene script blob out, exactly as\n"
                 "                  the interpreter sees it.\n"
                 "  --no-scr-tick   stop running the scene script's per-frame entry\n"
                 "                  and its object-script slots. On by default.\n"
                 "  --actor-report  print which actor behavior each spawned entity\n"
                 "                  dispatches to, and which of them are ported.\n"
                 "  --model-report  parse every grp record in the scene bundle and\n"
                 "                  print its geometry counts.\n"
                 "  --frame-stats   print where each frame's time goes, once per\n"
                 "                  second. Windowed runs only.\n"
                 "  --render-bench <N>\n"
                 "                  draw each frame N times before presenting, so the\n"
                 "                  render cost can be measured past the compositor's\n"
                 "                  refresh pacing. Implies --frame-stats.\n"
                 "  --push-probe\n"
                 "  --snapshot-at <frame>\n"
                 "                  write the 'G' diagnostic snapshot at <frame>\n"
                 "                  without a keypress, so a headless run can\n"
                 "                  produce one and two builds can be diffed.\n"
                 "  --screenshot <path>[:<frame>]\n"
                 "                  run one simulation step per frame, write a PPM at\n"
                 "                  <frame> and exit. Deterministic, so two builds can\n"
                 "                  be compared pixel for pixel.\n"
                 "  --map-base-slot draw only material slot 0 of each map primitive,\n"
                 "                  the way the port did before FUN_00211230's slot\n"
                 "                  loop was ported. Diagnostic A/B.\n"
                 "  --dump-map-textures <dir>\n"
                 "                  write the decoded map texture pages out as PAM.\n"
                 "  --cycle-map-every <frames>\n"
                 "                  headless only: advance to the next scene every\n"
                 "                  N frames, exercising the map-cycle reload.\n";
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
      if (argument == "--voice-index")
      {
        if (argumentIndex + 1 >= argc)
        {
          throw std::runtime_error("--voice-index requires a path");
        }
        config.voiceIndexPath = std::filesystem::path(argv[++argumentIndex]);
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
      if (argument == "--no-scr-tick")
      {
        config.runScriptTick = false;
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
      if (argument == "--sound-report")
      {
        config.printSoundReport = true;
        continue;
      }
      if (argument == "--no-scr-subproc-disp")
      {
        config.noSubprocDisplay = true;
        continue;
      }
      if (argument == "--music-solo")
      {
        config.musicSolo = true;
        continue;
      }
      if (argument == "--sound-dump")
      {
        if (argumentIndex + 1 >= argc)
        {
          throw std::runtime_error(std::string(argument) + " requires a path");
        }
        config.soundDumpPath = argv[++argumentIndex];
        continue;
      }
      if (argument == "--hold-stick")
      {
        if (argumentIndex + 1 >= argc)
        {
          throw std::runtime_error("--hold-stick needs <angle>,<magnitude>");
        }
        const std::string value = argv[++argumentIndex];
        const std::size_t comma = value.find(',');
        const float angle = std::stof(value.substr(0, comma));
        const float magnitude = comma == std::string::npos ? 128.0f : std::stof(value.substr(comma + 1));
        config.holdStick = std::make_pair(angle, magnitude);
        continue;
      }
      if (argument == "--window")
      {
        if (argumentIndex + 1 >= argc)
        {
          throw std::runtime_error("--window needs <width>x<height>");
        }
        const std::string value = argv[++argumentIndex];
        const std::size_t cross = value.find('x');
        if (cross == std::string::npos)
        {
          throw std::runtime_error("--window needs <width>x<height>");
        }
        config.windowWidth = std::stoi(value.substr(0, cross));
        config.windowHeight = std::stoi(value.substr(cross + 1));
        continue;
      }
      if (argument == "--no-audio")
      {
        config.audio = false;
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
      if (argument == "--lighting-floor")
      {
        config.applyLightFloor = true;
        continue;
      }
      if (argument == "--lighting-no-points")
      {
        config.suppressPointLights = true;
        continue;
      }
      if (argument == "--pose-report")
      {
        if (argumentIndex + 1 >= argc)
        {
          throw std::runtime_error("--pose-report requires a pool slot");
        }
        config.poseReportSlot = std::stoi(argv[++argumentIndex]);
        config.printModelReport = true;
        continue;
      }
      if (argument == "--map-no-blend")
      {
        config.suppressMapBlend = true;
        continue;
      }
      if (argument == "--map-base-slot")
      {
        config.mapBaseSlotOnly = true;
        continue;
      }
      if (argument == "--entity-bound-texture")
      {
        config.entityBoundTextureOnly = true;
        continue;
      }
      if (argument == "--scr-dump")
      {
        if (argumentIndex + 1 >= argc)
        {
          throw std::runtime_error("--scr-dump needs a path");
        }
        config.scrDumpPath = argv[++argumentIndex];
        continue;
      }
      if (argument == "--dump-map-textures")
      {
        if (argumentIndex + 1 >= argc)
        {
          throw std::runtime_error("--dump-map-textures needs a directory");
        }
        config.dumpMapTexturesPath = argv[++argumentIndex];
        continue;
      }
      if (argument == "--lighting-unlit")
      {
        config.applyUnlitFlag = true;
        continue;
      }
      if (argument == "--lighting-all")
      {
        config.applyLightFloor = true;
        config.applyUnlitFlag = true;
        continue;
      }
      if (argument == "--gleam-report")
      {
        config.printGleamReport = true;
        config.printRenderReport = true;
        continue;
      }
      if (argument == "--render-report")
      {
        config.printRenderReport = true;
        continue;
      }
      if (argument == "--frame-stats")
      {
        config.printFrameStats = true;
        continue;
      }
      if (argument == "--no-vsync")
      {
        config.vsync = false;
        continue;
      }
      if (argument == "--render-bench")
      {
        if (argumentIndex + 1 >= argc)
        {
          throw std::runtime_error("--render-bench requires a count");
        }
        config.rendersPerFrame = static_cast<std::uint32_t>(std::stoul(std::string(argv[++argumentIndex])));
        config.printFrameStats = true;
        continue;
      }
      if (argument == "--screenshot")
      {
        if (argumentIndex + 1 >= argc)
        {
          throw std::runtime_error("--screenshot needs <path>[:<frame>]");
        }
        std::string value = argv[++argumentIndex];
        // Split on the last colon so a drive letter survives.
        const std::size_t colon = value.rfind(':');
        if (colon != std::string::npos && colon > 1)
        {
          config.screenshotFrame = static_cast<std::uint32_t>(std::stoul(value.substr(colon + 1)));
          value = value.substr(0, colon);
        }
        config.screenshotPath = value;
        continue;
      }
      if (argument == "--model-report")
      {
        config.printModelReport = true;
        continue;
      }
      if (argument == "--hide-slots")
      {
        if (argumentIndex + 1 >= argc)
        {
          throw std::runtime_error("--hide-slots needs <slot>[,<slot>...]");
        }
        std::string value{argv[++argumentIndex]};
        std::size_t start = 0;
        while (start <= value.size())
        {
          const std::size_t comma = value.find(',', start);
          const std::string token =
              value.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
          if (!token.empty())
          {
            config.hideSlots.push_back(std::stoi(token));
          }
          if (comma == std::string::npos)
          {
            break;
          }
          start = comma + 1;
        }
        continue;
      }
      if (argument == "--push-probe")
      {
        orphen::ported::entity::gPushProbe = true;
        orphen::ported::psm2::gGroupProbe = true;
        continue;
      }
      if (argument == "--snapshot-at")
      {
        if (argumentIndex + 1 >= argc)
        {
          throw std::runtime_error("--snapshot-at needs a frame number");
        }
        config.snapshotFrame =
            static_cast<std::uint32_t>(std::stoul(std::string(argv[++argumentIndex])));
        continue;
      }
      if (argument == "--arm-stream")
      {
        if (argumentIndex + 1 >= argc)
        {
          throw std::runtime_error("--arm-stream needs <streamHex>[:<frame>]");
        }
        const std::string value{argv[++argumentIndex]};
        const std::size_t colon = value.find(':');
        config.armStreamOffset =
            static_cast<std::uint32_t>(std::stoul(value.substr(0, colon), nullptr, 16));
        config.armStreamFrame =
            colon == std::string::npos
                ? 1u
                : static_cast<std::uint32_t>(std::stoul(value.substr(colon + 1)));
        config.hasArmStream = true;
        continue;
      }
      if (argument == "--scr-trace-range")
      {
        if (argumentIndex + 1 >= argc)
        {
          throw std::runtime_error("--scr-trace-range needs <lowHex>-<highHex>");
        }
        const std::string range{argv[++argumentIndex]};
        const std::size_t dash = range.find('-');
        if (dash == std::string::npos)
        {
          throw std::runtime_error("--scr-trace-range needs <lowHex>-<highHex>");
        }
        config.scrTraceRangeLow = static_cast<std::uint32_t>(std::stoul(range.substr(0, dash), nullptr, 16));
        config.scrTraceRangeHigh = static_cast<std::uint32_t>(std::stoul(range.substr(dash + 1), nullptr, 16));
        config.hasScrTraceRange = true;
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
        // Comma-separated, so one run can open a chest and then dismiss the
        // caption it raises.
        const std::string frames{argv[++argumentIndex]};
        for (std::size_t start = 0; start < frames.size();)
        {
          const std::size_t comma = frames.find(',', start);
          const std::string one = frames.substr(start, comma - start);
          if (!one.empty())
          {
            config.pressConfirmFrames.push_back(static_cast<std::uint32_t>(std::stoul(one)));
          }
          if (comma == std::string::npos)
          {
            break;
          }
          start = comma + 1;
        }
        continue;
      }
      if (argument == "--cycle-map-every")
      {
        if (argumentIndex + 1 >= argc)
        {
          throw std::runtime_error(std::string(argument) + " requires a frame count");
        }
        config.cycleMapEveryFrames = static_cast<std::uint32_t>(std::stoul(std::string(argv[++argumentIndex])));
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

  // --frame-stats. Accumulated over a second's worth of frames and printed as
  // one block, because a per-frame number jitters too much to compare against.
  struct LoopStats
  {
    std::uint64_t simMicros = 0;
    std::uint64_t renderMicros = 0;
    std::uint64_t swapMicros = 0;
    std::uint64_t clearMicros = 0;
    std::uint32_t simSteps = 0;
    // Presented frames. Under --render-bench this is smaller than the render
    // count, so the two are divided by different denominators: simulation and
    // present happen once per displayed frame, the render phases N times.
    std::uint32_t displayedFrames = 0;
  };

  constexpr std::uint32_t kFrameStatsWindow = 60;

  std::uint64_t microsSince(const std::chrono::steady_clock::time_point &start)
  {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start)
            .count());
  }

  void printFrameStats(const LoopStats &loop,
                       const orphen::harness::RenderStats &render)
  {
    const auto frames = render.frames != 0 ? render.frames : 1u;
    const auto displayed = loop.displayedFrames != 0 ? loop.displayedFrames : 1u;
    const double scale = 1000.0 * static_cast<double>(frames); // micros -> ms/render
    const double displayScale = 1000.0 * static_cast<double>(displayed);
    const auto ms = [scale](std::uint64_t micros) {
      return static_cast<double>(micros) / scale;
    };
    const auto msDisplayed = [displayScale](std::uint64_t micros) {
      return static_cast<double>(micros) / displayScale;
    };
    const auto perFrame = [frames](std::uint64_t total) {
      return static_cast<double>(total) / static_cast<double>(frames);
    };

    // Swap is excluded from the ceiling: with vsync on it is idle waiting for
    // the refresh, not work, and including it would make every frame look like
    // exactly one refresh interval no matter how much headroom there was.
    const double workMs = msDisplayed(loop.simMicros) + ms(loop.renderMicros);

    std::cout << "[frame-stats] " << frames << " renders / " << displayed
              << " frames | work " << std::fixed << std::setprecision(2) << workMs
              << " ms/frame (" << (workMs > 0.0 ? 1000.0 / workMs : 0.0) << " fps ceiling)"
              << " | sim " << msDisplayed(loop.simMicros)
              << " render " << ms(loop.renderMicros)
              << " swap-wait " << msDisplayed(loop.swapMicros) << '\n';
    std::cout << "[frame-stats]   map      " << ms(render.mapDrawMicros) << " ms  "
              << perFrame(render.mapPrimitives) << " prim  "
              << perFrame(render.mapTriangles) << " tri  "
              << perFrame(render.mapBatches) << " batches  "
              << perFrame(render.mapTextureBinds) << " binds\n";
    std::cout << "[frame-stats]   entities " << ms(render.entityDrawMicros) << " ms  "
              << perFrame(render.entityModels) << " models  "
              << perFrame(render.entityPrimitives) << " prim  "
              << perFrame(render.entityTriangles) << " tri  "
              << perFrame(render.entityBatches) << " batches  "
              << perFrame(render.gleamTriangles) << " gleam-tri\n";
    std::cout << "[frame-stats]   drain    " << ms(render.gpuDrainMicros)
              << " ms  (driver/GPU catching up on queued immediate-mode calls)\n";
    std::cout << "[frame-stats]   prologue " << ms(render.prologueMicros)
              << " ms  (of which matrix readback " << ms(render.matrixReadMicros)
              << ")  clear " << ms(loop.clearMicros) << '\n';
    std::cout << "[frame-stats]   sort " << ms(render.entityListMicros)
              << "  overlay " << ms(render.overlayMicros)
              << "  hud " << ms(render.hudMicros)
              << "  | lighting " << perFrame(render.lightingEvaluations)
              << "/render  transforms " << perFrame(render.vertexTransforms)
              << "/render  sim-steps "
              << static_cast<double>(loop.simSteps) / static_cast<double>(displayed)
              << "/frame\n";
    std::cout.flush();
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

      // --sound-dump. One frame of mixer output per simulation step, which is
      // the same schedule a 60 Hz device would pull at.
      const std::size_t framesPerStep =
          static_cast<std::size_t>(orphen::ported::sound::kSpuBaseSampleRate / 60.0f);
      std::vector<float> soundDump;
      std::vector<float> stepMix(framesPerStep * 2, 0.0f);

      orphen::port::InputSnapshot input;
      for (std::uint32_t frameIndex = 0; frameIndex < config.headlessFrameCount; ++frameIndex)
      {
        // --press-confirm fires Cross on the listed frames, edge-triggered the
        // same way the window path does, so the interaction probe can be
        // exercised without a pad or a window.
        constexpr std::uint16_t kRawPadCross = 0x0040;
        const bool pressThisFrame =
            std::find(config.pressConfirmFrames.begin(), config.pressConfirmFrames.end(),
                      frameIndex + 1) != config.pressConfirmFrames.end();
        input.rawPressedPad = pressThisFrame ? kRawPadCross : 0;
        input.rawHeldPad = input.rawPressedPad;

        if (config.holdStick.has_value())
        {
          input.stickAngle = config.holdStick->first;
          input.stickMagnitude = config.holdStick->second;
          input.moveX = std::cos(input.stickAngle);
          input.moveY = std::sin(input.stickAngle);
        }

        // Edge-triggered the same way the window path delivers it, so the
        // scene reload runs exactly once per request.
        input.nextMapRequested = config.cycleMapEveryFrames != 0 &&
                                 frameIndex != 0 &&
                                 frameIndex % config.cycleMapEveryFrames == 0;
        if (!runtime.update(input))
        {
          break;
        }

        if (!config.soundDumpPath.empty())
        {
          runtime.soundEngine().mix(stepMix.data(), framesPerStep);
          soundDump.insert(soundDump.end(), stepMix.begin(), stepMix.end());
        }
        else
        {
          // Nothing is mixing, so the queue would otherwise grow for the whole
          // run.
          runtime.soundEngine().drainPendingKeyOns();
        }
      }
      if (!config.soundDumpPath.empty() &&
          orphen::harness::writeStereoWav(config.soundDumpPath, soundDump,
                                          static_cast<int>(orphen::ported::sound::kSpuBaseSampleRate)))
      {
        std::cout << "[snd] wrote " << config.soundDumpPath << " ("
                  << soundDump.size() / 2 << " frames)\n";
      }
      runtime.printExitReports();
      return 0;
    }

    // 4:3, the shape the game was displayed at. The 3D viewport letterboxes to
    // that aspect anyway, so this just avoids shipping default bars.
    orphen::port::SdlGlWindow window(
        {"Orphen Native Port Harness", config.windowWidth, config.windowHeight, config.vsync});
    orphen::port::PortRuntime runtime;

    runtime.initialize(config);

    // Sound. Windowed runs only -- a headless --frames run must not depend on an
    // audio device existing, and drains the queue itself instead. Capture runs
    // do open one, so the same command that checks a frame also exercises the
    // mixer; the audio thread cannot reach anything the capture compares.
    orphen::harness::AudioDevice audio;
    if (config.audio)
    {
      std::cout << (audio.open(&runtime.soundEngine())
                        ? "[snd] audio device open at 48 kHz\n"
                        : "[snd] no audio device; the port runs silent\n");
    }

    // The original is a 60 Hz title and every ported constant is per-frame, so
    // the simulation runs on a fixed step decoupled from the render rate. See
    // ported/original_frame_timing.h for how the original derives its own
    // timestep; on hardware that holds 60 fps it is exactly one step per frame.
    constexpr float kFixedStepSeconds = orphen::ported::kNominalFrameSeconds;
    constexpr int kMaxStepsPerFrame = 5;

    auto previousTick = std::chrono::steady_clock::now();
    float accumulatedSeconds = 0.0f;
    bool running = true;
    LoopStats loopStats;
    const bool capturing = !config.screenshotPath.empty();
    std::uint32_t renderedFrames = 0;

    if (!config.vsync && window.swapInterval() != 0)
    {
      std::cout << "[render] --no-vsync was not honoured (swap interval "
                << window.swapInterval()
                << "). Windows composites windowed surfaces through the DWM, "
                   "which paces them to the refresh regardless; frame timings "
                   "at or near the refresh interval are measuring that wait.\n";
    }

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

      // In screenshot mode the simulation advances exactly one step per
      // rendered frame regardless of wall clock. Otherwise the number of steps
      // depends on how fast the machine ran, and two builds would be
      // photographed at different points in the animation -- which would make
      // the comparison meaningless in precisely the case it is needed for.
      accumulatedSeconds = capturing ? kFixedStepSeconds : accumulatedSeconds + delta.count();

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

      // --press-confirm, so a capture run can reach the interaction path the
      // same way --frames does. Only meaningful alongside --screenshot, where
      // the step schedule is fixed and the frame number means something.
      if (std::find(config.pressConfirmFrames.begin(), config.pressConfirmFrames.end(),
                    renderedFrames + 1) != config.pressConfirmFrames.end())
      {
        constexpr std::uint16_t kRawPadCross = 0x0040;
        stepInput.rawPressedPad = static_cast<std::uint16_t>(stepInput.rawPressedPad | kRawPadCross);
        stepInput.rawHeldPad = static_cast<std::uint16_t>(stepInput.rawHeldPad | kRawPadCross);
      }

      const auto simStart = std::chrono::steady_clock::now();
      while (running && accumulatedSeconds >= kFixedStepSeconds)
      {
        running = runtime.update(stepInput);
        accumulatedSeconds -= kFixedStepSeconds;
        ++loopStats.simSteps;

        stepInput.jumpRequested = false;
        stepInput.captureSnapshotRequested = false;
        stepInput.toggleWireframeRequested = false;
        stepInput.previousMapRequested = false;
        stepInput.nextMapRequested = false;
      }
      loopStats.simMicros += microsSince(simStart);

      const auto renderStart = std::chrono::steady_clock::now();
      window.beginFrame(0.025f, 0.03f, 0.035f);
      loopStats.clearMicros += microsSince(renderStart);
      for (std::uint32_t repeat = 0; repeat < config.rendersPerFrame; ++repeat)
      {
        runtime.render(window.width(), window.height());
      }
      loopStats.renderMicros += microsSince(renderStart);

      const auto swapStart = std::chrono::steady_clock::now();
      window.swapBuffers();
      loopStats.swapMicros += microsSince(swapStart);
      ++loopStats.displayedFrames;

      ++renderedFrames;

      // 'G' during play. The runtime has already written the text; photograph
      // the frame it describes, after the swap so the buffer still holds it.
      if (std::string snapshotImage = runtime.consumePendingSnapshotImagePath();
          !snapshotImage.empty())
      {
        const bool wrote = window.captureFramebuffer(snapshotImage.c_str());
        std::cout << (wrote ? "[snapshot] wrote " : "[snapshot] FAILED to write ") << snapshotImage
                  << '\n';
      }

      if (capturing && renderedFrames >= config.screenshotFrame)
      {
        const bool wrote = window.captureFramebuffer(config.screenshotPath.c_str());
        std::cout << (wrote ? "[screenshot] wrote " : "[screenshot] FAILED to write ")
                  << config.screenshotPath << " at frame " << renderedFrames << '\n';
        break;
      }

      if (runtime.frameStatsEnabled() && runtime.frameStats().frames >= kFrameStatsWindow)
      {
        printFrameStats(loopStats, runtime.frameStats());
        loopStats = LoopStats{};
        runtime.frameStats() = orphen::harness::RenderStats{};
      }
    }

    // The headless and --load-only paths already do this. The windowed one did
    // not, so any report asked for interactively was collected and silently
    // dropped -- which matters most for the reports that can only be filled
    // from render(), since headless never calls it.
    runtime.printExitReports();
    return 0;
  }
  catch (const std::exception &error)
  {
    std::cerr << "orphen_port failed: " << error.what() << '\n';
    return 1;
  }
}

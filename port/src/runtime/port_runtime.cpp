#include "runtime/port_runtime.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace orphen::port
{
  namespace
  {

    constexpr std::uint32_t kHarnessFrameCounterAddress = 0x00001000;
    constexpr std::uint16_t kSceneScriptCategory = 1;

    std::string hex16(std::uint16_t value)
    {
      std::ostringstream stream;
      stream << std::hex << std::setw(4) << std::setfill('0') << value;
      return stream.str();
    }

    std::string hex32(std::uint32_t value)
    {
      std::ostringstream stream;
      stream << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
      return stream.str();
    }

    std::string hexOpcode(std::uint16_t value)
    {
      std::ostringstream stream;
      stream << "0x" << std::hex << std::setw(value > 0xff ? 3 : 2) << std::setfill('0') << value;
      return stream.str();
    }

    std::string byteSignature(const std::array<std::uint8_t, 4> &signature)
    {
      std::ostringstream stream;
      stream << std::hex << std::setfill('0');
      for (std::size_t byteIndex = 0; byteIndex < signature.size(); ++byteIndex)
      {
        if (byteIndex != 0)
        {
          stream << ' ';
        }
        stream << std::setw(2) << static_cast<int>(signature[byteIndex]);
      }
      return stream.str();
    }

    template <std::size_t ByteCount>
    std::string byteSequence(const std::array<std::uint8_t, ByteCount> &bytes, std::size_t count)
    {
      std::ostringstream stream;
      stream << std::hex << std::setfill('0');
      for (std::size_t byteIndex = 0; byteIndex < count && byteIndex < bytes.size(); ++byteIndex)
      {
        if (byteIndex != 0)
        {
          stream << ' ';
        }
        stream << std::setw(2) << static_cast<int>(bytes[byteIndex]);
      }
      return stream.str();
    }

  } // namespace

  void PortRuntime::initialize(const PortRuntimeConfig &config)
  {
    reset();

    if (!config.decodedPsm2Path.empty())
    {
      mapViewer_.loadDecodedPsm2(config.decodedPsm2Path);
    }
    else if (!config.discRoot.empty())
    {
      mapViewer_.loadDiscSceneMap(config.discRoot, config.discScene);
    }

    if (mapViewer_.loadedMap() != nullptr)
    {
      syncSceneScriptForLoadedMap();
      resetLeadPlayerForLoadedMap();
      if (config.printSceneTree)
      {
        mapViewer_.printLoadedSceneTree(std::cout);
      }
      const auto &stats = mapViewer_.loadedMap()->stats;
      std::cout << "[psm2] loaded " << mapViewer_.loadedSourceDescription()
                << " positions=" << stats.positionRecordCount
                << " sectionB=" << stats.sectionBRecordCount
                << " primitives=" << stats.primitiveRecordCount
                << " triangles=" << stats.triangleCount
                << " skipped=" << stats.skippedPrimitiveCount
                << " textures=" << mapViewer_.loadedTexturePageCount() << '\n';
    }
  }

  void PortRuntime::reset()
  {
    memory_.clear();
    sceneScript_.reset();
    trackedScriptGeneration_ = 0;
    frameCount_ = 0;
    mapViewer_.resetCamera();
    syncSceneScriptForLoadedMap();
    resetLeadPlayerForLoadedMap();
  }

  bool PortRuntime::update(float deltaSeconds, const InputSnapshot &input)
  {
    ++frameCount_;
    const float clampedDelta = std::clamp(deltaSeconds, 0.0f, 0.1f);
    mapViewer_.update(clampedDelta, input);

    if (mapViewer_.loadedMapGeneration() != trackedMapGeneration_)
    {
      syncSceneScriptForLoadedMap();
      resetLeadPlayerForLoadedMap();
    }
    const auto *loadedMap = mapViewer_.loadedMap();
    if (loadedMap != nullptr)
    {
      const auto cameraRelativeMove = probeCamera_.movementVectorForInput(input.moveX, input.moveY);
      leadPlayer_.update(clampedDelta, cameraRelativeMove, input.jumpRequested, loadedMap);
      probeCamera_.update(clampedDelta, input.rotateX, input.rotateY, input.zoom, leadPlayer_.viewState(), *loadedMap);
      mapViewer_.setDebugPlayerProbe(leadPlayer_.viewState());
      mapViewer_.setRuntimeCameraView(probeCamera_.view());
    }
    else
    {
      mapViewer_.setDebugPlayerProbe(std::nullopt);
      mapViewer_.setRuntimeCameraView(std::nullopt);
    }
    reportLeadPlayerGroundChange();

    memory_.write(kHarnessFrameCounterAddress, frameCount_);

    return true;
  }

  void PortRuntime::render(int framebufferWidth, int framebufferHeight) const
  {
    mapViewer_.render(framebufferWidth, framebufferHeight);
  }

  void PortRuntime::syncSceneScriptForLoadedMap()
  {
    const std::uint64_t loadedGeneration = mapViewer_.loadedMapGeneration();
    if (trackedScriptGeneration_ == loadedGeneration)
    {
      return;
    }

    trackedScriptGeneration_ = loadedGeneration;
    sceneScript_.reset();

    const auto *sceneResources = mapViewer_.loadedSceneResources();
    if (sceneResources == nullptr)
    {
      return;
    }

    const auto *scriptRecord = sceneResources->findFirst(kSceneScriptCategory);
    if (scriptRecord == nullptr)
    {
      std::cout << "[script] no category 0x0001 scene script in "
                << orphen::harness::sceneName(sceneResources->selection()) << '\n';
      return;
    }

    try
    {
      sceneScript_.loadDecodedSceneScript(orphen::harness::sceneName(sceneResources->selection()),
                                          scriptRecord->resourceId,
                                          sceneResources->decodeRecord(*scriptRecord),
                                          mapViewer_.loadedMap());
      const auto &summary = sceneScript_.summary();
      std::cout << "[script] loaded " << summary.sceneName
                << " script_" << hex16(summary.resourceId)
                << " bytes=" << summary.decodedSize
                << " sig=" << byteSignature(summary.signature)
                << " entries=";
      for (std::size_t entryIndex = 0; entryIndex < summary.entryOffsets.size(); ++entryIndex)
      {
        if (entryIndex != 0)
        {
          std::cout << ',';
        }
        std::cout << hex32(summary.entryOffsets[entryIndex]);
      }
      std::cout << " valid=" << summary.validEntryOffsetCount << '/' << summary.entryOffsets.size() << '\n';

      for (const SceneScriptTraceSummary &trace : sceneScript_.bootstrapTraces())
      {
        std::cout << "[script-vm] entry" << trace.entryIndex
                  << " start=" << hex32(trace.entryOffset)
                  << " steps=" << trace.steps
                  << " stop=" << sceneScriptTraceStopName(trace.stopReason)
                  << " at=" << hex32(static_cast<std::uint32_t>(trace.stopOffset));
        if (trace.hasStopOpcode)
        {
          std::cout << " stop-op=" << hexOpcode(trace.stopOpcode);
        }
        if (trace.stopByteCount > 0 && trace.stopReason != SceneScriptTraceStop::Completed)
        {
          std::cout << " bytes=" << byteSequence(trace.stopBytes, trace.stopByteCount);
        }
        const std::size_t terrainOpcodeCount = trace.terrainMutations.opcodeA4Count +
                                               trace.terrainMutations.opcodeA5Count +
                                               trace.terrainMutations.opcodeA6Count;
        if (terrainOpcodeCount != 0)
        {
          std::cout << " terrain=a4:" << trace.terrainMutations.opcodeA4Count
                    << ",a5:" << trace.terrainMutations.opcodeA5Count
                    << ",a6:" << trace.terrainMutations.opcodeA6Count
                    << " writes78:" << trace.terrainMutations.record78FlagWrites
                    << " lead78:" << trace.terrainMutations.record78LeadingWordWrites
                    << " writes80:" << trace.terrainMutations.record80FlagWrites;
        }
        if (!trace.events.empty())
        {
          const SceneScriptTraceEvent &event = trace.events.back();
          std::cout << " last=" << sceneScriptTraceEventName(event.kind)
                    << ':' << hexOpcode(event.opcode)
                    << " next=" << hex32(static_cast<std::uint32_t>(event.nextOffset))
                    << " depth=" << event.returnDepth;
          if (event.relativeDelta != 0)
          {
            std::cout << " delta=" << event.relativeDelta;
          }
        }
        std::cout << '\n';
      }
    }
    catch (const std::exception &error)
    {
      std::cout << "[script] failed to load " << orphen::harness::sceneName(sceneResources->selection())
                << " script_" << hex16(scriptRecord->resourceId)
                << ": " << error.what() << '\n';
    }
  }

  void PortRuntime::resetLeadPlayerForLoadedMap()
  {
    const auto *loadedMap = mapViewer_.loadedMap();
    trackedMapGeneration_ = mapViewer_.loadedMapGeneration();
    reportedGroundPrimitive_.reset();
    if (loadedMap == nullptr)
    {
      mapViewer_.setDebugPlayerProbe(std::nullopt);
      mapViewer_.setRuntimeCameraView(std::nullopt);
      return;
    }

    leadPlayer_.resetToMap(*loadedMap);
    probeCamera_.resetToProbe(leadPlayer_.viewState(), *loadedMap);
    mapViewer_.setDebugPlayerProbe(leadPlayer_.viewState());
    mapViewer_.setRuntimeCameraView(probeCamera_.view());
  }

  void PortRuntime::reportLeadPlayerGroundChange()
  {
    const auto &leadState = leadPlayer_.viewState();
    if (!leadState.groundHit.has_value())
    {
      if (reportedGroundPrimitive_.has_value())
      {
        std::cout << "[probe] ground=none\n";
        reportedGroundPrimitive_.reset();
      }
      return;
    }

    const auto &groundHit = *leadState.groundHit;
    if (reportedGroundPrimitive_ == groundHit.primitiveIndex)
    {
      return;
    }

    reportedGroundPrimitive_ = groundHit.primitiveIndex;
    std::cout << "[probe] primitive=" << groundHit.primitiveIndex
              << " triangle=" << groundHit.triangleIndex
              << " z=" << groundHit.height
              << " leading=0x" << std::hex << groundHit.leadingWord
              << " terrain=0x" << groundHit.terrainFlags << std::dec
              << (groundHit.sampledByOriginalTerrain ? " sampled" : " unsampled") << '\n';
  }

} // namespace orphen::port

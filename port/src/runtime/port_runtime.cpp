#include "runtime/port_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
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

    std::string hexRgb(std::uint32_t value)
    {
      std::ostringstream stream;
      stream << "0x" << std::hex << std::setw(6) << std::setfill('0') << (value & 0xffffffu);
      return stream.str();
    }

    orphen::ported::psm2::Vec3 cameraVectorFromScript(const SceneScriptCameraRuntimeState::Vector &vector)
    {
      return {vector.x, vector.y, vector.z};
    }

    float distanceBetween(const orphen::ported::psm2::Vec3 &left, const orphen::ported::psm2::Vec3 &right)
    {
      const float deltaX = left.x - right.x;
      const float deltaY = left.y - right.y;
      const float deltaZ = left.z - right.z;
      return std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
    }

    std::uint32_t scriptPrimaryControlMask(const PlayerDebugProbeState &leadState)
    {
      if (!leadState.groundHit.has_value())
      {
        return 0;
      }

      return leadState.groundHit->terrainFlags | leadState.groundHit->leadingWord;
    }

    std::size_t cameraMutationCount(const SceneScriptCameraMutationStats &cameraMutations)
    {
      return cameraMutations.resetRequests +
             cameraMutations.pairedPoseWrites +
             cameraMutations.eyeWrites +
             cameraMutations.targetWrites +
             cameraMutations.distanceWrites;
    }

    std::uint64_t scriptTraceStopKey(const SceneScriptTraceSummary &trace)
    {
      return (static_cast<std::uint64_t>(trace.entryIndex & 0xffffu) << 48) |
             (static_cast<std::uint64_t>(trace.stopOffset & 0xffffffffu) << 16) |
             static_cast<std::uint64_t>(trace.stopOpcode & 0xffffu);
    }

    std::uint64_t scriptRegisterTraceKey(const SceneScriptTraceSummary &trace)
    {
      std::uint64_t key = 1469598103934665603ull;
      const auto mix = [&key](std::uint64_t value)
      {
        key ^= value;
        key *= 1099511628211ull;
      };
      mix(trace.entryIndex);
      mix(trace.entryOffset);
      mix(trace.stopOffset);
      mix(trace.registerMutations.totalWrites);
      for (std::size_t sampleIndex = 0; sampleIndex < trace.registerMutations.sampleCount; ++sampleIndex)
      {
        const SceneScriptRegisterWriteSample &sample = trace.registerMutations.samples[sampleIndex];
        mix(sample.offset);
        mix(sample.opcode);
        mix(sample.selector);
        mix(sample.bank);
        mix(sample.registerId);
      }
      mix(trace.globalParameterMutations.totalWrites);
      for (std::size_t sampleIndex = 0; sampleIndex < trace.globalParameterMutations.sampleCount; ++sampleIndex)
      {
        const SceneScriptGlobalParameterSample &sample = trace.globalParameterMutations.samples[sampleIndex];
        mix(sample.offset);
        mix(sample.opcode);
        mix(sample.valueCount);
      }
      return key;
    }

    bool scriptFlagBit(const SceneScriptVmState &state, std::uint32_t flagId)
    {
      const std::size_t byteIndex = flagId >> 3;
      return byteIndex < state.flags.size() && (state.flags[byteIndex] & (1u << (flagId & 7u))) != 0;
    }

    std::string opcodeCountList(const SceneScriptKnownOpcodeStats &stats)
    {
      std::ostringstream stream;
      bool wroteAny = false;
      for (std::size_t opcode = 0; opcode < stats.standardOpcodeCounts.size(); ++opcode)
      {
        const std::size_t count = stats.standardOpcodeCounts[opcode];
        if (count == 0)
        {
          continue;
        }

        if (wroteAny)
        {
          stream << ',';
        }
        stream << hexOpcode(static_cast<std::uint16_t>(opcode)) << ':' << count;
        wroteAny = true;
      }
      return stream.str();
    }

    std::string globalParameterList(const SceneScriptGlobalParameterMutationStats &stats)
    {
      std::ostringstream stream;
      stream << "writes:" << stats.totalWrites;
      if (stats.sampleCount != 0)
      {
        stream << " samples:";
        for (std::size_t sampleIndex = 0; sampleIndex < stats.sampleCount; ++sampleIndex)
        {
          const SceneScriptGlobalParameterSample &sample = stats.samples[sampleIndex];
          if (sampleIndex != 0)
          {
            stream << ';';
          }
          stream << hex32(static_cast<std::uint32_t>(sample.offset))
                 << ':' << hexOpcode(sample.opcode)
                 << " values=";
          for (std::size_t valueIndex = 0; valueIndex < sample.valueCount; ++valueIndex)
          {
            if (valueIndex != 0)
            {
              stream << '/';
            }
            stream << sample.values[valueIndex];
          }
        }
      }

      return stream.str();
    }

    std::string registerWriteList(const SceneScriptRegisterMutationStats &stats)
    {
      std::ostringstream stream;
      stream << "writes:" << stats.totalWrites;
      if (stats.sampleCount != 0)
      {
        stream << " samples:";
        for (std::size_t sampleIndex = 0; sampleIndex < stats.sampleCount; ++sampleIndex)
        {
          const SceneScriptRegisterWriteSample &sample = stats.samples[sampleIndex];
          if (sampleIndex != 0)
          {
            stream << ';';
          }
          stream << hex32(static_cast<std::uint32_t>(sample.offset))
                 << ':' << hexOpcode(sample.opcode)
                 << " sel=" << sample.selector
                 << " bank=" << sample.bank
                 << " r=" << sample.registerId
                 << " old=" << sample.previousValue
                 << " val=" << sample.operandValue
                 << " new=" << sample.writtenValue;
        }
      }

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
    reportedScriptCameraOverride_.reset();
    reportedScriptCameraDistance_.reset();
    reportedScriptFrameStop_.reset();
    reportedScriptRegisterTraces_.clear();
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
    auto *loadedMap = mapViewer_.loadedMap();
    if (loadedMap != nullptr)
    {
      const auto cameraRelativeMove = probeCamera_.movementVectorForInput(input.moveX, input.moveY);
      leadPlayer_.update(clampedDelta, cameraRelativeMove, input.jumpRequested, loadedMap);
      const auto frameTrace = sceneScript_.runFrameTick(loadedMap, scriptPrimaryControlMask(leadPlayer_.viewState()));
      if (frameTrace.has_value() && frameTrace->stopReason != SceneScriptTraceStop::Completed)
      {
        const std::uint64_t stopKey = scriptTraceStopKey(*frameTrace);
        if (!reportedScriptFrameStop_.has_value() || *reportedScriptFrameStop_ != stopKey)
        {
          std::cout << "[script-vm] frame entry" << frameTrace->entryIndex
                    << " start=" << hex32(frameTrace->entryOffset)
                    << " steps=" << frameTrace->steps
                    << " stop=" << sceneScriptTraceStopName(frameTrace->stopReason)
                    << " at=" << hex32(static_cast<std::uint32_t>(frameTrace->stopOffset));
          if (frameTrace->hasStopOpcode)
          {
            std::cout << " stop-op=" << hexOpcode(frameTrace->stopOpcode);
          }
          if (frameTrace->stopByteCount > 0)
          {
            std::cout << " bytes=" << byteSequence(frameTrace->stopBytes, frameTrace->stopByteCount);
          }
          if (!frameTrace->events.empty())
          {
            const SceneScriptTraceEvent &event = frameTrace->events.back();
            std::cout << " last=" << sceneScriptTraceEventName(event.kind)
                      << ':' << hexOpcode(event.opcode)
                      << " next=" << hex32(static_cast<std::uint32_t>(event.nextOffset));
          }
          std::cout << '\n';
          reportedScriptFrameStop_ = stopKey;
        }
      }
      else if (frameTrace.has_value() &&
               (frameTrace->registerMutations.totalWrites != 0 || frameTrace->globalParameterMutations.totalWrites != 0))
      {
        const std::uint64_t registerTraceKey = scriptRegisterTraceKey(*frameTrace);
        if (reportedScriptRegisterTraces_.insert(registerTraceKey).second)
        {
          std::cout << "[script-vm] frame entry" << frameTrace->entryIndex
                    << " start=" << hex32(frameTrace->entryOffset)
                    << " steps=" << frameTrace->steps
                    << " stop=" << sceneScriptTraceStopName(frameTrace->stopReason)
                    << " at=" << hex32(static_cast<std::uint32_t>(frameTrace->stopOffset));
          if (frameTrace->knownOpcodes.totalStandardOpcodes != 0)
          {
            std::cout << " known=" << opcodeCountList(frameTrace->knownOpcodes);
          }
          if (frameTrace->registerMutations.totalWrites != 0)
          {
            std::cout << " reg=" << registerWriteList(frameTrace->registerMutations);
          }
          if (frameTrace->globalParameterMutations.totalWrites != 0)
          {
            std::cout << " global=" << globalParameterList(frameTrace->globalParameterMutations);
          }
          std::cout << '\n';
        }
      }
      const auto &vmState = sceneScript_.vmState();
      const SceneScriptCoroutineSlot &coroutineSlot = vmState.coroutineSlots[0];
      const std::uint64_t coroutineStateKey = (static_cast<std::uint64_t>(coroutineSlot.tableOffset) << 32) |
                                              static_cast<std::uint64_t>(coroutineSlot.returnWord & 0xffffu);
      if (coroutineSlot.tableOffset != 0 &&
          (!reportedScriptCoroutineState_.has_value() || *reportedScriptCoroutineState_ != coroutineStateKey))
      {
        std::cout << "[script-coroutine] slot=0 table=" << hex32(coroutineSlot.tableOffset)
                  << " timer=" << (coroutineSlot.timer >> 5)
                  << " return=" << coroutineSlot.returnWord
                  << " flags=3:" << (scriptFlagBit(vmState, 3) ? 1 : 0)
                  << ",67:" << (scriptFlagBit(vmState, 0x67) ? 1 : 0)
                  << ",68:" << (scriptFlagBit(vmState, 0x68) ? 1 : 0)
                  << ",69:" << (scriptFlagBit(vmState, 0x69) ? 1 : 0)
                  << ",6a:" << (scriptFlagBit(vmState, 0x6a) ? 1 : 0)
                  << ",6b:" << (scriptFlagBit(vmState, 0x6b) ? 1 : 0)
                  << ",7a:" << (scriptFlagBit(vmState, 0x7a) ? 1 : 0)
                  << ",7b:" << (scriptFlagBit(vmState, 0x7b) ? 1 : 0);
        if (const auto record = sceneScript_.coroutineRecord(0))
        {
          std::cout << " delay=" << record->delay
                    << " condition=0x" << hex16(record->condition)
                    << " ready=" << (record->conditionReady ? 1 : 0)
                    << " target=" << hex32(record->scriptOffset);
        }
        std::cout << '\n';
        reportedScriptCoroutineState_ = coroutineStateKey;
      }
      const auto &scriptCamera = sceneScript_.runtimeState().camera;
      const bool scriptCameraOverrideActive = scriptCamera.overrideEnabled && scriptCamera.eye.hasValue && scriptCamera.target.hasValue;
      if (scriptCamera.hasDistance)
      {
        if (!reportedScriptCameraDistance_.has_value() ||
            std::abs(*reportedScriptCameraDistance_ - scriptCamera.distance) > 0.001f)
        {
          std::cout << "[script-camera] b8 raw=" << scriptCamera.rawDistance
                    << " distance=" << scriptCamera.distance
                    << " near=" << scriptCamera.nearPlane;
          if (scriptCameraOverrideActive)
          {
            std::cout << " eye-target=" << distanceBetween(cameraVectorFromScript(scriptCamera.eye), cameraVectorFromScript(scriptCamera.target));
          }
          else
          {
            std::cout << " pose=inactive";
          }
          std::cout << '\n';
          reportedScriptCameraDistance_ = scriptCamera.distance;
        }
        probeCamera_.setScriptDistance(scriptCamera.distance, leadPlayer_.viewState(), *loadedMap);
      }
      if (!reportedScriptCameraOverride_.has_value() || *reportedScriptCameraOverride_ != scriptCameraOverrideActive)
      {
        if (scriptCameraOverrideActive)
        {
          std::cout << "[script-camera] eye=" << scriptCamera.eye.x << '/' << scriptCamera.eye.y << '/' << scriptCamera.eye.z
                    << " target=" << scriptCamera.target.x << '/' << scriptCamera.target.y << '/' << scriptCamera.target.z
                    << " separation=" << distanceBetween(cameraVectorFromScript(scriptCamera.eye), cameraVectorFromScript(scriptCamera.target)) << '\n';
        }
        else if (reportedScriptCameraOverride_.value_or(false))
        {
          std::cout << "[script-camera] cleared\n";
        }
        reportedScriptCameraOverride_ = scriptCameraOverrideActive;
      }
      if (scriptCameraOverrideActive)
      {
        probeCamera_.setScriptPose(cameraVectorFromScript(scriptCamera.eye),
                                   cameraVectorFromScript(scriptCamera.target),
                                   *loadedMap);
      }
      else
      {
        probeCamera_.clearScriptPose(leadPlayer_.viewState(), *loadedMap);
      }
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
        if (cameraMutationCount(trace.cameraMutations) != 0)
        {
          const auto &camera = trace.runtimeState.camera;
          std::cout << " camera=";
          bool wroteCamera = false;
          if (trace.cameraMutations.distanceWrites != 0)
          {
            std::cout << "b8:" << trace.cameraMutations.distanceWrites
                      << " raw:" << camera.rawDistance
                      << " distance:" << camera.distance
                      << " near:" << camera.nearPlane;
            wroteCamera = true;
          }
          if (trace.cameraMutations.resetRequests != 0)
          {
            std::cout << (wroteCamera ? "," : "") << "reset:" << trace.cameraMutations.resetRequests;
            wroteCamera = true;
          }
          if ((trace.cameraMutations.pairedPoseWrites != 0 || trace.cameraMutations.eyeWrites != 0) && camera.eye.hasValue)
          {
            std::cout << (wroteCamera ? "," : "")
                      << "eye:" << camera.eye.x << '/' << camera.eye.y << '/' << camera.eye.z;
            wroteCamera = true;
          }
          if ((trace.cameraMutations.pairedPoseWrites != 0 || trace.cameraMutations.targetWrites != 0) && camera.target.hasValue)
          {
            std::cout << (wroteCamera ? "," : "")
                      << "target:" << camera.target.x << '/' << camera.target.y << '/' << camera.target.z;
          }
        }
        const std::size_t visualMutationCount = trace.visualMutations.globalRgbWrites +
                                                trace.visualMutations.vectorRgbWrites +
                                                trace.visualMutations.simpleVectorWrites +
                                                trace.visualMutations.indexedDualRgbEvents +
                                                trace.visualMutations.color1Writes +
                                                trace.visualMutations.color2Writes +
                                                trace.visualMutations.fadeRadiusWrites;
        if (visualMutationCount != 0)
        {
          const auto &visual = trace.runtimeState.visual;
          std::cout << " visual=";
          bool wroteVisual = false;
          if (trace.visualMutations.globalRgbWrites != 0 && visual.globalRgb.hasValue)
          {
            std::cout << "rgb:" << hexRgb(visual.globalRgb.packedRgb);
            wroteVisual = true;
          }
          if (trace.visualMutations.vectorRgbWrites != 0 && visual.vectorRgb.hasValue)
          {
            std::cout << (wroteVisual ? "," : "")
                      << "vec:" << visual.vectorRgb.x << '/' << visual.vectorRgb.y << '/' << visual.vectorRgb.z
                      << '@' << hexRgb(visual.vectorRgb.packedRgb);
            wroteVisual = true;
          }
          if (trace.visualMutations.indexedDualRgbEvents != 0 && visual.indexedDualRgbEvent.hasValue)
          {
            std::cout << (wroteVisual ? "," : "")
                      << "dual:" << visual.indexedDualRgbEvent.index
                      << '/' << hexRgb(visual.indexedDualRgbEvent.color1)
                      << '/' << hexRgb(visual.indexedDualRgbEvent.color2)
                      << '/' << visual.indexedDualRgbEvent.parameter;
            wroteVisual = true;
          }
          if (trace.visualMutations.color1Writes != 0 && visual.color1.hasValue)
          {
            std::cout << (wroteVisual ? "," : "") << "color1:" << hexRgb(visual.color1.packedRgb);
            wroteVisual = true;
          }
          if (trace.visualMutations.color2Writes != 0 && visual.color2.hasValue)
          {
            std::cout << (wroteVisual ? "," : "") << "color2:" << hexRgb(visual.color2.packedRgb);
            wroteVisual = true;
          }
          if (trace.visualMutations.fadeRadiusWrites != 0 && visual.fadeRadii.hasValue)
          {
            std::cout << (wroteVisual ? "," : "")
                      << "fade:" << visual.fadeRadii.innerRadius << '/' << visual.fadeRadii.outerRadius;
          }
        }
        if (trace.knownOpcodes.totalStandardOpcodes != 0)
        {
          std::cout << " known=" << opcodeCountList(trace.knownOpcodes);
        }
        if (trace.registerMutations.totalWrites != 0)
        {
          std::cout << " reg=" << registerWriteList(trace.registerMutations);
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
    reportedScriptCameraOverride_.reset();
    reportedScriptCameraDistance_.reset();
    reportedScriptFrameStop_.reset();
    reportedScriptRegisterTraces_.clear();
    reportedScriptCoroutineState_.reset();
    if (loadedMap == nullptr)
    {
      mapViewer_.setDebugPlayerProbe(std::nullopt);
      mapViewer_.setRuntimeCameraView(std::nullopt);
      return;
    }

    leadPlayer_.resetToMap(*loadedMap);
    probeCamera_.resetToProbe(leadPlayer_.viewState(), *loadedMap);
    if (sceneScript_.runtimeState().camera.hasDistance)
    {
      probeCamera_.setScriptDistance(sceneScript_.runtimeState().camera.distance, leadPlayer_.viewState(), *loadedMap);
    }
    if (sceneScript_.runtimeState().camera.overrideEnabled &&
        sceneScript_.runtimeState().camera.eye.hasValue &&
        sceneScript_.runtimeState().camera.target.hasValue)
    {
      probeCamera_.setScriptPose(cameraVectorFromScript(sceneScript_.runtimeState().camera.eye),
                                 cameraVectorFromScript(sceneScript_.runtimeState().camera.target),
                                 *loadedMap);
    }
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

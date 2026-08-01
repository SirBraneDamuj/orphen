#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace orphen::ported::psm2
{
  struct Psm2RuntimeState;
}

namespace orphen::port
{

  enum class SceneScriptTraceStop
  {
    Completed,
    InvalidEntryOffset,
    OutOfBoundsRead,
    StepLimitReached,
    ReturnStackLimitReached,
    RequiresVmEvaluation,
    StandardOpcodeDispatch,
    ExtendedOpcodeDispatch,
    InvalidRelativeTarget,
  };

  enum class SceneScriptTraceEventKind
  {
    Noop,
    BlockBegin,
    BlockEnd,
    RelativeAdvance,
    SkipInlineWord,
    LowOpcodeRequiresVm,
    ConditionalBranch,
    StandardOpcode,
    ExtendedOpcode,
  };

  struct SceneScriptTraceEvent
  {
    SceneScriptTraceEventKind kind = SceneScriptTraceEventKind::Noop;
    std::size_t offset = 0;
    std::uint16_t opcode = 0;
    std::size_t nextOffset = 0;
    std::int64_t relativeDelta = 0;
    std::size_t returnDepth = 0;
  };

  struct SceneScriptTerrainMutationStats
  {
    std::size_t opcodeA4Count = 0;
    std::size_t opcodeA5Count = 0;
    std::size_t opcodeA6Count = 0;
    std::size_t record78FlagWrites = 0;
    std::size_t record78LeadingWordWrites = 0;
    std::size_t record80FlagWrites = 0;
  };

  struct SceneScriptCameraRuntimeState
  {
    struct Vector
    {
      bool hasValue = false;
      std::array<std::int32_t, 3> raw{};
      float x = 0.0f;
      float y = 0.0f;
      float z = 0.0f;
    };

    bool hasDistance = false;
    bool overrideEnabled = false;
    std::int32_t rawDistance = 0;
    float distance = 0.0f;
    float nearPlane = 0.0f;
    Vector eye;
    Vector target;
  };

  struct SceneScriptRuntimeState
  {
    SceneScriptCameraRuntimeState camera;
    struct
    {
      struct
      {
        bool hasValue = false;
        std::uint32_t packedRgb = 0;
      } globalRgb, color1, color2;
      struct
      {
        bool hasValue = false;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        std::uint32_t packedRgb = 0;
      } vectorRgb;
      struct
      {
        bool hasValue = false;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
      } simpleVector;
      struct
      {
        bool hasValue = false;
        std::uint32_t index = 0;
        std::uint32_t color1 = 0;
        std::uint32_t color2 = 0;
        std::uint32_t parameter = 0;
      } indexedDualRgbEvent;
      struct
      {
        bool hasValue = false;
        float innerRadius = 0.0f;
        float outerRadius = 0.0f;
      } fadeRadii;
    } visual;
  };

  struct SceneScriptCameraMutationStats
  {
    std::size_t resetRequests = 0;
    std::size_t pairedPoseWrites = 0;
    std::size_t eyeWrites = 0;
    std::size_t targetWrites = 0;
    std::size_t distanceWrites = 0;
  };

  struct SceneScriptVisualMutationStats
  {
    std::size_t globalRgbWrites = 0;
    std::size_t vectorRgbWrites = 0;
    std::size_t simpleVectorWrites = 0;
    std::size_t indexedDualRgbEvents = 0;
    std::size_t color1Writes = 0;
    std::size_t color2Writes = 0;
    std::size_t fadeRadiusWrites = 0;
  };

  struct SceneScriptKnownOpcodeStats
  {
    std::array<std::size_t, 256> standardOpcodeCounts{};
    std::size_t totalStandardOpcodes = 0;
  };

  struct SceneScriptRegisterWriteSample
  {
    std::size_t offset = 0;
    std::uint8_t opcode = 0;
    std::uint32_t selector = 0;
    std::uint32_t bank = 0;
    std::uint32_t registerId = 0;
    std::uint32_t previousValue = 0;
    std::uint32_t operandValue = 0;
    std::uint32_t writtenValue = 0;
  };

  struct SceneScriptRegisterMutationStats
  {
    std::size_t totalWrites = 0;
    std::array<std::size_t, 256> registerWriteCounts{};
    std::array<SceneScriptRegisterWriteSample, 8> samples{};
    std::size_t sampleCount = 0;
  };

  struct SceneScriptGlobalParameterSample
  {
    std::size_t offset = 0;
    std::uint8_t opcode = 0;
    std::array<std::int32_t, 3> values{};
    std::size_t valueCount = 0;
  };

  struct SceneScriptGlobalParameterMutationStats
  {
    std::size_t totalWrites = 0;
    std::array<SceneScriptGlobalParameterSample, 8> samples{};
    std::size_t sampleCount = 0;
  };

  struct SceneScriptCoroutineSlot
  {
    std::uint32_t tableOffset = 0;
    std::uint32_t timer = 0;
    std::uint32_t returnWord = 0;
  };

  struct SceneScriptVmState
  {
    std::array<std::uint32_t, 128> work{};
    std::array<std::uint8_t, 2304> flags{};
    std::array<std::array<std::uint32_t, 0x41>, 0x101> objectRegisters{};
    std::uint32_t currentObjectRegisterBank = 0;
    std::array<std::uint32_t, 0x41> scriptSlots{};
    std::array<SceneScriptCoroutineSlot, 4> coroutineSlots{};
    std::int32_t currentScriptSlot = -1;
    std::uint32_t primaryControlMask = 0;
    std::uint32_t alternateControlMask = 0;
    std::uint32_t controllerGateFlags = 1;
    SceneScriptRuntimeState runtimeState;
  };

  struct SceneScriptTraceSummary
  {
    std::size_t entryIndex = 0;
    std::uint32_t entryOffset = 0;
    std::size_t steps = 0;
    std::size_t stopOffset = 0;
    bool hasStopOpcode = false;
    std::uint16_t stopOpcode = 0;
    std::array<std::uint8_t, 12> stopBytes{};
    std::size_t stopByteCount = 0;
    SceneScriptTerrainMutationStats terrainMutations;
    SceneScriptCameraMutationStats cameraMutations;
    SceneScriptVisualMutationStats visualMutations;
    SceneScriptRegisterMutationStats registerMutations;
    SceneScriptGlobalParameterMutationStats globalParameterMutations;
    SceneScriptKnownOpcodeStats knownOpcodes;
    SceneScriptRuntimeState runtimeState;
    SceneScriptTraceStop stopReason = SceneScriptTraceStop::InvalidEntryOffset;
    std::vector<SceneScriptTraceEvent> events;
  };

  struct SceneScriptTraceOptions
  {
    std::size_t maxSteps = 512;
    std::size_t maxExpressionSteps = 128;
    std::size_t maxExpressionDepth = 32;
    std::size_t maxReturnDepth = 32;
    std::size_t maxEvents = 64;
  };

  std::vector<SceneScriptTraceSummary> traceSceneScriptEntrypoints(std::span<const std::uint8_t> scriptBytes,
                                                                   std::span<const std::uint32_t> entryOffsets,
                                                                   SceneScriptTraceOptions options = {});
  std::vector<SceneScriptTraceSummary> traceSceneScriptEntrypoints(std::span<const std::uint8_t> scriptBytes,
                                                                   std::span<const std::uint32_t> entryOffsets,
                                                                   orphen::ported::psm2::Psm2RuntimeState &terrainState,
                                                                   SceneScriptTraceOptions options = {});
  SceneScriptTraceSummary traceSceneScriptEntrypoint(std::span<const std::uint8_t> scriptBytes,
                                                     std::size_t entryIndex,
                                                     std::uint32_t entryOffset,
                                                     SceneScriptVmState &vmState,
                                                     orphen::ported::psm2::Psm2RuntimeState *terrainState = nullptr,
                                                     SceneScriptTraceOptions options = {});

  std::string_view sceneScriptTraceStopName(SceneScriptTraceStop stopReason);
  std::string_view sceneScriptTraceEventName(SceneScriptTraceEventKind eventKind);

} // namespace orphen::port

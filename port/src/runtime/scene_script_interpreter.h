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

  std::string_view sceneScriptTraceStopName(SceneScriptTraceStop stopReason);
  std::string_view sceneScriptTraceEventName(SceneScriptTraceEventKind eventKind);

} // namespace orphen::port

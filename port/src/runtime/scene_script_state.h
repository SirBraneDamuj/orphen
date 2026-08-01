#pragma once

#include "runtime/scene_script_interpreter.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace orphen::ported::psm2
{
  struct Psm2RuntimeState;
}

namespace orphen::port
{

  struct SceneScriptSummary
  {
    std::string sceneName;
    std::uint16_t resourceId = 0;
    std::size_t decodedSize = 0;
    std::array<std::uint8_t, 4> signature{};
    std::array<std::uint32_t, 5> entryOffsets{};
    std::size_t validEntryOffsetCount = 0;
  };

  struct SceneScriptCoroutineRecordView
  {
    std::uint32_t tableOffset = 0;
    std::uint16_t delay = 0;
    std::uint16_t condition = 0;
    std::uint32_t scriptOffset = 0;
    bool conditionReady = false;
  };

  class SceneScriptState
  {
  public:
    void reset();
    void loadDecodedSceneScript(std::string sceneName,
                                std::uint16_t resourceId,
                                std::vector<std::uint8_t> decodedScript,
                                orphen::ported::psm2::Psm2RuntimeState *terrainState = nullptr);
    std::optional<SceneScriptTraceSummary> runFrameTick(orphen::ported::psm2::Psm2RuntimeState *terrainState,
                                                        std::uint32_t primaryControlMask,
                                                        std::uint32_t alternateControlMask = 0);

    bool hasScript() const { return !decodedScript_.empty(); }
    const SceneScriptSummary &summary() const { return summary_; }
    const std::vector<SceneScriptTraceSummary> &bootstrapTraces() const { return bootstrapTraces_; }
    const SceneScriptRuntimeState &runtimeState() const { return runtimeState_; }
    const SceneScriptVmState &vmState() const { return vmState_; }
    std::optional<SceneScriptCoroutineRecordView> coroutineRecord(std::size_t slotIndex) const;

  private:
    std::vector<std::uint8_t> decodedScript_;
    SceneScriptSummary summary_;
    std::vector<SceneScriptTraceSummary> bootstrapTraces_;
    SceneScriptVmState vmState_;
    SceneScriptRuntimeState runtimeState_;

    void fillStopMetadata(SceneScriptTraceSummary &trace) const;
  };

} // namespace orphen::port

#pragma once

#include "runtime/scene_script_interpreter.h"

#include <array>
#include <cstddef>
#include <cstdint>
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

  class SceneScriptState
  {
  public:
    void reset();
    void loadDecodedSceneScript(std::string sceneName,
                                std::uint16_t resourceId,
                                std::vector<std::uint8_t> decodedScript,
                                orphen::ported::psm2::Psm2RuntimeState *terrainState = nullptr);

    bool hasScript() const { return !decodedScript_.empty(); }
    const SceneScriptSummary &summary() const { return summary_; }
    const std::vector<SceneScriptTraceSummary> &bootstrapTraces() const { return bootstrapTraces_; }

  private:
    std::vector<std::uint8_t> decodedScript_;
    SceneScriptSummary summary_;
    std::vector<SceneScriptTraceSummary> bootstrapTraces_;
  };

} // namespace orphen::port

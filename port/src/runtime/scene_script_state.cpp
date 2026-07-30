#include "runtime/scene_script_state.h"

#include "ported/resource/mcb_runtime.h"

#include <algorithm>
#include <span>
#include <stdexcept>

namespace orphen::port
{

  void SceneScriptState::reset()
  {
    decodedScript_.clear();
    summary_ = {};
    bootstrapTraces_.clear();
  }

  void SceneScriptState::loadDecodedSceneScript(std::string sceneName,
                                                std::uint16_t resourceId,
                                                std::vector<std::uint8_t> decodedScript,
                                                orphen::ported::psm2::Psm2RuntimeState *terrainState)
  {
    if (decodedScript.size() < 20)
    {
      throw std::runtime_error("scene script blob is too small for the five-entry SCR header");
    }

    decodedScript_ = std::move(decodedScript);
    const std::span<const std::uint8_t> scriptBytes(decodedScript_.data(), decodedScript_.size());

    summary_ = {};
    summary_.sceneName = std::move(sceneName);
    summary_.resourceId = resourceId;
    summary_.decodedSize = decodedScript_.size();
    std::copy_n(decodedScript_.begin(), summary_.signature.size(), summary_.signature.begin());

    for (std::size_t entryIndex = 0; entryIndex < summary_.entryOffsets.size(); ++entryIndex)
    {
      const std::uint32_t entryOffset = orphen::ported::resource::readLeU32(scriptBytes, entryIndex * 4);
      summary_.entryOffsets[entryIndex] = entryOffset;
      if (entryOffset < decodedScript_.size())
      {
        ++summary_.validEntryOffsetCount;
      }
    }

    if (terrainState != nullptr)
    {
      bootstrapTraces_ = traceSceneScriptEntrypoints(scriptBytes,
                                                     std::span<const std::uint32_t>(summary_.entryOffsets.data(),
                                                                                    summary_.entryOffsets.size()),
                                                     *terrainState);
    }
    else
    {
      bootstrapTraces_ = traceSceneScriptEntrypoints(scriptBytes,
                                                     std::span<const std::uint32_t>(summary_.entryOffsets.data(),
                                                                                    summary_.entryOffsets.size()));
    }
    for (SceneScriptTraceSummary &trace : bootstrapTraces_)
    {
      if (trace.stopOffset < decodedScript_.size())
      {
        trace.hasStopOpcode = true;
        trace.stopOpcode = decodedScript_[trace.stopOffset];
        if (trace.stopOpcode == 0xff && trace.stopOffset + 1 < decodedScript_.size())
        {
          trace.stopOpcode = static_cast<std::uint16_t>(0x100u + decodedScript_[trace.stopOffset + 1]);
        }
        trace.stopByteCount = std::min(trace.stopBytes.size(), decodedScript_.size() - trace.stopOffset);
        std::copy_n(decodedScript_.begin() + static_cast<std::ptrdiff_t>(trace.stopOffset),
                    trace.stopByteCount,
                    trace.stopBytes.begin());
      }
    }
  }

} // namespace orphen::port

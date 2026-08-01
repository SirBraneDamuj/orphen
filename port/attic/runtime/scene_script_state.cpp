#include "runtime/scene_script_state.h"

#include "ported/resource/mcb_runtime.h"

#include <algorithm>
#include <span>
#include <stdexcept>

namespace orphen::port
{
  namespace
  {

    constexpr std::uint32_t kCoroutineFrameDelta = 32;
    constexpr std::size_t kCoroutineRecordSize = 8;

    bool canRead(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t byteCount)
    {
      return offset <= bytes.size() && byteCount <= bytes.size() - offset;
    }

    std::uint16_t readLeU16Unchecked(std::span<const std::uint8_t> bytes, std::size_t offset)
    {
      return static_cast<std::uint16_t>(bytes[offset]) |
             static_cast<std::uint16_t>(bytes[offset + 1] << 8);
    }

    bool readFlagBit(const SceneScriptVmState &state, std::uint32_t flagId)
    {
      const std::size_t byteIndex = static_cast<std::size_t>(flagId >> 3);
      const std::uint8_t bitMask = static_cast<std::uint8_t>(1u << (flagId & 7u));
      return byteIndex < state.flags.size() && (state.flags[byteIndex] & bitMask) != 0;
    }

    void writeFlagBit(SceneScriptVmState &state, std::uint32_t flagId, bool value)
    {
      const std::size_t byteIndex = static_cast<std::size_t>(flagId >> 3);
      if (byteIndex >= state.flags.size())
      {
        return;
      }

      const std::uint8_t bitMask = static_cast<std::uint8_t>(1u << (flagId & 7u));
      if (value)
      {
        state.flags[byteIndex] = static_cast<std::uint8_t>(state.flags[byteIndex] | bitMask);
      }
      else
      {
        state.flags[byteIndex] = static_cast<std::uint8_t>(state.flags[byteIndex] & ~bitMask);
      }
    }

    bool coroutineConditionReady(const SceneScriptVmState &state, std::uint16_t condition)
    {
      if (condition == 0)
      {
        return true;
      }
      if ((condition & 0x8000u) == 0)
      {
        return readFlagBit(state, condition);
      }

      const std::uint32_t controlMask = state.primaryControlMask | state.alternateControlMask | 0x8000u;
      return (condition & controlMask) == condition;
    }

    std::optional<std::uint32_t> chainedBlockOffset(std::span<const std::uint8_t> scriptBytes,
                                                    const SceneScriptTraceSummary &trace,
                                                    std::uint32_t firstCodeOffset)
    {
      if (trace.stopReason != SceneScriptTraceStop::Completed ||
          trace.stopOffset == 0 ||
          trace.stopOffset > scriptBytes.size() ||
          scriptBytes[trace.stopOffset - 1] != 0x04 ||
          !canRead(scriptBytes, trace.stopOffset, sizeof(std::uint32_t)))
      {
        return std::nullopt;
      }

      const std::uint32_t chainTag = orphen::ported::resource::readLeU32(scriptBytes, trace.stopOffset);
      const std::uint32_t chainedOffset = static_cast<std::uint32_t>(trace.stopOffset + sizeof(std::uint32_t));
      if (chainTag == 0 || chainTag > 0xffffu || chainedOffset < firstCodeOffset || chainedOffset >= scriptBytes.size())
      {
        return std::nullopt;
      }

      return chainedOffset;
    }

    std::size_t applyDialogueFlagSetters(std::span<const std::uint8_t> scriptBytes,
                                         std::uint32_t streamOffset,
                                         std::uint32_t firstCodeOffset,
                                         SceneScriptVmState &state)
    {
      if (streamOffset >= firstCodeOffset || streamOffset >= scriptBytes.size())
      {
        return 0;
      }

      const std::size_t scanEnd = std::min<std::size_t>(firstCodeOffset, scriptBytes.size());
      std::size_t flagsSet = 0;
      for (std::size_t cursor = streamOffset; cursor < scanEnd; ++cursor)
      {
        if (scriptBytes[cursor] == 0x1a)
        {
          break;
        }
        if (scriptBytes[cursor] != 0x1b || !canRead(scriptBytes, cursor + 1, sizeof(std::uint16_t)))
        {
          continue;
        }

        writeFlagBit(state, readLeU16Unchecked(scriptBytes, cursor + 1), true);
        ++flagsSet;
        cursor += sizeof(std::uint16_t);
      }
      return flagsSet;
    }

  } // namespace

  void SceneScriptState::reset()
  {
    decodedScript_.clear();
    summary_ = {};
    bootstrapTraces_.clear();
    vmState_ = {};
    runtimeState_ = {};
  }

  void SceneScriptState::fillStopMetadata(SceneScriptTraceSummary &trace) const
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

  std::optional<SceneScriptCoroutineRecordView> SceneScriptState::coroutineRecord(std::size_t slotIndex) const
  {
    if (slotIndex >= vmState_.coroutineSlots.size())
    {
      return std::nullopt;
    }

    const SceneScriptCoroutineSlot &slot = vmState_.coroutineSlots[slotIndex];
    if (slot.tableOffset == 0)
    {
      return std::nullopt;
    }

    const std::span<const std::uint8_t> scriptBytes(decodedScript_.data(), decodedScript_.size());
    if (!canRead(scriptBytes, slot.tableOffset, kCoroutineRecordSize))
    {
      return std::nullopt;
    }

    SceneScriptCoroutineRecordView record;
    record.tableOffset = slot.tableOffset;
    record.delay = readLeU16Unchecked(scriptBytes, slot.tableOffset);
    record.condition = readLeU16Unchecked(scriptBytes, slot.tableOffset + 2);
    record.scriptOffset = orphen::ported::resource::readLeU32(scriptBytes, slot.tableOffset + 4);
    record.conditionReady = coroutineConditionReady(vmState_, record.condition);
    return record;
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

    vmState_ = {};
    bootstrapTraces_.clear();
    bootstrapTraces_.reserve(summary_.entryOffsets.size());
    if (terrainState != nullptr)
    {
      for (std::size_t entryIndex = 0; entryIndex < summary_.entryOffsets.size(); ++entryIndex)
      {
        bootstrapTraces_.push_back(traceSceneScriptEntrypoint(scriptBytes,
                                                              entryIndex,
                                                              summary_.entryOffsets[entryIndex],
                                                              vmState_,
                                                              terrainState));
      }
    }
    else
    {
      for (std::size_t entryIndex = 0; entryIndex < summary_.entryOffsets.size(); ++entryIndex)
      {
        bootstrapTraces_.push_back(traceSceneScriptEntrypoint(scriptBytes,
                                                              entryIndex,
                                                              summary_.entryOffsets[entryIndex],
                                                              vmState_));
      }
    }
    runtimeState_ = vmState_.runtimeState;
    for (SceneScriptTraceSummary &trace : bootstrapTraces_)
    {
      fillStopMetadata(trace);
    }
  }

  std::optional<SceneScriptTraceSummary> SceneScriptState::runFrameTick(orphen::ported::psm2::Psm2RuntimeState *terrainState,
                                                                        std::uint32_t primaryControlMask,
                                                                        std::uint32_t alternateControlMask)
  {
    constexpr std::size_t kSceneTickEntryIndex = 2;
    if (decodedScript_.empty() || kSceneTickEntryIndex >= summary_.entryOffsets.size())
    {
      return std::nullopt;
    }
    if (summary_.entryOffsets[kSceneTickEntryIndex] >= decodedScript_.size())
    {
      return std::nullopt;
    }

    vmState_.primaryControlMask = primaryControlMask;
    vmState_.alternateControlMask = alternateControlMask;
    vmState_.controllerGateFlags = 1;

    const std::span<const std::uint8_t> scriptBytes(decodedScript_.data(), decodedScript_.size());
    std::uint32_t firstCodeOffset = static_cast<std::uint32_t>(decodedScript_.size());
    for (const std::uint32_t entryOffset : summary_.entryOffsets)
    {
      if (entryOffset < decodedScript_.size())
      {
        firstCodeOffset = std::min(firstCodeOffset, entryOffset);
      }
    }

    SceneScriptTraceSummary trace = traceSceneScriptEntrypoint(scriptBytes,
                                                               kSceneTickEntryIndex,
                                                               summary_.entryOffsets[kSceneTickEntryIndex],
                                                               vmState_,
                                                               terrainState);
    std::optional<SceneScriptTraceSummary> lastTrace = trace;
    fillStopMetadata(*lastTrace);

    for (std::size_t slotIndex = 0; slotIndex < 0x40 && slotIndex < vmState_.scriptSlots.size(); ++slotIndex)
    {
      const std::uint32_t scriptOffset = vmState_.scriptSlots[slotIndex];
      if (scriptOffset == 0 || scriptOffset >= decodedScript_.size())
      {
        continue;
      }

      const std::int32_t previousScriptSlot = vmState_.currentScriptSlot;
      vmState_.currentScriptSlot = static_cast<std::int32_t>(slotIndex);
      SceneScriptTraceSummary slotTrace = traceSceneScriptEntrypoint(scriptBytes,
                                                                     0x200u + slotIndex,
                                                                     scriptOffset,
                                                                     vmState_,
                                                                     terrainState);
      vmState_.currentScriptSlot = previousScriptSlot;
      fillStopMetadata(slotTrace);
      if (slotTrace.stopReason == SceneScriptTraceStop::Completed && vmState_.scriptSlots[slotIndex] == 0)
      {
        if (const std::optional<std::uint32_t> chainedOffset = chainedBlockOffset(scriptBytes, slotTrace, firstCodeOffset))
        {
          vmState_.scriptSlots[slotIndex] = *chainedOffset;
        }
      }
      lastTrace = slotTrace;
    }

    for (std::size_t slotIndex = 0; slotIndex < vmState_.coroutineSlots.size(); ++slotIndex)
    {
      SceneScriptCoroutineSlot &slot = vmState_.coroutineSlots[slotIndex];
      if (slot.tableOffset == 0)
      {
        continue;
      }
      if (!canRead(scriptBytes, slot.tableOffset, kCoroutineRecordSize))
      {
        slot.tableOffset = 0;
        slot.timer = 0;
        continue;
      }

      const std::uint16_t delay = readLeU16Unchecked(scriptBytes, slot.tableOffset);
      const std::uint16_t condition = readLeU16Unchecked(scriptBytes, slot.tableOffset + 2);
      const std::uint32_t scriptOffset = orphen::ported::resource::readLeU32(scriptBytes, slot.tableOffset + 4);
      if (scriptOffset == 0)
      {
        slot.tableOffset = 0;
        slot.timer = 0;
        continue;
      }
      if (!coroutineConditionReady(vmState_, condition))
      {
        continue;
      }
      if (((slot.timer >> 5) & 0xffffu) < delay)
      {
        slot.timer += kCoroutineFrameDelta;
        continue;
      }

      const std::uint32_t scheduledTableOffset = slot.tableOffset;
      if (scriptOffset < firstCodeOffset)
      {
        applyDialogueFlagSetters(scriptBytes, scriptOffset, firstCodeOffset, vmState_);
        writeFlagBit(vmState_, 0x8ff, true);
        writeFlagBit(vmState_, 0x8fe, true);
      }
      else if (scriptOffset < decodedScript_.size())
      {
        std::uint32_t chainedScriptOffset = scriptOffset;
        SceneScriptTraceSummary coroutineTrace = traceSceneScriptEntrypoint(scriptBytes,
                                                                            0x100u + slotIndex,
                                                                            chainedScriptOffset,
                                                                            vmState_,
                                                                            terrainState);
        fillStopMetadata(coroutineTrace);
        lastTrace = coroutineTrace;
        for (std::size_t chainCount = 0; chainCount < 8; ++chainCount)
        {
          const std::optional<std::uint32_t> chainedOffset = chainedBlockOffset(scriptBytes, coroutineTrace, firstCodeOffset);
          if (!chainedOffset.has_value())
          {
            break;
          }

          chainedScriptOffset = *chainedOffset;
          coroutineTrace = traceSceneScriptEntrypoint(scriptBytes,
                                                      0x100u + slotIndex,
                                                      chainedScriptOffset,
                                                      vmState_,
                                                      terrainState);
          fillStopMetadata(coroutineTrace);
          lastTrace = coroutineTrace;
        }
      }

      if (slot.tableOffset == scheduledTableOffset)
      {
        slot.tableOffset += kCoroutineRecordSize;
        ++slot.returnWord;
        if (!canRead(scriptBytes, slot.tableOffset, kCoroutineRecordSize) ||
            orphen::ported::resource::readLeU32(scriptBytes, slot.tableOffset + 4) == 0)
        {
          slot.tableOffset = 0;
        }
        slot.timer = 0;
      }
    }

    runtimeState_ = vmState_.runtimeState;
    return lastTrace;
  }

} // namespace orphen::port

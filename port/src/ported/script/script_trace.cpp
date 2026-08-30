#include "ported/script/script_trace.h"

#include <iostream>

namespace orphen::ported::script
{

  void ScriptTrace::reset()
  {
    opcodes_.clear();
    spawns_.clear();
    preloadedResources_.clear();
    registeredScripts_.clear();
    entriesRun_.clear();
    objectRegisters_.clear();
    terrainTriggers_.clear();
    eventFlagChanges_.clear();
    objectMethods_.clear();
    eventDispatches_.clear();
    eventStreamsArmed_.clear();
    frame_ = 0;
    fadesArmed_.clear();
    playerLocks_.clear();
    battleBootCount_ = 0;
    sceneChangeCount_ = 0;
    lastSceneChange_ = 0;
    tickRunCount_ = 0;
    slotRunCount_ = 0;
    leadTeleported_ = false;
    leadTeleportX_ = 0.0f;
    leadTeleportY_ = 0.0f;
    leadTeleportZ_ = 0.0f;
  }

  const char *opcodeSupportName(OpcodeSupport support)
  {
    switch (support)
    {
    case OpcodeSupport::Modelled: return "modelled";
    case OpcodeSupport::OperandsOnly: return "operands-only";
    case OpcodeSupport::Unimplemented: return "UNIMPLEMENTED";
    default: return "?";
    }
  }

  void ScriptTrace::recordOpcode(std::uint16_t opcode, std::uint32_t offset, OpcodeSupport support)
  {
    tracingCurrentOpcode_ = traceRangeSet_ && offset >= traceRangeLow_ && offset <= traceRangeHigh_;
    if (tracingCurrentOpcode_)
    {
      std::cout << "[scr] f=" << frame_ << " @0x" << std::hex << offset << " op=0x" << opcode
                << std::dec << ' ' << opcodeSupportName(support) << '\n';
    }

    auto entry = opcodes_.find(opcode);
    if (entry == opcodes_.end())
    {
      OpcodeStat stat;
      stat.hitCount = 1;
      stat.firstOffset = offset;
      stat.support = support;
      opcodes_.emplace(opcode, stat);
      return;
    }
    ++entry->second.hitCount;
    entry->second.support = support;
  }

  void ScriptTrace::recordLeadTeleport(float x, float y, float z)
  {
    leadTeleported_ = true;
    leadTeleportX_ = x;
    leadTeleportY_ = y;
    leadTeleportZ_ = z;
  }

  void ScriptTrace::noteEntryRun(const std::string &name, std::uint32_t offset, bool empty)
  {
    entriesRun_.push_back(name + " @0x" + [offset]
                          {
                            static const char *digits = "0123456789abcdef";
                            std::string text;
                            for (int shift = 28; shift >= 0; shift -= 4)
                            {
                              text.push_back(digits[(offset >> shift) & 0xF]);
                            }
                            return text;
                          }() +
                          (empty ? " (empty)" : ""));
  }

  void ScriptTrace::recordTerrainTrigger(std::uint32_t offset,
                                         std::uint32_t mask,
                                         std::uint8_t selector,
                                         std::uint32_t word,
                                         bool passed)
  {
    auto &stat = terrainTriggers_[offset];
    stat.mask = mask;
    stat.selector = selector;
    stat.observedWord = word;
    ++stat.tests;
    if (passed)
    {
      ++stat.passes;
    }
  }

  void ScriptTrace::recordFadeArmed(std::uint32_t bank, std::uint32_t rate, std::uint32_t packedRgb)
  {
    for (auto &event : fadesArmed_)
    {
      if (event.bank == bank && event.rate == rate && event.packedRgb == packedRgb)
      {
        ++event.hits;
        return;
      }
    }
    fadesArmed_.push_back({bank, rate, packedRgb, 1});
  }

  void ScriptTrace::recordFrameFeedback(std::uint8_t alpha,
                                        bool withTransform,
                                        const std::int16_t *transform)
  {
    ++frameFeedback_.alphaWrites;
    if (alpha != 0)
    {
      ++frameFeedback_.nonZeroAlphaWrites;
    }
    if (alpha > frameFeedback_.peakAlpha)
    {
      frameFeedback_.peakAlpha = alpha;
    }
    if (withTransform && transform != nullptr)
    {
      ++frameFeedback_.transformWrites;
      for (std::size_t index = 0; index < frameFeedback_.lastTransform.size(); ++index)
      {
        frameFeedback_.lastTransform[index] = transform[index];
      }
    }
  }

  void ScriptTrace::noteFadeTrackArmed(std::uint32_t track, std::int32_t durationFrames)
  {
    auto &entry = fadeTracksArmed_[track];
    ++entry.arms;
    entry.lastDurationFrames = durationFrames;
  }

  void ScriptTrace::recordPlayerLock(std::int8_t mode)
  {
    ++playerLocks_[static_cast<std::int32_t>(mode)];
  }

  void ScriptTrace::recordEventFlagChange(const EventFlagChange &change)
  {
    eventFlagChanges_.push_back(change);
  }

  void ScriptTrace::recordObjectMethod(std::uint32_t method)
  {
    ++objectMethods_[method];
  }

  void ScriptTrace::recordEventDispatch(const EventDispatch &dispatch)
  {
    eventDispatches_.push_back(dispatch);
  }

  void ScriptTrace::recordEventStreamArmed(const EventStreamArmed &armed)
  {
    eventStreamsArmed_.push_back(armed);
  }

  void ScriptTrace::recordObjectRegisterAccess(std::uint32_t index, bool write)
  {
    auto &stat = objectRegisters_[index];
    if (write)
    {
      ++stat.writes;
    }
    else
    {
      ++stat.reads;
    }
  }

  void ScriptTrace::recordObjectRegisterValue(std::uint32_t index, std::int32_t slot, std::uint32_t value)
  {
    auto &stat = objectRegisters_[index];
    stat.lastSlot = slot;
    stat.lastValue = value;
  }

  void ScriptTrace::recordUnmodelledObjectRegister(std::uint32_t index, bool noEntity)
  {
    auto &stat = objectRegisters_[index];
    ++stat.unmodelledHits;
    if (noEntity)
    {
      ++stat.noEntityHits;
    }
  }

  std::uint32_t ScriptTrace::unmodelledObjectRegisterHits() const
  {
    std::uint32_t count = 0;
    for (const auto &entry : objectRegisters_)
    {
      count += entry.second.unmodelledHits;
    }
    return count;
  }

  namespace
  {
    std::uint32_t countOpcodes(const std::map<std::uint16_t, OpcodeStat> &opcodes,
                               OpcodeSupport support,
                               bool byHit)
    {
      std::uint32_t count = 0;
      for (const auto &entry : opcodes)
      {
        if (entry.second.support == support)
        {
          count += byHit ? entry.second.hitCount : 1u;
        }
      }
      return count;
    }
  } // namespace

  std::uint32_t ScriptTrace::unimplementedOpcodeCount() const
  {
    return countOpcodes(opcodes_, OpcodeSupport::Unimplemented, false);
  }

  std::uint32_t ScriptTrace::unimplementedHitCount() const
  {
    return countOpcodes(opcodes_, OpcodeSupport::Unimplemented, true);
  }

  std::uint32_t ScriptTrace::operandsOnlyOpcodeCount() const
  {
    return countOpcodes(opcodes_, OpcodeSupport::OperandsOnly, false);
  }

  std::uint32_t ScriptTrace::operandsOnlyHitCount() const
  {
    return countOpcodes(opcodes_, OpcodeSupport::OperandsOnly, true);
  }

} // namespace orphen::ported::script

#include "ported/script/script_trace.h"

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
    fadesArmed_.clear();
    playerLocks_.clear();
    battleBootCount_ = 0;
    tickRunCount_ = 0;
    slotRunCount_ = 0;
    leadTeleported_ = false;
    leadTeleportX_ = 0.0f;
    leadTeleportY_ = 0.0f;
    leadTeleportZ_ = 0.0f;
  }

  void ScriptTrace::recordOpcode(std::uint16_t opcode, std::uint32_t offset, bool implemented)
  {
    auto entry = opcodes_.find(opcode);
    if (entry == opcodes_.end())
    {
      OpcodeStat stat;
      stat.hitCount = 1;
      stat.firstOffset = offset;
      stat.implemented = implemented;
      opcodes_.emplace(opcode, stat);
      return;
    }
    ++entry->second.hitCount;
    entry->second.implemented = implemented;
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

  void ScriptTrace::recordPlayerLock(std::int8_t mode)
  {
    ++playerLocks_[static_cast<std::int32_t>(mode)];
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

  std::uint32_t ScriptTrace::unimplementedOpcodeCount() const
  {
    std::uint32_t count = 0;
    for (const auto &entry : opcodes_)
    {
      if (!entry.second.implemented)
      {
        ++count;
      }
    }
    return count;
  }

  std::uint32_t ScriptTrace::unimplementedHitCount() const
  {
    std::uint32_t count = 0;
    for (const auto &entry : opcodes_)
    {
      if (!entry.second.implemented)
      {
        count += entry.second.hitCount;
      }
    }
    return count;
  }

} // namespace orphen::ported::script

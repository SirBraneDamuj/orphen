#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace orphen::ported::script
{

  // Records what the scene script actually did, so the port can report an
  // evidence-based inventory rather than a guess. There is no PCSX2 trace
  // comparison, so this is how a decode desync makes itself visible: a stream
  // that has gone out of sync produces a flood of opcodes that were never
  // implemented, at implausible offsets.
  struct OpcodeStat
  {
    std::uint32_t hitCount = 0;
    std::uint32_t firstOffset = 0; // offset into the script blob of the first hit
    bool implemented = false;
  };

  // One entity the script asked for. Recorded even when the spawn failed, so the
  // report can distinguish "the script never asked" from "we could not do it".
  struct SpawnRecord
  {
    std::uint32_t scriptOffset = 0;
    std::int32_t typeId = 0;
    std::size_t slot = 0;
    bool allocated = false;
    bool descriptorResolved = false;
    bool positioned = false;
    bool grounded = false; // opcode 0x55 found terrain under it
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
  };

  class ScriptTrace
  {
  public:
    void reset();

    void recordOpcode(std::uint16_t opcode, std::uint32_t offset, bool implemented);
    void recordPreloadedResource(std::uint16_t resourceId) { preloadedResources_.push_back(resourceId); }
    void recordRegisteredScript(std::uint32_t scriptId) { registeredScripts_.push_back(scriptId); }
    void recordLeadTeleport(float x, float y, float z);
    SpawnRecord &beginSpawn() { return spawns_.emplace_back(); }
    SpawnRecord *lastSpawn() { return spawns_.empty() ? nullptr : &spawns_.back(); }

    void noteEntryRun(const std::string &name, std::uint32_t offset, bool empty);

    const std::map<std::uint16_t, OpcodeStat> &opcodes() const { return opcodes_; }
    const std::vector<SpawnRecord> &spawns() const { return spawns_; }
    const std::vector<std::uint16_t> &preloadedResources() const { return preloadedResources_; }
    const std::vector<std::uint32_t> &registeredScripts() const { return registeredScripts_; }
    const std::vector<std::string> &entriesRun() const { return entriesRun_; }

    bool leadTeleported() const { return leadTeleported_; }
    float leadTeleportX() const { return leadTeleportX_; }
    float leadTeleportY() const { return leadTeleportY_; }
    float leadTeleportZ() const { return leadTeleportZ_; }

    std::uint32_t unimplementedOpcodeCount() const;
    std::uint32_t unimplementedHitCount() const;

  private:
    std::map<std::uint16_t, OpcodeStat> opcodes_;
    std::vector<SpawnRecord> spawns_;
    std::vector<std::uint16_t> preloadedResources_;
    std::vector<std::uint32_t> registeredScripts_;
    std::vector<std::string> entriesRun_;

    bool leadTeleported_ = false;
    float leadTeleportX_ = 0.0f;
    float leadTeleportY_ = 0.0f;
    float leadTeleportZ_ = 0.0f;
  };

} // namespace orphen::ported::script

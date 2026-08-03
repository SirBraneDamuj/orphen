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

    // Opcodes 0x76..0x7C address entity fields through FUN_0025c548 /
    // FUN_0025c8f8. An index the port has no field for still has to go
    // somewhere, and the side array it lands in is read by nothing -- so those
    // are counted here and named in the report rather than dropped quietly.
    //
    // "No case in the original" is a third, entirely different thing: indices
    // such as 0x12, 0x24, 0x25, 0x27 and 0x31 fall into both functions' default,
    // which writes nothing and reads zero. The port answering zero for those is
    // exactly right, so they must not be reported as a gap.
    struct ObjectRegisterStat
    {
      std::uint32_t reads = 0;
      std::uint32_t writes = 0;
      std::uint32_t unmodelledHits = 0;
      std::uint32_t noEntityHits = 0; // no object was selected at all
    };
    // Opcode 0x61 (FUN_0025f4b8) is the terrain trigger: it tests the lead
    // player's +0x6C or +0x70 -- the words copied off whatever surface the
    // player last settled on -- against a mask. A floor panel is not an entity
    // and not a volume; it is terrain carrying a flag that the scene's per-frame
    // entry watches for. Recording each call site with its mask is the only way
    // to find out which flag a scene's panels actually use, because the branch
    // stays untaken until someone stands on one.
    struct TerrainTriggerStat
    {
      std::uint32_t mask = 0;
      std::uint8_t selector = 0;
      std::uint32_t tests = 0;
      std::uint32_t passes = 0;
      std::uint32_t observedWord = 0; // last value the test saw
    };
    void recordTerrainTrigger(std::uint32_t offset,
                              std::uint32_t mask,
                              std::uint8_t selector,
                              std::uint32_t word,
                              bool passed);
    const std::map<std::uint32_t, TerrainTriggerStat> &terrainTriggers() const { return terrainTriggers_; }

    // Outcomes a floor panel reached. Recorded because the port can only carry
    // them part way: 0x6D's lock does not hold without the lead's state-10
    // handler, and 0xE1 has no battle system to hand off to.
    //
    // Opcode 0x85 / 0x87 arming the fullscreen fade. Its second operand is the
    // per-tick rate, not an event id -- FUN_0025d1c0 stores it at the bank's
    // +0x2, which FUN_0025d238 then multiplies by the frame tick.
    struct FadeArmed
    {
      std::uint32_t bank = 0;
      std::uint32_t rate = 0;
      std::uint32_t packedRgb = 0;
      std::uint32_t hits = 0;
    };
    void recordFadeArmed(std::uint32_t bank, std::uint32_t rate, std::uint32_t packedRgb);
    const std::vector<FadeArmed> &fadesArmed() const { return fadesArmed_; }

    void recordPlayerLock(std::int8_t mode);
    void recordBattleBoot() { ++battleBootCount_; }
    const std::map<std::int32_t, std::uint32_t> &playerLocks() const { return playerLocks_; }
    std::uint32_t battleBootCount() const { return battleBootCount_; }

    void recordObjectRegisterAccess(std::uint32_t index, bool write);
    void recordUnmodelledObjectRegister(std::uint32_t index, bool noEntity);
    const std::map<std::uint32_t, ObjectRegisterStat> &objectRegisters() const { return objectRegisters_; }
    std::uint32_t unmodelledObjectRegisterHits() const;

    // The per-frame entry runs every frame, so it is counted rather than listed;
    // 600 identical lines in entriesRun_ would bury everything else.
    void recordTickRun() { ++tickRunCount_; }
    void recordSlotRun() { ++slotRunCount_; }
    std::uint32_t tickRunCount() const { return tickRunCount_; }
    std::uint32_t slotRunCount() const { return slotRunCount_; }

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
    std::map<std::uint32_t, ObjectRegisterStat> objectRegisters_;
    std::map<std::uint32_t, TerrainTriggerStat> terrainTriggers_;
    std::vector<FadeArmed> fadesArmed_;
    std::map<std::int32_t, std::uint32_t> playerLocks_;
    std::uint32_t battleBootCount_ = 0;
    std::uint32_t tickRunCount_ = 0;
    std::uint32_t slotRunCount_ = 0;

    bool leadTeleported_ = false;
    float leadTeleportX_ = 0.0f;
    float leadTeleportY_ = 0.0f;
    float leadTeleportZ_ = 0.0f;
  };

} // namespace orphen::ported::script

#pragma once

#include <array>
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
  // How much of an opcode the port actually reproduces. "The script kept
  // running" and "the script did the right thing" are different claims, and a
  // report that conflates them is a report that hides gaps -- so they are
  // counted apart.
  enum class OpcodeSupport
  {
    // Operands consumed and the effect reproduced.
    Modelled,
    // Operands consumed exactly as the original reads them, effect deliberately
    // not modelled. The stream stays in sync and the scene keeps running, which
    // is the only way a long cutscene chain gets exercised at all. The arity
    // must come from the original -- a guess desyncs everything after it.
    OperandsOnly,
    // Not decoded at all. The stream halts here rather than inventing operands.
    Unimplemented,
  };

  const char *opcodeSupportName(OpcodeSupport support);

  struct OpcodeStat
  {
    std::uint32_t hitCount = 0;
    std::uint32_t firstOffset = 0; // offset into the script blob of the first hit
    OpcodeSupport support = OpcodeSupport::Unimplemented;
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

    void recordOpcode(std::uint16_t opcode, std::uint32_t offset, OpcodeSupport support);

    // True while the opcode being interpreted sits inside --scr-trace-range.
    // recordOpcode latches it, so a handler can print its decoded operands
    // under the same window that selected the opcode line above them.
    bool tracingCurrentOpcode() const { return tracingCurrentOpcode_; }
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
      // Which entity the last access picked, and what it saw. A cutscene that
      // polls a register until it reaches a value looks identical in the counts
      // whether it is waiting patiently or waiting forever; the value is what
      // tells the two apart.
      std::int32_t lastSlot = -1;
      std::uint32_t lastValue = 0;
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

    // Opcodes 0xC8 / 0xC9 driving the screen smear. Summarised rather than
    // listed for the reason the colour ramps below are: a script holds the
    // effect by rewriting the alpha every frame, so the interesting numbers
    // are how many frames it was up for, how strong it got, and whether any
    // caller reached for the transform at all.
    struct FrameFeedbackUse
    {
      std::uint32_t alphaWrites = 0;
      std::uint32_t transformWrites = 0;
      std::uint32_t nonZeroAlphaWrites = 0;
      std::uint32_t peakAlpha = 0;
      std::array<std::int16_t, 5> lastTransform{};
    };
    void recordFrameFeedback(std::uint8_t alpha, bool withTransform, const std::int16_t *transform);
    const FrameFeedbackUse &frameFeedback() const { return frameFeedback_; }

    // Opcode 0x9A arming one of the sixteen colour ramps. Counted per track
    // rather than listed: the ping-pong ones re-arm for the whole scene, so a
    // list would be thousands of entries saying the same thing. What matters is
    // *which* tracks a scene uses, how often each turns around, and the last
    // duration -- a track that never re-arms is one whose 0x9B never reported
    // finished, which is the failure this subsystem has.
    struct FadeTrackArmed
    {
      std::uint32_t arms = 0;
      std::int32_t lastDurationFrames = 0;
    };
    void noteFadeTrackArmed(std::uint32_t track, std::int32_t durationFrames);
    const std::map<std::uint32_t, FadeTrackArmed> &fadeTracksArmed() const { return fadeTracksArmed_; }

    // FUN_0025ce30 paying out one record. Recorded rather than counted because
    // the order and the timing *are* the cutscene: a stream that fires its
    // records in the wrong order, or all on one frame, is the failure mode this
    // subsystem has, and neither is visible from a total.
    struct EventDispatch
    {
      std::uint32_t frame = 0;
      std::uint8_t channel = 0;
      std::uint16_t delayUnits = 0;
      std::uint16_t gate = 0;
      std::uint32_t targetOffset = 0;
      bool toDialogue = false;  // inside the pointer-table window
      std::int32_t slot = -1;   // the object-script slot it was queued into
    };
    void recordEventDispatch(const EventDispatch &dispatch);
    const std::vector<EventDispatch> &eventDispatches() const { return eventDispatches_; }

    // Opcode 0xA1 arming a channel, so the report can say which streams a scene
    // asked for even when none of their records has come due yet.
    struct EventStreamArmed
    {
      std::uint32_t frame = 0;
      std::uint8_t channel = 0;
      std::uint32_t streamOffset = 0;
    };
    void recordEventStreamArmed(const EventStreamArmed &armed);
    const std::vector<EventStreamArmed> &eventStreamsArmed() const { return eventStreamsArmed_; }

    // The frame counter the two records above stamp themselves with. Set once a
    // frame by the caller; zero during the load-time entries.
    void setFrame(std::uint32_t frame) { frame_ = frame; }
    std::uint32_t frame() const { return frame_; }

    // --scr-trace-range. Every opcode executed inside [low, high] is printed as
    // it runs, which is how a script body gets read: the aggregate report says
    // an opcode was reached, this says in what order and which way a branch
    // went. Feed it the offsets the report already prints.
    void setTraceRange(std::uint32_t low, std::uint32_t high)
    {
      traceRangeLow_ = low;
      traceRangeHigh_ = high;
      traceRangeSet_ = true;
    }

    // Every event-flag transition opcodes 0x3E..0x40 actually cause. The flags
    // are how a scene records that a beat finished -- s01_e012's opening latches
    // 0x515 when it hands control to the player -- and until they were listed
    // there was no way to watch a cutscene make progress.
    struct EventFlagChange
    {
      std::uint32_t frame = 0;
      std::uint32_t flagId = 0;
      std::uint32_t scriptOffset = 0;
      bool set = false;
    };
    void recordEventFlagChange(const EventFlagChange &change);
    const std::vector<EventFlagChange> &eventFlagChanges() const { return eventFlagChanges_; }

    // Opcode 0xBD (FUN_00263e80 -> FUN_00242a18): the object-method dispatcher.
    // Its method ids are a second, entirely separate instruction set, and 0x70
    // / 0x71 / 0x72 are the voice-line play, tune and poll calls. Counting them
    // by id is how the report can say a scene tried to speak, even though
    // VOICE.BIN is not in the disc root and nothing can be played.
    void recordObjectMethod(std::uint32_t method);
    const std::map<std::uint32_t, std::uint32_t> &objectMethods() const { return objectMethods_; }

    void recordPlayerLock(std::int8_t mode);
    void recordBattleBoot() { ++battleBootCount_; }

    // Opcode 0x8E, the scene transition. Recorded rather than performed, so
    // --scr-report can say which scene a chain wanted to leave for.
    void recordSceneChange(std::int32_t destination)
    {
      ++sceneChangeCount_;
      lastSceneChange_ = destination;
    }
    std::uint32_t sceneChangeCount() const { return sceneChangeCount_; }
    std::int32_t lastSceneChange() const { return lastSceneChange_; }
    const std::map<std::int32_t, std::uint32_t> &playerLocks() const { return playerLocks_; }
    std::uint32_t battleBootCount() const { return battleBootCount_; }

    void recordObjectRegisterAccess(std::uint32_t index, bool write);
    void recordObjectRegisterValue(std::uint32_t index, std::int32_t slot, std::uint32_t value);
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
    std::uint32_t operandsOnlyOpcodeCount() const;
    std::uint32_t operandsOnlyHitCount() const;

  private:
    std::map<std::uint16_t, OpcodeStat> opcodes_;
    std::vector<SpawnRecord> spawns_;
    std::vector<std::uint16_t> preloadedResources_;
    std::vector<std::uint32_t> registeredScripts_;
    std::vector<std::string> entriesRun_;
    std::map<std::uint32_t, ObjectRegisterStat> objectRegisters_;
    std::map<std::uint32_t, TerrainTriggerStat> terrainTriggers_;
    std::vector<EventFlagChange> eventFlagChanges_;
    std::map<std::uint32_t, std::uint32_t> objectMethods_;
    std::vector<EventDispatch> eventDispatches_;
    std::vector<EventStreamArmed> eventStreamsArmed_;
    std::uint32_t frame_ = 0;
    bool tracingCurrentOpcode_ = false;
    bool traceRangeSet_ = false;
    std::uint32_t traceRangeLow_ = 0;
    std::uint32_t traceRangeHigh_ = 0;
    std::vector<FadeArmed> fadesArmed_;
    FrameFeedbackUse frameFeedback_;
    std::map<std::uint32_t, FadeTrackArmed> fadeTracksArmed_;
    std::map<std::int32_t, std::uint32_t> playerLocks_;
    std::uint32_t battleBootCount_ = 0;
    std::uint32_t sceneChangeCount_ = 0;
    std::int32_t lastSceneChange_ = 0;
    std::uint32_t tickRunCount_ = 0;
    std::uint32_t slotRunCount_ = 0;

    bool leadTeleported_ = false;
    float leadTeleportX_ = 0.0f;
    float leadTeleportY_ = 0.0f;
    float leadTeleportZ_ = 0.0f;
  };

} // namespace orphen::ported::script

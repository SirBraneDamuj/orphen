#pragma once

#include "ported/entity/entity_descriptor_table.h"
#include "ported/entity/entity_pool.h"
#include "ported/original_frame_timing.h"
#include "ported/psm2/psm2_runtime.h"
#include "ported/script/script_trace.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace orphen::ported::script
{

  // Native counterpart of the SCR bytecode VM:
  //   src/FUN_0025bc68.c  the statement interpreter, three dispatch tables
  //   src/FUN_0025c258.c  the expression evaluator
  //   src/FUN_0025bf70.c  the literal decoder
  //   src/FUN_0025c220.c  relative jump
  //
  // See analyzed/script_expression_evaluator.c. The three stream globals are one
  // contiguous group off GP -- pbGpffffbd60 and DAT_00355cd0 are the same
  // address (0x00355CD0), so there is a single stream pointer here, not two.
  //
  // Only the opcodes s01_e024 reaches are implemented. Everything else is
  // recorded by the trace and returns zero rather than guessing, which is what
  // makes a decode desync loud instead of silent.

  // fGpffff8c40 / DAT_00352bdc / DAT_00352ca0, all 100000.0 in the retail
  // executable. Combined with the 0x0F literal's built-in *100, a script value
  // of 1000 is one world unit. This is a script-to-world scale and is unrelated
  // to the 4096 fixed point used elsewhere in the engine.
  constexpr float kScriptCoordinateScale = 100000.0f;

  // Script-visible state that outlives one entry point. These are globals in the
  // original, so they persist across the init and start runs the way they do on
  // the PS2.
  struct SceneScriptState
  {
    // DAT_00355060: 128 words, cleared by FUN_0025b390 at scene load.
    static constexpr std::size_t kWorkWordCount = 128;
    std::uint32_t DAT_00355060_work[kWorkWordCount]{};

    // DAT_00342b70: the game-wide flag array. Opcodes 0x36..0x39 address it by a
    // bit index that must be a multiple of 8 and below 0x47F8, then use
    // index >> 3 as a byte index -- so despite the naming it is a byte array,
    // not a bitfield, at least on this path.
    static constexpr std::size_t kFlagBucketCount = 0x47F8 / 8 + 1;
    std::uint8_t DAT_00342b70_flags[kFlagBucketCount]{};

    // DAT_00571d00 / DAT_0035504c: the 16-entry table opcode 0x4E pushes into
    // and opcode 0x51 batch-spawns from. FUN_0025b6d0 clears the counter.
    struct LookupEntry
    {
      std::uint32_t value1 = 0;
      std::uint32_t value3 = 0;
      std::uint32_t value2 = 0;
    };
    static constexpr std::size_t kLookupCapacity = 0x10;
    LookupEntry DAT_00571d00_lookup[kLookupCapacity]{};
    std::size_t DAT_0035504c_lookupCount = 0;

    // uGpffffb6fc / uGpffffb700 and the vector at 0x3439C8. Kept so the values
    // are inspectable; the port has no lighting model to feed them to yet.
    std::uint32_t uGpffffb6fc_globalRgb = 0;
    std::uint32_t uGpffffb700_vectorRgb = 0;
    float DAT_003439c8_vector[3]{};

    // Per-object register banks, read and written by opcodes 0x76..0x7C through
    // FUN_0025c548 / FUN_0025c8f8. 0x101 banks of 0x41 words: one per pool slot
    // plus the "current object" bank at index 0x100. Held in a vector because
    // the whole thing is 66 KB and this struct is owned by value.
    static constexpr std::size_t kObjectRegisterBanks = 0x101;
    static constexpr std::size_t kObjectRegistersPerBank = 0x41;
    std::vector<std::uint32_t> objectRegisters =
        std::vector<std::uint32_t>(kObjectRegisterBanks * kObjectRegistersPerBank, 0u);

    std::uint32_t &objectRegister(std::size_t bank, std::size_t index)
    {
      static std::uint32_t discard = 0;
      if (bank >= kObjectRegisterBanks || index >= kObjectRegistersPerBank)
      {
        discard = 0;
        return discard;
      }
      return objectRegisters[bank * kObjectRegistersPerBank + index];
    }

    // DAT_003437b8: byte counters opcode 0xBC increments, capped at 99. Used by
    // event scripts as one-shot gates.
    static constexpr std::size_t kEventCounterCount = 256;
    std::uint8_t DAT_003437b8_eventCounters[kEventCounterCount]{};

    // uGpffffb704 / uGpffffb708 (0xB9, 0xBA) and fGpffffb70c / fGpffffb710
    // (0xBB). Global colours and the fade radius pair; recorded, not yet used.
    std::uint32_t uGpffffb704_color1 = 0;
    std::uint32_t uGpffffb708_color2 = 0;
    // DAT_0032538c / fGpffffb6b8 / fGpffffb6bc, set by 0xB8.
    float DAT_0032538c_cameraDistance = 0.0f;
    float fGpffffb70c_fadeNear = 0.0f;
    float fGpffffb710_fadeFar = 0.0f;

    // Resource ids the script asked about through opcodes 0x3D..0x40. The port
    // has no resource manager, so these are recorded and answered "not loaded".
    std::vector<std::uint32_t> resourceQueries;

    // DAT_00355cf4: the object-script slot table, 65 dwords cleared by
    // FUN_0025b390. The original stores absolute pointers; the port stores blob
    // offsets, with 0 meaning empty -- the header occupies offsets 0..0x2B, so
    // no entry point can legitimately be at 0.
    //
    // Partitioned as: 0x00..0x3D run by FUN_0025b778, 0x3E..0x3F run later in
    // the frame by FUN_0025b918, and 0x40 the lead-bound slot opcode 0xA8
    // installs. See analyzed/scene_script_frame_entry.c.
    static constexpr std::size_t kObjectScriptSlots = 65;
    static constexpr std::size_t kGeneralSlotCount = 0x3E;
    static constexpr std::size_t kFirstLateSlot = 0x3E;
    static constexpr std::size_t kLeadSlot = 0x40;
    std::uint32_t DAT_00355cf4_objectScriptSlots[kObjectScriptSlots]{};

    // DAT_00355cf8 / iGpffffbd88: the slot currently executing, or -1 outside
    // the slot loop. Opcode 0x9E reads it to retire the slot it is running in.
    std::int32_t DAT_00355cf8_currentSlot = -1;

    // DAT_00354d2c: the battle-start state opcode 0xE1 raises to 0x10. The port
    // has no battle system; this is recorded so the report can say the scene
    // asked for one.
    std::uint32_t DAT_00354d2c_battleState = 0;

    // The fullscreen fade, DAT_00571dc0 (buffer 0) and DAT_00571dd0 (buffer 1).
    // FUN_0025d1c0 arms one and FUN_0025d238 steps it; opcodes 0x85/0x87 and
    // 0x86 are the script's handles on them, and the chest-opening player state
    // 0xC drives the same pair. Both banks have the same shape:
    //
    //   +0x0  level, 0..0x1FE0
    //   +0x2  rate per tick   <- FUN_0025d1c0's second argument
    //   +0x4  packed colour   <- its third
    //   +0x8  "finished" flag, the value FUN_0025d238 returns
    //   +0xA  hold time after the level tops out, seeded to 0xA0
    //
    // Buffer 0 arms at 0x1FE0 (already full) and buffer 1 at 0 (fades up).
    struct FullscreenFade
    {
      std::uint16_t level = 0;
      std::uint16_t rate = 0;
      std::uint32_t colour = 0;
      std::uint16_t finished = 0;
      std::int16_t hold = 0;
    };
    static constexpr std::size_t kFadeBankCount = 2;
    FullscreenFade DAT_00571dc0_fades[kFadeBankCount]{};

    // FUN_0025d1c0. The GS submit it ends with (FUN_0025d0e0) has no port.
    void FUN_0025d1c0_arm_fade(std::uint32_t bank, std::uint16_t rate, std::uint32_t colour)
    {
      FullscreenFade &fade = DAT_00571dc0_fades[bank == 0 ? 0 : 1];
      fade.level = bank == 0 ? 0x1FE0 : 0;
      fade.hold = 0xA0;
      fade.rate = rate;
      fade.colour = colour;
      fade.finished = 0;
    }

    // FUN_0025d238: step buffer 1 and report whether it has finished holding.
    // Returns the flag the original returns, so a caller can wait on it.
    std::uint16_t FUN_0025d238_step_fade(std::uint32_t frameTicks)
    {
      FullscreenFade &fade = DAT_00571dc0_fades[1];
      fade.finished = 0;
      if (static_cast<std::int16_t>(fade.level) < 0x1FE0)
      {
        const std::int32_t stepped =
            static_cast<std::int32_t>(fade.level) + static_cast<std::int32_t>(fade.rate) * static_cast<std::int32_t>(frameTicks);
        fade.level = static_cast<std::uint16_t>(stepped);
        if (static_cast<std::int16_t>(fade.level) > 0x1FE0)
        {
          fade.level = 0x1FE0;
        }
      }
      else if (fade.hold < 1)
      {
        fade.finished = 1;
      }
      else
      {
        fade.hold = static_cast<std::int16_t>(fade.hold - static_cast<std::int16_t>(frameTicks));
      }
      return fade.finished;
    }

    // FUN_002663a0: clear one bit of the flag bank.
    void FUN_002663a0_clearEventFlag(std::uint32_t flagId)
    {
      const std::size_t bucket = static_cast<std::size_t>(static_cast<std::int32_t>(flagId) >> 3);
      if (bucket > 0x8FF || bucket >= kFlagBucketCount)
      {
        return;
      }
      DAT_00342b70_flags[bucket] &= static_cast<std::uint8_t>(~(1u << (flagId & 7u)));
    }

    // FUN_002663f0: set one bit of the flag bank. Opening a chest is exactly
    // this -- FUN_002d1ea8 only observes the bit.
    void FUN_002663a0_setEventFlag(std::uint32_t flagId)
    {
      const std::size_t bucket = static_cast<std::size_t>(static_cast<std::int32_t>(flagId) >> 3);
      if (bucket > 0x8FF || bucket >= kFlagBucketCount)
      {
        return;
      }
      DAT_00342b70_flags[bucket] |= static_cast<std::uint8_t>(1u << (flagId & 7u));
    }

    // FUN_00266368: one bit of the flag bank, which the actor tick also reads.
    bool FUN_00266368_eventFlag(std::uint32_t flagId) const
    {
      const std::size_t bucket = static_cast<std::size_t>(static_cast<std::int32_t>(flagId) >> 3);
      if (bucket > 0x8FF || bucket >= kFlagBucketCount)
      {
        return false;
      }
      return (DAT_00342b70_flags[bucket] & (1u << (flagId & 7u))) != 0;
    }
  };

  // Everything a handler is allowed to touch. Terrain and lead movement arrive
  // as callbacks so this stays free of harness and runtime dependencies, the
  // same way the player controller takes its terrain sampler.
  struct ScriptEnvironment
  {
    orphen::ported::entity::EntityPool *entityPool = nullptr;
    const orphen::ported::entity::EntityDescriptorTable *descriptors = nullptr;
    SceneScriptState *state = nullptr;

    // The loaded map. Opcode 0x51 spawns from its object placement table, which
    // is where scene objects actually stand.
    const orphen::ported::psm2::Psm2RuntimeState *map = nullptr;

    // FUN_00227070: ground height under a world (x, y), or nullopt off-mesh.
    std::function<std::optional<float>(float x, float y)> terrainHeight;

    // FUN_002582d0: teleport the lead player and camera.
    std::function<void(float x, float y, float z)> teleportLead;

    // DAT_003555bc, the per-frame tick count. The fade steps by it.
    std::uint32_t frameTicks = orphen::ported::kNominalFrameTicks;
  };

  class SceneCommandInterpreter
  {
  public:
    SceneCommandInterpreter(std::span<const std::uint8_t> scriptBlob,
                            ScriptEnvironment environment,
                            ScriptTrace &trace)
        : blob_(scriptBlob), environment_(std::move(environment)), trace_(trace)
    {
    }

    // FUN_0025bc68. Runs from an offset into the blob until the outermost block
    // ends. Returns false when the entry offset is outside the blob.
    bool FUN_0025bc68_run(std::uint32_t entryOffset);

    // FUN_0025b778 points both entity selection globals at pool slot 0 before
    // running the lead-bound slot, so that slot's opcodes act on the player.
    void selectEntity(std::size_t slot) { currentEntity_ = slot; }

    // True when execution stopped because the stream ran off the end of the blob
    // rather than at a block end. Always a decode desync.
    bool overran() const { return overran_; }

    // Set when execution stopped on an opcode with no implementation. The stream
    // is halted rather than skipped, because an unimplemented opcode whose
    // operands were not consumed desyncs everything after it -- one honest stop
    // beats a cascade of invented instructions.
    bool haltedOnUnimplemented() const { return haltedOnUnimplemented_; }
    std::uint16_t haltOpcode() const { return haltOpcode_; }
    std::uint32_t haltOffset() const { return haltOffset_; }

  private:
    static constexpr std::size_t kCallStackDepth = 0x10;

    std::span<const std::uint8_t> blob_;
    ScriptEnvironment environment_;
    ScriptTrace &trace_;

    // DAT_00355cd0 / pbGpffffbd60. Held as an offset rather than a pointer so
    // every read is bounds-checked against the blob.
    std::uint32_t streamOffset_ = 0;
    bool halted_ = false;
    bool overran_ = false;
    bool haltedOnUnimplemented_ = false;
    std::uint16_t haltOpcode_ = 0;
    std::uint32_t haltOffset_ = 0;

    std::uint16_t currentOpcode_ = 0; // DAT_00355cd8 / uGpffffbd68
    std::uint16_t literalToken_ = 0;  // uGpffffbd64

    // puGpffffb0d4: the entity most recently spawned or selected. kNoEntity when
    // nothing is selected.
    static constexpr std::size_t kNoEntity = orphen::ported::entity::kEntitySlotCount;
    std::size_t currentEntity_ = kNoEntity;

    // Shared by 0x4F and 0x51: place a spawned entity from a map placement
    // record and record it in the trace.
    void placeFromRecord(std::size_t slot,
                         std::size_t recordIndex,
                         const orphen::ported::psm2::ObjectPlacementRecord &record,
                         SpawnRecord &spawnRecord);

    // Stream access. Every read checks the blob bounds and halts on overrun,
    // because a desynced stream must stop rather than wander.
    bool canRead(std::size_t byteCount) const;
    std::uint8_t peekU8() const;
    std::uint8_t readU8();
    std::uint32_t FUN_0025c1d0_readStreamU32(); // unaligned 32-bit read, advances 4
    void FUN_0025c220_relativeJump();

    // FUN_0025c258 / FUN_0025bf70.
    std::uint32_t FUN_0025c258_evaluate();
    bool FUN_0025bf70_decodeLiteral(std::uint32_t &value);

    // The three dispatch tables, kept as three functions so the shape of
    // FUN_0025bc68 survives. Each returns the opcode's value for the expression
    // evaluator; statements ignore it.
    void dispatchLow(std::uint8_t opcode);                  // PTR_LAB_0031e1f8, 0x00..0x0A
    std::uint32_t dispatchStandard(std::uint8_t opcode);    // PTR_LAB_0031e228, 0x32..0xF5
    std::uint32_t dispatchExtended(std::uint8_t extension); // PTR_LAB_0031e538, 0x100..0x14A

    // Handlers, named for the original they came from. See
    // scene_script_opcodes.cpp.
    void FUN_0025bdd0_conditional_jump();     // 0x01
    void FUN_0025be10_switch_dispatch();      // 0x02
    void FUN_0025e628_process_resource_ids(); // 0x4D
    void FUN_0025e730_push_lookup_entry();    // 0x4E
    void FUN_0025e7c0_process_placements();  // 0x4F
    void FUN_002618c0_set_global_rgb();      // 0x96
    void FUN_00261910_set_vector_with_rgb(); // 0x97
    std::uint32_t FUN_0025d768_read_work_or_flag(); // 0x36, 0x38
    std::uint32_t FUN_0025d818_write_work_or_flag(); // 0x37, 0x39
    std::uint32_t FUN_0025e560_resource_flag();      // 0x3D..0x40
    std::uint32_t FUN_00260318_read_object_register();    // 0x76
    std::uint32_t FUN_00260360_modify_object_register(); // 0x77..0x7C
    std::uint32_t FUN_00263e30_increment_event_counter(); // 0xBC
    void FUN_00263d10_set_global_color();                // 0xB9, 0xBA
    void FUN_00263db0_set_fade_radius_pair();            // 0xBB
    void FUN_00263cb8_set_camera_distance();             // 0xB8
    std::uint32_t FUN_0025f0d8_select_slot_by_index();   // 0x58
    std::uint32_t FUN_0025f150_select_by_record_index(); // 0x5A
    std::uint32_t FUN_0025eaf0_init_selected();  // 0x50
    std::uint32_t FUN_0025eb48_set_pw_all();     // 0x51
    std::uint32_t FUN_0025edc8_spawn_by_type();  // 0x52
    void FUN_0025eeb0_set_entity_position();     // 0x54, 0x55
    void FUN_00263148_teleport_lead();           // 0xAB
    std::uint32_t FUN_0025f120_get_slot_index(); // 0x59
    std::uint32_t FUN_0025f4b8_test_lead_flag_word(); // 0x61

    // The two outcomes s01_e024's floor panels reach. Both have unambiguous
    // operand widths -- 0x6D takes one signed byte, 0xE1 takes none -- so
    // neither is a guess about how much stream to consume.
    std::uint32_t FUN_0025fd10_set_player_lock();   // 0x6D
    std::uint32_t FUN_00260038_angle_to_lead();     // 0x70
    std::uint32_t FUN_00260c20_dispatch_rgb_event(); // 0x85, 0x87
    std::uint32_t FUN_00260ca0_advance_fade();       // 0x86
    std::uint32_t FUN_00265000_boot_party_for_battle(); // 0xE1

    // The object-script slot table. See analyzed/scene_script_frame_entry.c.
    std::uint32_t FUN_00261cb8_install_slot();   // 0x9D
    std::uint32_t FUN_00261d18_clear_slot();     // 0x9E
    std::uint32_t FUN_00261d88_slot_occupied();  // 0x9F
    std::uint32_t FUN_00261de0_find_free_slot(); // 0xA0
    std::uint32_t FUN_00262f38_install_lead_slot(); // 0xA8
    std::uint32_t FUN_00263118_clear_lead_slot();   // 0xAA

    orphen::ported::entity::OriginalEntity *resolveEntity(std::uint32_t index);
    void alignStreamTo4();
    std::uint32_t haltUnimplemented(std::uint16_t opcode);
  };

} // namespace orphen::ported::script

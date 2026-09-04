#pragma once

#include "ported/entity/entity_descriptor_table.h"
#include "ported/entity/entity_pool.h"
#include "ported/entity/original_entity_sound.h"
#include "ported/original_frame_timing.h"
#include "ported/psm2/psm2_runtime.h"
#include "ported/render/original_fade_track.h"
#include "ported/render/original_light_table.h"
#include "ported/render/original_frame_feedback.h"
#include "ported/render/original_letterbox.h"
#include "ported/render/original_screen_fade.h"
#include "ported/resource/character_stats.h"
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

    // DAT_0031e770: which of those 128 work words the SCEN WORK DISP submenu
    // has switched on, as four 32-bit words. FUN_0026a508 XORs a bit per menu
    // entry; nothing in the game itself writes it, so it stays zero and
    // FUN_0025b778's second loop prints nothing until the menu turns a slot on.
    static constexpr std::size_t kSceneWorkDisplayWords = 4;
    std::uint32_t DAT_0031e770_sceneWorkDisplayMask[kSceneWorkDisplayWords]{};

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

    // DAT_00572078: the sixteen colour-ramp tracks opcodes 0x9A/0x9B/0x9C drive.
    // A global in the original, but nothing outside the script reads it and a
    // scene load has to start clean, so it lives with the rest of the script's
    // state.
    orphen::ported::render::FadeTrackTable DAT_00572078_fadeTracks;

    // DAT_00343888: the sixteen dynamic light slots, opcodes 0xBF..0xC7.
    orphen::ported::render::LightTable DAT_00343888_lights;

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

    // DAT_00355656, written by extended opcode 0x149 and zeroed by
    // FUN_0022a418 at scene load. Nothing in the retail executable reads it
    // back, so it is scene state with no consumer -- stored rather than skipped
    // because it costs nothing and a later slice may find the reader.
    std::uint8_t DAT_00355656_sceneByte = 0;

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

    // FUN_00261de0, shared by opcode 0xA0 and the event scheduler: the first
    // free general slot, or -1 when all 62 are taken.
    std::int32_t findFreeObjectScriptSlot() const
    {
      for (std::size_t slot = 0; slot < kGeneralSlotCount; ++slot)
      {
        if (DAT_00355cf4_objectScriptSlots[slot] == 0)
        {
          return static_cast<std::int32_t>(slot);
        }
      }
      return -1;
    }

    // ---- FUN_0025ce30, the timed event scheduler -------------------------
    //
    // This is how a cutscene keeps time. The VM has no yield, so a scene cannot
    // write "walk here, wait two seconds, then speak" as a linear routine.
    // Instead opcode 0xA1 arms a channel with a *stream* of 8-byte records and
    // this scheduler pays them out, one per elapsed delay, either straight into
    // the dialogue driver or into a free object-script slot.
    //
    // Three parallel arrays in the original (DAT_00571e40 / e44 / e48, 12-byte
    // stride); one struct here because they are indexed together everywhere.
    struct EventChannel
    {
      // The original holds an absolute pointer. The port holds a blob offset
      // with 0 meaning idle, the same convention the object-script slot table
      // uses -- the header occupies offsets 0..0x2B, so no record can sit at 0.
      std::uint32_t cursor = 0;
      std::uint32_t timer = 0;    // accumulates frameTicks; compared as timer >> 5
      std::uint32_t consumed = 0; // records paid out, returned by opcode 0xA3
    };
    static constexpr std::size_t kEventChannelCount = 4;
    EventChannel DAT_00571e40_eventChannels[kEventChannelCount]{};

    // Each record is [u16 delayUnits][u16 gate][u32 targetOffset].
    static constexpr std::size_t kEventRecordSize = 8;

    // uGpffffbd70 / uGpffffbd74, which are DAT_00355ce0 / DAT_00355ce4 -- the
    // same addresses under two Ghidra names. FUN_0025b288 walks header word 5's
    // pointer table and stores its first and last non-zero entries. A scheduler
    // target inside that half-open window is a *dialogue record* and is started
    // directly; anything else is script and gets queued into a slot.
    std::uint32_t DAT_00355ce0_dialogueWindowFirst = 0;
    std::uint32_t DAT_00355ce4_dialogueWindowLast = 0;

    // DAT_00343692: the seven party slots, 0x28 bytes apart in the original.
    // Each holds the pool index of the entity bound to that slot; 0x100 means
    // "released this scene" and is distinct from 0, which means never filled.
    // Opcode 0xAC binds, 0xAD/0xAE release, 0xAF selects.
    //
    // The parallel event flags 0x501 + slot are the persistent half: 0xAC
    // refuses to bind a slot whose flag is already set, and caps the party at
    // three.
    static constexpr std::size_t kPartySlotCount = 7;
    std::uint16_t DAT_00343692_partySlots[kPartySlotCount]{};

    // The rest of the same 0x28-byte records at DAT_00343688, which
    // FUN_002294d0 fills once per new game from group 1 of the SCR.BIN stat
    // blob, keyed by DAT_0031c1f0 = {1, 3, 4, 5, 6, 7, 0x16} -- the party
    // character type ids, in slot order. Kept beside the slot halfword rather
    // than folded into one struct because every existing reader addresses
    // DAT_00343692 by itself.
    //
    // Opcode 0xAC hands a record to FUN_0023a518, which is where a follower
    // gets its collision radius and height. Empty records leave a follower with
    // a zero-radius body, so `partyRecordsLoaded` gates the call.
    static constexpr std::int16_t DAT_0031c1f0_partyCharacterIds[kPartySlotCount] = {1, 3, 4, 5, 6, 7, 0x16};
    orphen::ported::resource::StatRecord DAT_00343688_partyRecords[kPartySlotCount]{};
    bool partyRecordsLoaded = false;

    // FUN_002294d0's record loop, without the flag wipe around it: that half is
    // a new-game reset and the port's scenes start mid-game.
    //
    //   FUN_0025bae8(1, FUN_00229888(i), record); record[0x0A] = 0x100;
    //   record[0x02] = (short)(char)record[0x06];
    //
    // The 0x100 goes to DAT_00343692, which the port keeps in its own array and
    // which opcode 0xAC's own paths already write, so it is not repeated here.
    void FUN_002294d0_load_party_records(const orphen::ported::resource::CharacterStats &stats)
    {
      if (!stats.loaded())
      {
        return;
      }
      bool anyLoaded = false;
      for (std::size_t slot = 0; slot < kPartySlotCount; ++slot)
      {
        const auto record = stats.FUN_0025bae8_record(1, DAT_0031c1f0_partyCharacterIds[slot]);
        if (!record.has_value())
        {
          continue;
        }
        DAT_00343688_partyRecords[slot] = *record;
        DAT_00343688_partyRecords[slot].halfword02 =
            static_cast<std::int16_t>(static_cast<std::int8_t>(record->byte06));
        anyLoaded = true;
      }
      partyRecordsLoaded = anyLoaded;
    }

    // FUN_00251dc0: stamp one party record onto the lead player's four combat
    // halfwords. FUN_0022a418:206 calls it at scene init with DAT_0058beb0
    // outright, so it is always slot 0 that gets them.
    //
    //   +0x128 max hit points   record +0x02 (which FUN_002294d0 has already
    //                           copied from +0x06, so the party starts full)
    //   +0x12A hit points       record +0x06
    //   +0x12C attack power     record +0x07
    //   +0x12E defence          record +0x08
    //
    // Orphen's record gives 50/50/1/0, which is exactly what eeMemory.bin holds
    // at slot 0. Attack power is the number FUN_00216140 scales by the attack's
    // element resistance and its +30% sword bonus, so without this every hit
    // fell through to the "at least one point" floor by accident.
    void FUN_00251dc0_load_player_stats(orphen::ported::entity::OriginalEntity &player) const
    {
      if (!partyRecordsLoaded)
      {
        return;
      }
      // FUN_0022a418:190-197 reads the party leader out of DAT_0058beb0 and
      // *forces it to 1 when it is zero*; :206 then calls FUN_00251dc0 with
      // that same slot. The port clears the pool between those two lines
      // (resetLeadPlayerForLoadedMap), so by the time this runs the lead's type
      // is back at zero, FUN_002298d0 answers 7, and the guard below threw the
      // stats away -- every scene the port has ever loaded gave the lead 0 HP,
      // 0 attack and 0 defence.
      //
      // Nothing noticed until the battle module: FUN_00249610:84 stops the
      // whole character update when +0x12A minus +0xBE drops below 1, so a lead
      // with no hit points reads as a dead one and never dispatches a state.
      constexpr std::int16_t kDAT_0058beb0_defaultLeader = 1;
      const std::int16_t leaderType =
          player.effectiveTypeId() != 0 ? player.effectiveTypeId() : kDAT_0058beb0_defaultLeader;
      const int characterClass =
          orphen::ported::entity::FUN_002298d0_character_class(leaderType);
      if (characterClass >= static_cast<int>(kPartySlotCount))
      {
        return;
      }
      const auto &record = DAT_00343688_partyRecords[static_cast<std::size_t>(characterClass)];
      player.maxHitPoints128 = static_cast<std::uint16_t>(record.halfword02);
      player.staggerTimer12a =
          static_cast<std::uint16_t>(static_cast<std::int8_t>(record.byte06));
      player.attackPower12c =
          static_cast<std::uint16_t>(static_cast<std::int8_t>(record.byte07));
      player.defence12e = static_cast<std::uint16_t>(static_cast<std::int8_t>(record.byte08));
    }

    // DAT_00571de0: parameter ramps, three floats each -- current, target,
    // step. Opcode 0x90 arms one and 0x91 advances it, **returning 1 only once
    // it has arrived**, which is how a cutscene waits for a fade or a move to
    // finish. The dispatch table files call these audio; nothing about them is.
    struct ParameterRamp
    {
      float current = 0.0f;
      float target = 0.0f;
      float step = 0.0f;
    };
    static constexpr std::size_t kParameterRampCount = 64;
    ParameterRamp DAT_00571de0_ramps[kParameterRampCount]{};

    // puGpffffb0d8: the entity the choreography opcodes 0xE9..0xF5 act on.
    // Separate from the general selection (puGpffffb0d4) that 0x76..0x7C use,
    // so a cutscene can hold one actor "in focus" across a whole sequence of
    // moves while other opcodes address other entities.
    std::size_t puGpffffb0d8_focusEntity = orphen::ported::entity::kEntitySlotCount;

    // iGpffffbd78: the scripted camera path's elapsed time, in frame ticks.
    // Opcodes 0x41 and 0x43 zero it when they build a path and 0x42/0x44
    // accumulate into it. One global, not one per path -- a scene can only have
    // one camera move in flight.
    std::uint32_t uGpffffbd78_pathElapsed = 0;

    // uGpffffb0f4: the scheduler's second gate. FUN_00237b38 clears bits 0x6000
    // while a dialogue stream is running and sets them when it ends, so a
    // record gated on those bits waits for the text to finish.
    std::uint32_t uGpffffb0f4_gateMask = 0;

    // DAT_00354d2c: the battle-start state opcode 0xE1 raises to 0x10. The port
    // has no battle system; this is recorded so the report can say the scene
    // asked for one.
    std::uint32_t DAT_00354d2c_battleState = 0;

    // The fullscreen fade used to be modelled a second time right here, as a
    // pair of banks with their own arm/step methods. It stepped correctly and
    // was never drawn: PortRuntime owns the `ScreenFade` the renderer reads, and
    // that one only ever saw the chest cutscene. Two copies of DAT_00571DC0
    // meant every fade a *script* asked for was invisible. There is now one
    // object, reached through ScriptEnvironment::DAT_00571dc0_screenFade.

    // FUN_002663d8: clear one bit of the flag bank. (This carried FUN_002663a0's
    // name, which is the *setter* below -- 0x2663a0 ORs the bit in and 0x2663d8
    // masks it out. Corrected because the FUN_ prefix is the traceable identity
    // and party release, which clears flag 0x501 + slot, goes through here.)
    void FUN_002663d8_clearEventFlag(std::uint32_t flagId)
    {
      const std::size_t bucket = static_cast<std::size_t>(static_cast<std::int32_t>(flagId) >> 3);
      if (bucket > 0x8FF || bucket >= kFlagBucketCount)
      {
        return;
      }
      DAT_00342b70_flags[bucket] &= static_cast<std::uint8_t>(~(1u << (flagId & 7u)));
    }

    // FUN_002663a0: set one bit of the flag bank. Opening a chest is exactly
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

    // FUN_00266418: flip one bit and report its new value.
    bool FUN_00266418_toggleEventFlag(std::uint32_t flagId)
    {
      const std::size_t bucket = static_cast<std::size_t>(static_cast<std::int32_t>(flagId) >> 3);
      if (bucket > 0x8FF || bucket >= kFlagBucketCount)
      {
        return false;
      }
      const std::uint8_t mask = static_cast<std::uint8_t>(1u << (flagId & 7u));
      DAT_00342b70_flags[bucket] ^= mask;
      return (DAT_00342b70_flags[bucket] & mask) != 0;
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
    // Opcode 0xBD's entity methods 0x70 / 0x72, the waypoint path-follow pair
    // (FUN_002443f8 / FUN_002445c8). The interpreter decodes the path out of the
    // blob itself -- the points are VM expressions and only it can evaluate them
    // -- and hands the finished world-space list over. Start returns 1 or -1;
    // progress returns non-zero while the actor is still walking.
    std::function<int(std::size_t entitySlot,
                      std::span<const orphen::ported::psm2::Vec3> waypoints,
                      std::uint32_t duration)>
        FUN_002443f8_start_path;
    std::function<int(std::size_t entitySlot)> FUN_002445c8_path_progress;

    // Opcode 0xBD's *battle* methods, the low numbers of the same table. This
    // is how a scene enters battle mode -- see ported/battle/battle_party.h for
    // why docs/battle_mode_activation.md says otherwise and is wrong.
    //
    //   1    FUN_00242de0  stop the battle
    //   2    FUN_00243f80  start it, building the party first if method 3 has
    //                      not already run
    //   3    FUN_002432d8  build the party from the roster
    //   0x78 FUN_00244cc0  equip `spellId` into loadout slot `packed & 0xF` of
    //                      row `(packed >> 8) & 0xF`
    //
    // s14_e012 calls all four. The battle module lives outside ported/script/,
    // so like every other cross-module effect it arrives as a callback.
    std::function<std::uint32_t(std::int32_t method, std::int32_t arg3, std::int32_t arg4)>
        FUN_00242a18_battle_method;

    orphen::ported::entity::EntityPool *entityPool = nullptr;
    const orphen::ported::entity::EntityDescriptorTable *descriptors = nullptr;
    SceneScriptState *state = nullptr;

    // uGpffffadf8. FUN_0025eb48 stamps a stat record on every placement it
    // spawns from a group other than 3 -- collision radius, body height and hit
    // points -- which is the only thing that gives an enemy a body.
    const orphen::ported::resource::CharacterStats *uGpffffadf8_stats = nullptr;

    // FUN_0023f8b8, reached from the enemy's own state 0. See
    // ported/battle/battle_encounter.h: this is what binds a spawned enemy into
    // the battle actor table, and therefore what makes it targetable.
    std::function<void(std::size_t entitySlot)> FUN_0023f8b8_bind_battle_actor;

    // DAT_00571DC0/DAT_00571DD0, the one fullscreen fade. Opcodes 0x85/0x87 arm
    // it and 0x86/0x88 step it, and the chest cutscene's player states drive the
    // same object -- in the original they are literally the same two banks of
    // BSS, so the port has to share one too or a script fade and a cutscene fade
    // fight over a screen only one of them is drawn on.
    orphen::ported::render::ScreenFade *DAT_00571dc0_screenFade = nullptr;

    // DAT_00355661 / DAT_00354B88 / DAT_00343878.., the screen smear. Opcodes
    // 0xC8 and 0xC9 write it and FUN_002000c0 draws it; the renderer reads the
    // same object rather than keeping a copy, for the reason the fade above
    // has to.
    orphen::ported::render::FrameFeedback *DAT_00343878_frameFeedback = nullptr;

    // DAT_00355054 / DAT_00355CFC, the cinematic bars. Opcode 0x6D arms them
    // and FUN_0025b778 steps them at the end of every tick, so like the fade
    // this is one object the renderer also reads rather than a second copy.
    orphen::ported::render::Letterbox *DAT_00355054_letterbox = nullptr;

    // The loaded map. Opcode 0x51 spawns from its object placement table, which
    // is where scene objects actually stand.
    const orphen::ported::psm2::Psm2RuntimeState *map = nullptr;

    // FUN_00227070 / FUN_00227798: ground height under a world (x, y), or
    // nullopt off-mesh.
    //
    // `feetHeight` and `headHeight` are the vertical band the query answers
    // within, and they are not optional -- a ship deck stacks floors, so
    // "the ground at (x, y)" has no answer without them. FUN_00227070 stages
    // the entity's +0x28 and +0x28 + +0x58 into the scan workspace at +0x0B
    // and +0x0C; FUN_00227798 puts its z argument into both. FUN_00227840 then
    // refuses to settle on anything above the head.
    std::function<std::optional<float>(float x, float y, float feetHeight, float headHeight)>
        terrainHeight;

    // FUN_00227070 proper, for the callers that really invoke it -- opcode 0x55
    // is one. Unlike `terrainHeight` above (which is FUN_00227798's body-less
    // point query) this samples four corners of the entity's collision radius
    // and keeps the highest, unless entity +0x04 bit 1 says otherwise.
    std::function<std::optional<float>(float x,
                                       float y,
                                       float feetHeight,
                                       float bodyHeight,
                                       float radius,
                                       std::uint16_t entityFlags04,
                                       std::uint32_t rejectTerrainMask)>
        FUN_00227070_sample_ground;

    // FUN_002589c0:14-17. Leaving the party drops the two bone overrides the
    // follower's look-at was holding -- role 2 (the bust) and role 1 (the
    // head). They live in DAT_004a7e00, which the script layer has no view of,
    // so the release arrives as a callback the same way everything else does.
    std::function<void(std::size_t slot)> FUN_0020d9c8_release_look_bones;

    // FUN_0022dcf0: arm the camera shake. `magnitude` has already been divided
    // by DAT_00352c34 and `durationTicks` is the raw halfword. The state lives
    // in the render layer's CameraShake, which FUN_0020bec8 spends.
    std::function<void(float magnitude, std::int16_t durationTicks)> FUN_0022dcf0_shake_camera;

    // FUN_002582d0: teleport the lead player and camera.
    std::function<void(float x, float y, float z)> teleportLead;

    // FUN_002610a8, opcode 0x8E: leave for another scene. Publishing the request
    // is all the opcode does -- DAT_003551ec, DAT_003551f8 and the departure
    // position all live outside the script, and FUN_002239c8 spends them at the
    // top of the next frame.
    std::function<void(std::int32_t destination)> FUN_002610a8_request_scene_change;

    // FUN_0025daf8, opcode 0x3C: `DAT_00355208 = expr`, the map-prop bank. The
    // banks live on the runtime's model store and descriptor table, so the write
    // has to reach both.
    std::function<void(std::int32_t bank)> FUN_0025daf8_set_map_prop_bank;

    // DAT_003555d3, set from bit 0x20000 of the scene-change request: this scene
    // came out of the group-0xE list. FUN_0025b6d0 branches on it before it runs
    // anything, so the script layer needs to see it.
    bool DAT_003555d3_groupEScene = false;

    // FUN_00217e18: drop an installed script camera, restoring the saved pose
    // when the argument is non-zero. Opcode 0x45's whole effect.
    std::function<void(bool restore)> FUN_00217e18_release_camera;

    // FUN_00217d70: pin the camera to one explicit eye and look-at, with no
    // curve behind it. Opcode 0x46's whole effect. The install-once guard
    // (cGpffffad2f) lives inside the camera, so a second call while a script
    // camera is up is dropped there rather than here.
    std::function<void(const orphen::ported::psm2::Vec3 &eye,
                       const orphen::ported::psm2::Vec3 &lookAt)>
        FUN_00217d70_set_manual_camera;

    // FUN_00218158: sample the installed camera path at elapsed/duration and
    // publish the eye, look-at, roll and zoom. Opcode 0x44 steps it.
    std::function<void(int elapsedFrames, int durationFrames)> FUN_00218158_step_camera_path;

    // FUN_00217f38: the same sample without the roll/zoom publish. Opcode 0x42
    // steps this one; FUN_0025dd60 picks between the two on its own opcode.
    std::function<void(int elapsedFrames, int durationFrames)> FUN_00217f38_step_camera_path;

    // FUN_00261fd8, opcode 0xA7: retag the map's 0x78 primitive records. The
    // map has to be mutable, which ScriptEnvironment::map is not, so this goes
    // through a callback the same way the collision-group move does.
    std::function<void(std::uint32_t mask, std::uint32_t topNibble)> FUN_00261fd8_retag_primitives;

    // FUN_00217fe8: install a scripted camera path. `zoomScales` are the raw
    // values from the script, before FUN_00218230's log.
    std::function<void(std::span<const orphen::ported::psm2::Vec3> eyePoints,
                       std::span<const float> rollValues,
                       std::span<const float> zoomScales,
                       std::span<const orphen::ported::psm2::Vec3> lookAtPoints)>
        FUN_00217fe8_set_camera_path;

    // FUN_00218230 + uGpffffb6e8: set the camera's log2 zoom outright.
    std::function<void(float zoomLog2)> FUN_00218230_set_zoom;

    // uGpffffb6dc / DAT_0035564c, the third rotation FUN_0020bec8:49 puts into
    // the view matrix. Assigned outright by opcode 0x4C and by FUN_002676d8.
    std::function<void(float radians)> set_uGpffffb6dc_roll;

    // The camera's current eye and look-at, and cGpffffb6e1. Opcode 0x41 can
    // splice the live pose into its curve as the first or last control point,
    // which is how a cut starts from wherever the camera already is.
    struct CameraPose
    {
      orphen::ported::psm2::Vec3 eye{};
      orphen::ported::psm2::Vec3 lookAt{};
      std::uint8_t subMode = 0;
    };
    std::function<CameraPose()> cameraPose;

    // FUN_00216868: the engine RNG, read by opcode 0x95.
    std::function<std::uint32_t()> FUN_00216868_random;

    // FUN_00267d38: play a sound cue positioned on a pool entity. Extended
    // opcodes 0x125 / 0x126.
    std::function<void(std::uint16_t cue, std::size_t slot)> FUN_00267d38_play_at_entity;

    // == FUN_0025b778's own debug output ==
    //
    // DAT_003555dd, the debug display byte the menu writes. Bit 7 is the
    // "SCR SUBPROC DISP" entry (`bGpffffb66d & 0x80` in the menu handler, and
    // gp 0xffffb66d resolves to 0x003555dd against the 0x00359F70 base).
    std::uint8_t DAT_003555dd_debugDisplay = 0;
    static constexpr std::uint8_t kSubprocDisplayBit = 0x80;

    // The two FUN_002681c0 call sites in FUN_0025b778, one callback each so the
    // format strings stay where the original keeps them.
    //   0x0034CA60  "Subproc:%3d [%5d]\n"   slot, the dword at (body - 4)
    std::function<void(int slot, std::int32_t subprocId)> FUN_002681c0_subprocLine;
    //   0x0034CA78  " %02d:%d(%X)\n"        work index, its value twice
    std::function<void(int index, std::uint32_t value)> FUN_002681c0_sceneWorkLine;

    // The music slots. 0x129 starts a slot the scene preloaded, 0x12A ramps one
    // up and 0x12B ramps one down -- see FUN_00205d90 / FUN_002063c8 /
    // FUN_00206260. The fader is 0..1000 over the slot record's volume byte.
    std::function<void(std::size_t slot, int fader)> FUN_00205d90_play_music_slot;
    std::function<void(std::size_t slot, int speed, int fader)> FUN_002063c8_ramp_music_up;
    std::function<void(std::size_t slot, int speed, int fader)> FUN_00206260_ramp_music_down;

    // FUN_00213640: suspend or resume the player's bandana. Extended opcode
    // 0x146's whole effect; it needs the bone-override table, which lives on the
    // runtime rather than in the pool.
    std::function<void(std::int32_t mode)> FUN_00213640_set_bandana;

    // Opcode 0x140/0x141's visual half. FUN_0020dd78 needs the parsed model and
    // FUN_0020dc38 writes entity +0x168, and both of those live on the runtime
    // rather than in the pool, so they come in the same way the bandana does.
    //
    // The pair is what makes a head swap look like a head swap: 0x140 resolves
    // bone role 6 on the character to decide where the replacement attaches, and
    // the hide loop blanks the bones the replacement stands in for. Without the
    // hide the original head is still drawn, inside the new one.
    // FUN_0020dc88: a bone-local point in world space, for opcode 0x64's bake.
    // The port already has the maths in psc3_skeleton; this is the palette
    // lookup, which lives with the runtime because the script has no view of
    // DAT_00357e00. nullopt means the parent slot has no palette this frame,
    // which is FUN_0020dc88's own +0x0C bit 0x2000 fallback.
    std::function<std::optional<orphen::ported::psm2::Vec3>(std::size_t parentSlot,
                                                            int bone,
                                                            orphen::ported::psm2::Vec3 localPoint)>
        FUN_0020dc88_bone_point;

    // FUN_002d2f40, called straight from opcode 0x13F rather than waited for.
    // Returns the pool slots it parked at +0x19C and +0x198 -- the close-up body
    // and the head -- or nullopt when the rig could not be built.
    std::function<std::optional<std::pair<std::int32_t, std::int32_t>>(std::size_t slot)>
        FUN_002d2f40_build_closeup_rig;

    std::function<std::size_t(std::size_t slot, std::uint8_t role)> FUN_0020dd78_bone_for_role;
    std::function<void(std::size_t slot, int firstBone, int count)> FUN_0020dc38_hide_bones;

    // Opcode 0x142 (FUN_002606d0) past its entity selection, which is the exact
    // undo of the pair above: FUN_00265f70 destroys every entity whose +0x192
    // names this slot, FUN_00251e40 rebuilds the bandana when the entity is the
    // lead, and FUN_0020dc48(-1) clears all 42 of its +0x168 bytes. Without it a
    // cutscene's replacement head outlives the cutscene, still wearing the last
    // pose the scene left it in, and the character's own head stays hidden
    // underneath it.
    std::function<void(std::size_t slot)> FUN_002606d0_detach_children;

    // Opcodes 0xA4 and 0xA6 (FUN_00261f60 -> FUN_0022dbc8 / FUN_0022dc68).
    //
    // Both take a group mask and test it against the 0x78 terrain record's
    // +0x04 -- the same word entity +0x74 rejects against -- and both walk the
    // two primitive tables in lockstep, so record 78[i] selects record 80[i].
    // Between them they are how a map opens a door:
    //
    //   0xA4  bit 0x20 of the 0x80 record's +0x70, the draw-time hidden bit
    //   0xA6  bit 0x800 of the 0x78 record's word 0, the bit the ground scan
    //         requires before a primitive is terrain at all
    //
    // The inline byte reads the same way for both even though the polarity of
    // the two bits is opposite: zero turns the group off, non-zero turns it on.
    std::function<void(std::uint32_t groupMask, bool visible)> FUN_0022dbc8_show_map_primitives;
    std::function<void(std::uint32_t groupMask, bool solid)> FUN_0022dc68_enable_map_terrain;

    // Opcodes 0x7D and 0x7E (FUN_00260738): a collision group index, an inline
    // channel byte (0..2) and a value. 0x7D writes a rotation channel, 0x7E a
    // translation channel, and each raises its bit in the group's dirty byte
    // for FUN_00208450 to spend. This is how a door swings -- the map's own
    // geometry moves, so the drawn door and its collision travel together.
    //
    // The map is not const here the way ScriptEnvironment::map is, which is why
    // this is a callback rather than a direct write.
    std::function<void(std::uint32_t group, std::uint8_t channel, float value, bool rotation)>
        FUN_00260738_move_collision_group;

    // FUN_00237b38: start a dialogue stream at a blob offset, or terminate the
    // current one when the offset is zero. Both the scheduler and opcode 0x33
    // reach the text system through this.
    std::function<void(std::uint32_t blobOffset)> FUN_00237b38_start_dialogue;

    // FUN_00237c60 / FUN_00237c70: is a stream up, and has it finished. Opcodes
    // 0x34 and 0x35 poll these every frame while a cutscene waits on a line.
    std::function<bool()> FUN_00237c60_dialogue_busy;
    std::function<bool()> FUN_00237c70_dialogue_complete;

    // FUN_002661a8: resolve a type id to its model record and make sure the
    // model and its texture are resident. Opcode 0x4D's per-id call.
    //
    // The order matters and is checkable: this is what fills the texture slot
    // cache, and s01_e024's preload list reproduces the EE dump's slot table
    // exactly -- 0x55 to the alt bank at 24, then 0x62, 0x64, 0x1, 0x3, 0x4,
    // 0x7, 0x5, 0x6 filling 11 through 17 behind the lead player's slot 10.
    std::function<void(std::uint16_t typeId)> FUN_002661a8_preload_model;

    // DAT_003555bc, the per-frame tick count. The fade steps by it.
    std::uint32_t frameTicks = orphen::ported::kNominalFrameTicks;

    // Diagnostics only. The frame number, and DAT_003555d0 as the actor pass
    // will see it. Opcode 0x55 reports here when a placement lands *inside*
    // geometry -- the situation the embedded-corner push-out exists to undo,
    // and which only gets one frame to be undone in.
    std::uint32_t frameNumber = 0;
    bool DAT_003555d0_collisionGroupMoved = false;

    // DAT_003555b8 (iGpffffb648), the tick *accumulator* -- FUN_002239c8:189
    // adds DAT_003555bc to it once per frame. It is the phase every native
    // wave in the engine is driven from, including FUN_002676d8's, so a
    // dropped frame advances the swell by the time it actually took rather
    // than by one frame.
    std::uint32_t DAT_003555b8_tickCounter = 0;
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
    void FUN_00261b80_arm_fade_track();      // 0x9A
    std::int32_t FUN_00263f28_allocate_light(); // 0xBF / 0xC0
    std::uint32_t FUN_0025d768_read_work_or_flag(); // 0x36, 0x38
    std::uint32_t FUN_0025d818_write_work_or_flag(); // 0x37, 0x39
    std::uint32_t FUN_0025e560_event_flag();          // 0x3D..0x40
    std::uint32_t FUN_00260318_read_object_register();    // 0x76
    std::uint32_t FUN_00260360_modify_object_register(); // 0x77..0x7C
    std::uint32_t FUN_00263e30_increment_event_counter(); // 0xBC
    std::uint32_t FUN_00264448_set_frame_feedback();      // 0xC8 / 0xC9
    std::uint32_t FUN_00263ee0_call_function_table_entry(); // 0xBE
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
    std::uint32_t FUN_0025f548_find_entity_by_tag();  // 0x62
    std::uint32_t FUN_0025dff0_set_manual_camera();   // 0x46
    std::uint32_t FUN_00265290_get_or_set_gate_mask(); // 0xE7 / 0xE8
    std::uint32_t FUN_0025daf8_set_map_prop_bank();   // 0x3C
    std::uint32_t FUN_002601f8_entity_distance_or_angle(); // 0x74, 0x75

    // The choreography family. See the block comment in the .cpp: every one of
    // these advances the focus entity's +0x1BC step counter when it finishes,
    // which is how a cutscene keeps its place in a VM with no yield.
    std::uint32_t FUN_00265840_set_focus();        // 0xEB
    std::uint32_t FUN_00265880_set_step();         // 0xEC
    std::uint32_t FUN_002658b0_get_step();         // 0xED
    std::uint32_t FUN_00265d88_consume_interact(); // 0xE9
    std::uint32_t FUN_00265818_focus_index();      // 0xEA
    std::uint32_t FUN_002658c0_step_toward_xy();   // 0xEE..0xF1
    std::uint32_t FUN_00265b90_anim_for_duration(); // 0xF2
    std::uint32_t FUN_00265c30_anim_until_done();   // 0xF3
    std::uint32_t FUN_00265cb0_rotate_toward();     // 0xF4
    std::uint32_t FUN_00265d98_promote_state();     // 0xF5
    std::uint32_t FUN_0025ee08_read_position();     // 0x53
    std::uint32_t FUN_0025db20_build_camera_path_pair(); // 0x41
    std::uint32_t FUN_0025de08_build_camera_path();   // 0x43
    std::uint32_t FUN_0025dd60_step_camera_path();    // 0x42 / 0x44
    std::uint32_t FUN_0025efa8_set_entity_scale();    // 0x56
    std::uint32_t FUN_002610a8_request_scene_change();// 0x8E
    std::uint32_t FUN_002604a8_publish_rig_children();// 0x13F
    std::uint32_t FUN_0025f700_detach_from_bone();    // 0x64
    std::uint32_t FUN_00260ce0_set_overlay_colour();  // 0x89
    std::uint32_t FUN_00261fd8_retag_map_primitives();// 0xA7
    std::uint32_t FUN_0025dfc8_release_camera();      // 0x45
    void FUN_0025f5d8_attach_and_place_entity();      // 0x63
    void FUN_0025f950_convert_to_npc();               // 0x66
    void FUN_002589c0_release_party_slot(std::size_t slot);
    std::uint32_t FUN_002631f0_bind_party_slot();     // 0xAC
    static void FUN_0023a518_apply_party_record(orphen::ported::entity::OriginalEntity &entity,
                                                const orphen::ported::resource::StatRecord &record);
    std::uint32_t FUN_00263498_release_party_slot();  // 0xAD, 0xAE
    std::uint32_t FUN_002635c0_select_party_member();  // 0xAF
    std::uint32_t FUN_00260578_spawn_attached_prop(); // 0x140, 0x141
    void FUN_00263c58_set_entity_short_and_word();    // 0xB7
    std::uint32_t FUN_00265790_set_global_byte();     // 0x149
    std::uint32_t FUN_00261100_arm_ramp();            // 0x90
    std::uint32_t FUN_002611b8_step_ramp();           // 0x91

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

    // FUN_0025ce30's channels. See SceneScript::FUN_0025ce30_run_event_scheduler.
    std::uint32_t FUN_00261e30_arm_event_channel();   // 0xA1
    std::uint32_t FUN_00261ea8_clear_event_channel(); // 0xA2
    std::uint32_t FUN_00261f08_read_event_channel();  // 0xA3
    std::uint32_t FUN_00262f38_install_lead_slot(); // 0xA8
    std::uint32_t FUN_00263118_clear_lead_slot();   // 0xAA

    orphen::ported::entity::OriginalEntity *resolveEntity(std::uint32_t index);

    // FUN_0025d618: point the stream at `blobOffset`, read a u32 count, then
    // evaluate count*3 expressions as x/y/z, and put the cursor back. Returns
    // false if the offset or the data runs off the end of the blob.
    bool decodePathWaypoints(std::uint32_t blobOffset,
                             std::vector<orphen::ported::psm2::Vec3> &waypoints);
    // puGpffffb0d8, or null when nothing is in focus.
    orphen::ported::entity::OriginalEntity *focusEntity();
    void alignStreamTo4();
    std::uint32_t haltUnimplemented(std::uint16_t opcode);

    // Record the opcode currently being dispatched. Called before the handler
    // touches the stream, so `streamOffset_ - 1` is still the opcode byte.
    void noteOpcode(std::uint16_t opcode, OpcodeSupport support);

    // An opcode whose operands are read out of the original but whose effect is
    // deliberately not modelled. Consumes `expressionCount` expressions and then
    // `inlineBytes` raw stream bytes, in that order -- which is the shape of
    // nearly every handler in PTR_LAB_0031e228. Anything that reads its inline
    // bytes *before* its expressions, or that branches on an operand, needs a
    // real handler instead.
    //
    // The counts must come from src/FUN_*.c. A wrong count desyncs the whole
    // stream after it, which is exactly what haltUnimplemented exists to avoid.
    std::uint32_t consumeOnly(std::uint16_t opcode, int expressionCount, std::size_t inlineBytes = 0);
  };

} // namespace orphen::ported::script

#include "ported/script/scene_script.h"

namespace orphen::ported::script
{
  namespace
  {
    std::uint32_t readU32(std::span<const std::uint8_t> bytes, std::size_t offset)
    {
      if (offset + 4 > bytes.size())
      {
        return 0;
      }
      return static_cast<std::uint32_t>(bytes[offset]) |
             (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
             (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
             (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
    }
  } // namespace

  const char *sceneScriptEntryName(SceneScriptEntry entry)
  {
    switch (entry)
    {
    case SceneScriptEntry::Init: return "init";
    case SceneScriptEntry::Start: return "start";
    case SceneScriptEntry::Tick: return "tick";
    case SceneScriptEntry::ActorStatePrimary: return "actor-primary";
    case SceneScriptEntry::ActorStateSecondary: return "actor-secondary";
    default: return "?";
    }
  }

  bool SceneScript::load(std::span<const std::uint8_t> decodedScript)
  {
    blob_.clear();
    texturePageIds_.clear();
    // FUN_0025b390 clears the work array and the object-script slots at load.
    state_ = SceneScriptState{};

    if (decodedScript.size() < kSceneScriptHeaderWordCount * 4)
    {
      return false;
    }

    blob_.assign(decodedScript.begin(), decodedScript.end());
    for (std::size_t index = 0; index < kSceneScriptHeaderWordCount; ++index)
    {
      headerWords_[index] = readU32(blob_, index * 4);
    }

    // Header word 6: a zero-terminated list of texture page ids.
    const std::uint32_t texturePageOffset = headerWords_[6];
    if (texturePageOffset != 0 && texturePageOffset < blob_.size())
    {
      for (std::size_t offset = texturePageOffset; offset + 1 < blob_.size(); offset += 2)
      {
        const std::uint16_t pageId =
            static_cast<std::uint16_t>(blob_[offset] | (blob_[offset + 1] << 8));
        if (pageId == 0)
        {
          break;
        }
        texturePageIds_.push_back(pageId);
      }
    }

    FUN_0025b600_read_scene_defaults();
    FUN_0025b288_read_dialogue_window();
    return true;
  }

  // FUN_0025b288 (0x0025b288), called from FUN_0025b390 at scene load.
  //
  // Header word 5 points at the dialogue pointer table: ascending record
  // offsets terminated by a zero. This keeps the first entry and the last
  // non-zero one, and the pair becomes the window FUN_0025ce30 tests a
  // scheduler target against to decide "is this a line of dialogue or a piece of
  // script?".
  //
  // The bound is genuinely half-open at the top -- FUN_0025ce30 rejects a target
  // that is `>=` the last entry, so the final record in the table is treated as
  // script. That looks like an off-by-one and is reproduced rather than fixed,
  // because the scene data was authored against it.
  void SceneScript::FUN_0025b288_read_dialogue_window()
  {
    state_.DAT_00355ce0_dialogueWindowFirst = 0;
    state_.DAT_00355ce4_dialogueWindowLast = 0;
    dialogueRecordOffsets_.clear();

    const std::uint32_t tableOffset = headerWords_[5];
    if (tableOffset == 0 || tableOffset + 4 > blob_.size())
    {
      return;
    }

    for (std::size_t offset = tableOffset; offset + 4 <= blob_.size(); offset += 4)
    {
      const std::uint32_t entry = readU32(blob_, offset);
      if (entry == 0)
      {
        break;
      }
      dialogueRecordOffsets_.push_back(entry);
    }
    if (dialogueRecordOffsets_.empty())
    {
      return;
    }
    state_.DAT_00355ce0_dialogueWindowFirst = dialogueRecordOffsets_.front();
    state_.DAT_00355ce4_dialogueWindowLast = dialogueRecordOffsets_.back();
  }

  std::pair<std::uint32_t, std::uint32_t> SceneScript::dialogueRecordBounds(std::uint32_t offset) const
  {
    // The table is ascending, so the record containing `offset` is the last
    // entry at or below it. Opcode 0x33's inline text is not in the table at
    // all -- it points into the middle of the code -- so an offset past the
    // last entry falls back to running to the end of the blob, and the
    // printable-run scan stops at the first control code either way.
    std::pair<std::uint32_t, std::uint32_t> bounds{offset, static_cast<std::uint32_t>(blob_.size())};
    for (std::size_t index = 0; index < dialogueRecordOffsets_.size(); ++index)
    {
      if (dialogueRecordOffsets_[index] > offset)
      {
        bounds.second = dialogueRecordOffsets_[index];
        break;
      }
      bounds.first = dialogueRecordOffsets_[index] <= offset ? offset : bounds.first;
    }
    return bounds;
  }

  // FUN_0025b600 (0x0025b600), called from FUN_0022a418 with the 0x325368
  // per-scene defaults struct.
  //
  // This is where a scene's **spawn point** comes from, and the port had no idea
  // it existed. The block sits immediately after header word 6's texture page
  // list: skip one halfword, copy sixteen halfwords, align to 4, then read four
  // ints scaled by **1000.0** -- not the 100000.0 the coordinate opcodes use.
  //
  //   [0] [1] [2]  ->  struct +0x4C/+0x50/+0x54 == 0x3253B4, the *backup* spawn
  //   [3]          ->  struct +0x24 == DAT_0032538c, the scene's draw distance
  //
  // FUN_0022a418 then copies the backup into DAT_00325340 when DAT_003551ec has
  // bit 0x2000, and applies it to pool slot 0 when it has bit 1. FUN_002000c0
  // sets 0x2001 at boot, so arriving without an explicit warp -- which is what
  // the debug menu's map-select does -- lands on the script's own spawn. A warp
  // from another map overrides it through FUN_0022b2c0.
  //
  // Verified against an EE dump of s01_e024: the block reads
  // (-3250, -12750, 0, 32000) and the player is at (-3.25, -12.75, 0) with the
  // draw distance at 32.0.
  void SceneScript::FUN_0025b600_read_scene_defaults()
  {
    sceneSpawn_.reset();
    sceneDrawDistance_.reset();

    const std::uint32_t texturePageOffset = headerWords_[6];
    if (texturePageOffset == 0 || texturePageOffset >= blob_.size())
    {
      return;
    }

    // The copy loop runs 16 times from word6 + 2, so the ints start after it.
    std::size_t offset = texturePageOffset + 2 + 16 * 2;
    // `puVar3 + (4 - (addr & 3)) / 2` over a halfword pointer. Note this always
    // advances -- an already-aligned address moves on by a whole 4 bytes.
    offset += static_cast<std::size_t>((4 - (offset & 3)) / 2) * 2;
    if (offset + 16 > blob_.size())
    {
      return;
    }

    const auto readI32 = [this](std::size_t at) {
      return static_cast<std::int32_t>(readU32(blob_, at));
    };
    sceneSpawn_ = orphen::ported::psm2::Vec3{static_cast<float>(readI32(offset)) / kSceneDefaultScale,
                                             static_cast<float>(readI32(offset + 4)) / kSceneDefaultScale,
                                             static_cast<float>(readI32(offset + 8)) / kSceneDefaultScale};
    sceneDrawDistance_ = static_cast<float>(readI32(offset + 12)) / kSceneDefaultScale;
  }

  std::uint32_t SceneScript::headerWord(std::size_t index) const
  {
    return index < kSceneScriptHeaderWordCount ? headerWords_[index] : 0;
  }

  std::uint32_t SceneScript::entryOffset(SceneScriptEntry entry) const
  {
    return headerWord(static_cast<std::size_t>(entry));
  }

  bool SceneScript::runAtOffset(std::uint32_t offset,
                                const ScriptEnvironment &environment,
                                ScriptTrace &trace,
                                std::size_t selectedEntity)
  {
    SceneCommandInterpreter interpreter(blob_, environment, trace);
    if (selectedEntity != kNoSelectedEntity)
    {
      interpreter.selectEntity(selectedEntity);
    }
    const bool completed = interpreter.FUN_0025bc68_run(offset);

    // A halt is sticky across a tick: once any slot has stopped on an
    // unimplemented opcode, the run is not clean, and reporting only the last
    // slot's result would hide it.
    if (interpreter.overran() && !lastRunOverran_)
    {
      lastRunOverran_ = true;
      lastHaltOffset_ = interpreter.haltOffset();
      lastOverrunEntry_ = offset;
    }
    if (interpreter.haltedOnUnimplemented() && !lastRunHaltedOnUnimplemented_)
    {
      lastRunHaltedOnUnimplemented_ = true;
      lastHaltOpcode_ = interpreter.haltOpcode();
      lastHaltOffset_ = interpreter.haltOffset();
    }
    return completed;
  }

  bool SceneScript::runEntry(SceneScriptEntry entry,
                             const ScriptEnvironment &environment,
                             ScriptTrace &trace)
  {
    lastRunOverran_ = false;
    lastRunHaltedOnUnimplemented_ = false;
    lastHaltOpcode_ = 0;
    lastHaltOffset_ = 0;

    if (!loaded())
    {
      return false;
    }

    const std::uint32_t offset = entryOffset(entry);
    const bool completed = runAtOffset(offset, environment, trace, kNoSelectedEntity);

    // An entry whose body is a single block end is legitimately empty -- header
    // word 4 in s01_e024 is exactly that.
    const bool empty = offset < blob_.size() && blob_[offset] == 0x04;
    trace.noteEntryRun(sceneScriptEntryName(entry), offset, empty);

    return completed;
  }

  bool SceneScript::runEntryForEntity(SceneScriptEntry entry,
                                      const ScriptEnvironment &environment,
                                      ScriptTrace &trace,
                                      std::size_t selectedEntity)
  {
    lastRunOverran_ = false;
    lastRunHaltedOnUnimplemented_ = false;
    lastHaltOpcode_ = 0;
    lastHaltOffset_ = 0;

    if (!loaded())
    {
      return false;
    }

    const std::uint32_t offset = entryOffset(entry);
    const bool completed = runAtOffset(offset, environment, trace, selectedEntity);
    const bool empty = offset < blob_.size() && blob_[offset] == 0x04;
    trace.noteEntryRun(sceneScriptEntryName(entry), offset, empty);
    return completed;
  }

  bool SceneScript::FUN_0025b778_run_tick(const ScriptEnvironment &environment, ScriptTrace &trace)
  {
    lastRunOverran_ = false;
    lastRunHaltedOnUnimplemented_ = false;
    lastHaltOpcode_ = 0;
    lastHaltOffset_ = 0;

    if (!loaded())
    {
      return false;
    }

    // The per-frame entry itself.
    const std::uint32_t tickOffset = entryOffset(SceneScriptEntry::Tick);
    trace.recordTickRun();
    bool completed = runAtOffset(tickOffset, environment, trace, kNoSelectedEntity);

    FUN_0025ce30_run_event_scheduler(environment, trace);

    // The 62 general slots, in order, each with the current-slot global set so
    // opcode 0x9E can retire the slot it is running in.
    for (std::size_t slot = 0; slot < SceneScriptState::kGeneralSlotCount; ++slot)
    {
      const std::uint32_t offset = state_.DAT_00355cf4_objectScriptSlots[slot];
      if (offset == 0)
      {
        continue;
      }
      // FUN_0025b778:22-24. The line goes out *before* the slot runs, so a slot
      // that retires itself this frame is still listed. The id is the dword
      // immediately in front of the body -- the original reads the slot as an
      // absolute pointer and indexes [-4]; here the slot holds a blob offset.
      if ((environment.DAT_003555dd_debugDisplay & ScriptEnvironment::kSubprocDisplayBit) != 0 &&
          environment.FUN_002681c0_subprocLine)
      {
        environment.FUN_002681c0_subprocLine(static_cast<int>(slot), subprocIdAt(offset));
      }

      state_.DAT_00355cf8_currentSlot = static_cast<std::int32_t>(slot);
      trace.recordSlotRun();
      completed = runAtOffset(offset, environment, trace, kNoSelectedEntity) && completed;
    }
    state_.DAT_00355cf8_currentSlot = -1;

    // The lead-bound slot, with both entity selection globals on pool slot 0.
    const std::uint32_t leadOffset = state_.DAT_00355cf4_objectScriptSlots[SceneScriptState::kLeadSlot];
    if (leadOffset != 0)
    {
      completed = runAtOffset(leadOffset, environment, trace, 0) && completed;
    }

    // FUN_0025b778:57. The cinematic bars, stepped once per tick whether or
    // not this scene ever arms them -- that is what runs the slide down after
    // an operand-1 0x6D and clears the mode at the bottom of it.
    if (environment.DAT_00355054_letterbox != nullptr)
    {
      environment.DAT_00355054_letterbox->FUN_0025cfb8_step(environment.frameTicks);
    }

    // FUN_0025b778:38-58, after FUN_0025cfb8. Four words of display mask at
    // DAT_0031e770, one bit per work word; every set bit prints its work value
    // twice, once decimal and once hex. Nothing in the game writes the mask --
    // only FUN_0026a508, the SCEN WORK DISP submenu -- so this is inert until
    // that menu turns a slot on.
    if (environment.FUN_002681c0_sceneWorkLine)
    {
      for (std::size_t word = 0; word < SceneScriptState::kSceneWorkDisplayWords; ++word)
      {
        const std::uint32_t mask = state_.DAT_0031e770_sceneWorkDisplayMask[word];
        if (mask == 0)
        {
          continue;
        }
        for (std::size_t bit = 0; bit < 32; ++bit)
        {
          if ((mask & (1u << bit)) == 0)
          {
            continue;
          }
          const std::size_t index = word * 32 + bit;
          environment.FUN_002681c0_sceneWorkLine(static_cast<int>(index),
                                                 state_.DAT_00355060_work[index]);
        }
      }
    }

    return completed;
  }

  // FUN_0025b778:23's `*(u32 *)(body - 4)`. The scheduler and opcode 0x9D both
  // install a body offset; the dword in front of it is the authored subproc id
  // the debug overlay names. Returns -1 when there is no room for one, so a
  // body at the very start of the blob reads as "no id" rather than as garbage.
  std::int32_t SceneScript::subprocIdAt(std::uint32_t bodyOffset) const
  {
    if (bodyOffset < 4 || bodyOffset > blob_.size())
    {
      return -1;
    }
    const std::size_t at = static_cast<std::size_t>(bodyOffset) - 4;
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(blob_[at]) |
                                     (static_cast<std::uint32_t>(blob_[at + 1]) << 8) |
                                     (static_cast<std::uint32_t>(blob_[at + 2]) << 16) |
                                     (static_cast<std::uint32_t>(blob_[at + 3]) << 24));
  }

  // FUN_0025ce30 (0x0025ce30). See analyzed/process_entity_queue_system.c.
  //
  // Four channels, each a cursor into a stream of 8-byte records:
  //
  //     [u16 delayUnits][u16 gate][u32 targetOffset]
  //
  // Per frame, per channel: check the gate, then accumulate the frame tick into
  // the channel timer until `timer >> 5` reaches delayUnits, then pay the record
  // out and reset the timer. The `>> 5` is against the nominal 0x20 ticks a
  // frame, so delayUnits is a frame count.
  //
  // The gate has three readings and the order matters:
  //   gate == 0            -- no condition
  //   bit 15 clear         -- wait for event flag `gate` to be set
  //   bit 15 set           -- wait until `gate & (uGpffffb0f4 | 0x8000) == gate`
  //
  // A blocked channel does not advance its timer at all, so a delay is measured
  // from when the gate opened rather than from when the stream was armed.
  void SceneScript::FUN_0025ce30_run_event_scheduler(const ScriptEnvironment &environment,
                                                     ScriptTrace &trace)
  {
    for (std::size_t index = 0; index < SceneScriptState::kEventChannelCount; ++index)
    {
      auto &channel = state_.DAT_00571e40_eventChannels[index];
      if (channel.cursor == 0 ||
          channel.cursor + SceneScriptState::kEventRecordSize > blob_.size())
      {
        continue;
      }

      const std::uint16_t delayUnits =
          static_cast<std::uint16_t>(blob_[channel.cursor] | (blob_[channel.cursor + 1] << 8));
      const std::uint16_t gate =
          static_cast<std::uint16_t>(blob_[channel.cursor + 2] | (blob_[channel.cursor + 3] << 8));

      if (gate != 0)
      {
        if ((gate & 0x8000u) == 0)
        {
          if (!state_.FUN_00266368_eventFlag(gate))
          {
            continue;
          }
        }
        else if ((gate & (state_.uGpffffb0f4_gateMask | 0x8000u)) != gate)
        {
          continue;
        }
      }

      if (((channel.timer >> 5) & 0xFFFFu) < delayUnits)
      {
        channel.timer += environment.frameTicks;
        continue;
      }

      const std::uint32_t targetOffset = readU32(blob_, channel.cursor + 4);

      ScriptTrace::EventDispatch dispatch;
      dispatch.frame = trace.frame();
      dispatch.channel = static_cast<std::uint8_t>(index);
      dispatch.delayUnits = delayUnits;
      dispatch.gate = gate;
      dispatch.targetOffset = targetOffset;

      // A target inside the dialogue pointer table's range is a line of text and
      // starts immediately; anything else is a script body and is queued.
      const bool toDialogue = targetOffset >= state_.DAT_00355ce0_dialogueWindowFirst &&
                              targetOffset < state_.DAT_00355ce4_dialogueWindowLast;
      dispatch.toDialogue = toDialogue;

      if (toDialogue)
      {
        if (environment.FUN_00237b38_start_dialogue)
        {
          environment.FUN_00237b38_start_dialogue(targetOffset);
        }
      }
      else
      {
        const std::int32_t slot = state_.findFreeObjectScriptSlot();
        dispatch.slot = slot;
        if (slot >= 0)
        {
          state_.DAT_00355cf4_objectScriptSlots[static_cast<std::size_t>(slot)] = targetOffset;
        }
      }
      trace.recordEventDispatch(dispatch);

      channel.cursor += SceneScriptState::kEventRecordSize;
      ++channel.consumed;
      channel.timer = 0;

      // The stream ends where the *next* record's target offset is zero -- not
      // on a zero delay or a zero gate, both of which are ordinary values.
      if (channel.cursor + SceneScriptState::kEventRecordSize > blob_.size() ||
          readU32(blob_, channel.cursor + 4) == 0)
      {
        channel.cursor = 0;
      }
    }
  }

  bool SceneScript::FUN_0025b918_run_late_slots(const ScriptEnvironment &environment, ScriptTrace &trace)
  {
    if (!loaded())
    {
      return false;
    }

    bool completed = true;
    for (std::size_t slot = SceneScriptState::kFirstLateSlot;
         slot < SceneScriptState::kFirstLateSlot + 2;
         ++slot)
    {
      const std::uint32_t offset = state_.DAT_00355cf4_objectScriptSlots[slot];
      if (offset == 0)
      {
        continue;
      }
      state_.DAT_00355cf8_currentSlot = static_cast<std::int32_t>(slot);
      trace.recordSlotRun();
      completed = runAtOffset(offset, environment, trace, kNoSelectedEntity) && completed;
    }
    state_.DAT_00355cf8_currentSlot = -1;
    return completed;
  }

  bool SceneScript::FUN_0025bf20_run_npc_body(std::int16_t bodyOffset,
                                              const ScriptEnvironment &environment,
                                              ScriptTrace &trace,
                                              std::size_t entitySlot)
  {
    // `jal FUN_0025b9c0` returns the blob base and the body offset is added to
    // it, so a zero offset would run the blob's own header as code. The original
    // is guarded by FUN_0025bc68's `param_1 != 0` test; the port needs the same
    // guard plus a bounds check, because 0x66 will happily park any u32 here.
    if (!loaded() || bodyOffset <= 0 || static_cast<std::size_t>(bodyOffset) >= blob_.size())
    {
      return true;
    }

    // FUN_0025bf20 stores the entity into iGpffffb0d4 *and* iGpffffb0d8 before
    // running: an NPC body's choreography opcodes act on the NPC itself unless
    // it explicitly refocuses with 0xEB.
    state_.puGpffffb0d8_focusEntity = entitySlot;
    return runAtOffset(static_cast<std::uint32_t>(bodyOffset), environment, trace, entitySlot);
  }

  std::size_t SceneScript::occupiedObjectScriptSlots() const
  {
    std::size_t count = 0;
    for (std::size_t slot = 0; slot < SceneScriptState::kObjectScriptSlots; ++slot)
    {
      if (state_.DAT_00355cf4_objectScriptSlots[slot] != 0)
      {
        ++count;
      }
    }
    return count;
  }

  bool SceneScript::FUN_0025b6d0_run_init(const ScriptEnvironment &environment, ScriptTrace &trace)
  {
    // FUN_0025b6d0 clears 0x30 bytes at 0x571E40 -- which is exactly the four
    // 12-byte scheduler channels -- and resets DAT_0035504c, the 0x4E lookup
    // counter, before running the entry.
    //
    // Ahead of all of that is a branch nothing used to reach: when DAT_003555d3
    // is set -- the scene came out of the group-0xE list -- it writes 0x32 into
    // the work array. `*(u32 *)(DAT_00355060 + 0x68)` is a *byte* offset into a
    // dword array, so it is work word 0x1A.
    if (environment.DAT_003555d3_groupEScene)
    {
      state_.DAT_00355060_work[kGroupESceneWorkWord] = 0x32;
    }
    for (auto &channel : state_.DAT_00571e40_eventChannels)
    {
      channel = SceneScriptState::EventChannel{};
    }
    state_.DAT_0035504c_lookupCount = 0;
    // `DAT_00355064 |= 0x6000`. Same word as uGpffffb0f4, the scheduler's second
    // gate: a scene *starts* with the text gate open, and only a running
    // dialogue stream closes it. Without this a first record gated on 0x6000 has
    // nothing to open it and the channel never advances -- which cost nothing
    // while every scene the port ran opened with dialogue, and stops a scene
    // that opens with a gated record dead.
    state_.uGpffffb0f4_gateMask |= 0x6000u;
    return runEntry(SceneScriptEntry::Init, environment, trace);
  }

  bool SceneScript::FUN_0025b728_run_start(const ScriptEnvironment &environment, ScriptTrace &trace)
  {
    return runEntry(SceneScriptEntry::Start, environment, trace);
  }

} // namespace orphen::ported::script

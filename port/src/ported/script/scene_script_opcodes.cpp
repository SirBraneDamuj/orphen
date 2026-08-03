// Statement opcode handlers for the SCR VM. Split out of
// scene_command_interpreter.cpp only for file size; they are members of the same
// class because they all share the one stream pointer.
//
// Every handler is named for the original it came from. Opcodes with no
// implementation halt rather than fall through, because their operands would go
// unconsumed and everything after them would decode as nonsense.

#include "ported/script/scene_command_interpreter.h"

#include "ported/entity/actor_frame_update.h"
#include "ported/script/object_registers.h"

#include <cmath>

namespace orphen::ported::script
{
  namespace
  {
    // fGpffff8c2c and fGpffff8c30 in the retail executable: the placement
    // record's signed angle byte becomes angle * (pi/4) + (pi/2).
    constexpr float kPlacementAngleStep = 0.785398006439209f;
    constexpr float kPlacementAngleBias = 1.570796012878418f;

    // Opcode 0x52 refuses to spawn this type, and opcode 0x51 skips lookup
    // entries carrying it. It reads as a "marker, not an actor" sentinel.
    constexpr std::uint32_t kNonSpawningTypeId = 0x55;

    // FUN_0025eb48's group-3 branch spawns this type unconditionally.
    constexpr std::int32_t kGroup3TypeId = 0x3A;

    // FUN_0025eb48's group-3 branch biases the record's param byte into the
    // event-flag space: entity +0x198 = param + 0x400.
    constexpr std::uint32_t kChestFlagBase = 0x400;

    // FUN_00216690 now lives in object_registers.* because the register writes
    // need it too. Both placement paths apply it and the port previously did not
    // -- invisible to a cos/sin, but it matters to the opcodes that read +0x5C
    // back.
    using orphen::ported::script::FUN_00216690_wrapAngle;
  } // namespace

  std::uint32_t SceneCommandInterpreter::haltUnimplemented(std::uint16_t opcode)
  {
    halted_ = true;
    haltedOnUnimplemented_ = true;
    haltOpcode_ = opcode;
    // The opcode byte has already been consumed by the dispatch loop.
    haltOffset_ = streamOffset_ ? streamOffset_ - 1 : 0;
    return 0;
  }

  orphen::ported::entity::OriginalEntity *SceneCommandInterpreter::resolveEntity(std::uint32_t index)
  {
    if (environment_.entityPool == nullptr)
    {
      return nullptr;
    }
    if (index == orphen::ported::entity::kCurrentEntityIndex)
    {
      return currentEntity_ < orphen::ported::entity::kEntitySlotCount
                 ? &environment_.entityPool->slot(currentEntity_)
                 : nullptr;
    }
    if (index >= orphen::ported::entity::kEntitySlotCount)
    {
      return nullptr;
    }
    currentEntity_ = index;
    return &environment_.entityPool->slot(index);
  }

  // 0x01 (FUN_0025bdd0): take the branch when the expression is zero, otherwise
  // step over the four-byte offset.
  void SceneCommandInterpreter::FUN_0025bdd0_conditional_jump()
  {
    const std::uint32_t condition = FUN_0025c258_evaluate();
    if (condition == 0)
    {
      FUN_0025c220_relativeJump();
    }
    else
    {
      streamOffset_ += 4;
      if (streamOffset_ > blob_.size())
      {
        halted_ = true;
        overran_ = true;
      }
    }
  }

  // 0x02 (FUN_0025be10): switch. Evaluate the selector, read a case count byte,
  // align to 4, then scan (key, target) pairs. On a match the pointer lands on
  // that pair's target; with no match it lands past the table, on the default.
  // Either way the relative jump is taken from wherever it ended up.
  void SceneCommandInterpreter::FUN_0025be10_switch_dispatch()
  {
    const std::uint32_t selector = FUN_0025c258_evaluate();
    const std::uint8_t caseCount = readU8();
    alignStreamTo4();
    if (halted_)
    {
      return;
    }

    for (std::uint8_t index = 0; index < caseCount; ++index)
    {
      if (!canRead(8))
      {
        halted_ = true;
        overran_ = true;
        return;
      }
      const std::uint32_t key = static_cast<std::uint32_t>(blob_[streamOffset_]) |
                                (static_cast<std::uint32_t>(blob_[streamOffset_ + 1]) << 8) |
                                (static_cast<std::uint32_t>(blob_[streamOffset_ + 2]) << 16) |
                                (static_cast<std::uint32_t>(blob_[streamOffset_ + 3]) << 24);
      if (key == selector)
      {
        streamOffset_ += 4; // point at this case's target
        FUN_0025c220_relativeJump();
        return;
      }
      streamOffset_ += 8;
    }

    FUN_0025c220_relativeJump(); // the default target sits past the pairs
  }

  // 0x4D (FUN_0025e628): count byte, then that many raw dwords, each a resource
  // id handed to FUN_002661f8 -> FUN_002661a8 to load a model. The port has no
  // models, so it records the list; negative ids are skipped by the original's
  // walker and are skipped here too.
  void SceneCommandInterpreter::FUN_0025e628_process_resource_ids()
  {
    const std::uint8_t count = readU8();
    for (std::uint8_t index = 0; index < count && !halted_; ++index)
    {
      const std::uint32_t resourceId = FUN_0025c1d0_readStreamU32();
      const std::int16_t narrowed = static_cast<std::int16_t>(resourceId & 0xFFFF);
      if (narrowed > 0)
      {
        trace_.recordPreloadedResource(static_cast<std::uint16_t>(narrowed));
      }
    }
  }

  // 0x4E (FUN_0025e730): expression, raw dword, expression. Stored as a 12-byte
  // entry [value1][value3][value2] in the 16-entry table opcode 0x51 reads.
  void SceneCommandInterpreter::FUN_0025e730_push_lookup_entry()
  {
    const std::uint32_t value1 = FUN_0025c258_evaluate();
    const std::uint32_t value3 = FUN_0025c1d0_readStreamU32();
    const std::uint32_t value2 = FUN_0025c258_evaluate();

    SceneScriptState &state = *environment_.state;
    if (state.DAT_0035504c_lookupCount < SceneScriptState::kLookupCapacity)
    {
      state.DAT_00571d00_lookup[state.DAT_0035504c_lookupCount] =
          SceneScriptState::LookupEntry{value1, value3, value2};
      ++state.DAT_0035504c_lookupCount;
    }
    // The original prints an overflow warning and drops the entry.
  }

  void SceneCommandInterpreter::placeFromRecord(std::size_t slot,
                                                std::size_t recordIndex,
                                                const orphen::ported::psm2::ObjectPlacementRecord &record,
                                                SpawnRecord &spawnRecord)
  {
    auto &entity = environment_.entityPool->slot(slot);

    // FUN_002662e0: +0x20/+0x24/+0x28, with z mirrored into +0x4C.
    entity.positionX20 = record.position.x;
    entity.positionZ24 = record.position.y;
    entity.positionY28 = record.position.z;
    entity.groundHeight4c = record.position.z;
    entity.facingRadians5c =
        FUN_00216690_wrapAngle(static_cast<float>(record.angle) * kPlacementAngleStep + kPlacementAngleBias);

    // Both spawn paths record which placement record built the entity:
    // FUN_0025e7c0 at *(int *)(pw + 0x4c) and FUN_0025eb48 at the same offset,
    // over an undefined2 * -- byte 0x98. Opcode 0x5A searches on it.
    entity.placementRecordIndex98 = static_cast<std::int32_t>(recordIndex);

    if (environment_.terrainHeight)
    {
      const auto height = environment_.terrainHeight(record.position.x, record.position.y);
      if (height.has_value())
      {
        entity.groundHeight4c = *height;
        spawnRecord.grounded = true;
      }
    }

    spawnRecord.slot = slot;
    spawnRecord.allocated = true;
    spawnRecord.descriptorResolved = entity.modelIndex >= 0;
    spawnRecord.positioned = true;
    spawnRecord.x = entity.positionX20;
    spawnRecord.y = entity.positionZ24;
    spawnRecord.z = entity.positionY28;
  }

  // 0x4F (FUN_0025e7c0): walk the map's object placement table and instantiate
  // every record in groups 0, 4 and 5. The record's +0x0E id is one-based and
  // maps into the *map-streamed* descriptor bands rather than the executable's
  // static tables:
  //
  //   group 0 -> (id - 1) + 0x272
  //   group 4 -> (id - 1) + 0x373
  //   group 5 -> (id - 1) + 0x474
  //
  // Those descriptors ship with the map, so the port cannot resolve them out of
  // SLUS_200.11 and the entity keeps default collision dimensions. That is
  // recorded rather than papered over.
  void SceneCommandInterpreter::FUN_0025e7c0_process_placements()
  {
    if (environment_.map == nullptr || environment_.entityPool == nullptr)
    {
      return;
    }

    const auto &placements = environment_.map->DAT_003556e8_objectPlacements;
    for (std::size_t recordIndex = 0; recordIndex < placements.size(); ++recordIndex)
    {
      const auto &record = placements[recordIndex];
      const std::int8_t group = record.group;
      if (group != 0 && group != 4 && group != 5)
      {
        continue;
      }

      const std::int32_t index = static_cast<std::int32_t>(record.id) - 1;
      if (index < 0)
      {
        continue;
      }

      std::int32_t typeId = index + 0x272;
      if (group == 4)
      {
        typeId = index + 0x373;
      }
      else if (group == 5)
      {
        typeId = index + 0x474;
      }

      SpawnRecord &spawnRecord = trace_.beginSpawn();
      spawnRecord.scriptOffset = streamOffset_;
      spawnRecord.typeId = typeId;

      const std::size_t slot =
          environment_.entityPool->FUN_00265e28_allocate_and_initialize(typeId, *environment_.descriptors);
      if (slot >= orphen::ported::entity::kEntitySlotCount)
      {
        continue;
      }
      currentEntity_ = slot;
      placeFromRecord(slot, recordIndex, record, spawnRecord);
    }
  }

  // 0x61 (FUN_0025f4b8): mask expression, then a raw selector byte; returns
  // whether any masked bit is set in one of two words.
  //
  // The three globals it reads are fields of pool slot 0, not standalone state:
  // DAT_0058bebc is 0x58BEB0 + 0x0C, DAT_0058bf1c is +0x6C and DAT_0058bf20 is
  // +0x70 -- all on the lead player. analyzed/ops/0x61_*.c describes them as
  // cached controller state, which is a guess; the addresses say otherwise.
  //
  // Selector bit 7 picks +0x70 over +0x6C. Bits 0..6, if any are set, gate the
  // whole test on +0x0C bit 0. +0x0C bit 0x100 disables it outright.
  std::uint32_t SceneCommandInterpreter::FUN_0025f4b8_test_lead_flag_word()
  {
    const std::uint32_t callOffset = streamOffset_;
    const std::uint32_t mask = FUN_0025c258_evaluate();
    const std::uint8_t selector = readU8();
    if (halted_ || environment_.entityPool == nullptr)
    {
      return 0;
    }

    const auto &lead = environment_.entityPool->leadPlayer();
    const std::uint32_t gate = lead.collisionFlags0c;
    const std::uint32_t word = (selector & 0x80u) != 0 ? lead.flagWord70 : lead.flagWord6c;

    bool passed = false;
    if ((gate & 0x100u) == 0 && ((selector & 0x7Fu) == 0 || (gate & 1u) != 0))
    {
      passed = (word & mask) != 0;
    }

    // Every call site is recorded with its mask, because a panel's flag cannot
    // be learned any other way: the branch stays untaken until the player is
    // standing on the surface that carries it.
    trace_.recordTerrainTrigger(callOffset, mask, selector, word, passed);
    return passed ? 1u : 0u;
  }

  // 0x9D (FUN_00261cb8): install an object script into a slot. The operand is a
  // raw dword offset from the script base, which the original turns into an
  // absolute pointer; the port keeps it as a blob offset. Note the bound is
  // 0x40, so 0x9D cannot reach the lead-bound slot -- that is 0xA8's job.
  std::uint32_t SceneCommandInterpreter::FUN_00261cb8_install_slot()
  {
    const std::uint32_t slot = FUN_0025c258_evaluate();
    const std::uint32_t offset = FUN_0025c1d0_readStreamU32();
    if (halted_)
    {
      return 0;
    }
    if (slot >= SceneScriptState::kLeadSlot)
    {
      // The original reports a fatal diagnostic and stops. A slot index this far
      // out means the stream is not where we think it is.
      return haltUnimplemented(currentOpcode_);
    }
    environment_.state->DAT_00355cf4_objectScriptSlots[slot] = offset;
    return 0;
  }

  // 0x9E (FUN_00261d18): clear a slot. A negative selector means "the slot I am
  // running in", which is how a one-shot object script retires itself. The bound
  // here is 0x41, so unlike 0x9D this one can clear the lead slot.
  std::uint32_t SceneCommandInterpreter::FUN_00261d18_clear_slot()
  {
    const std::uint32_t selector = FUN_0025c258_evaluate();
    if (halted_)
    {
      return 0;
    }

    SceneScriptState &state = *environment_.state;
    std::uint32_t slot = selector;
    if (static_cast<std::int32_t>(selector) < 0)
    {
      if (state.DAT_00355cf8_currentSlot < 0)
      {
        return haltUnimplemented(currentOpcode_);
      }
      slot = static_cast<std::uint32_t>(state.DAT_00355cf8_currentSlot);
    }
    else if (selector > SceneScriptState::kLeadSlot)
    {
      return haltUnimplemented(currentOpcode_);
    }

    if (slot < SceneScriptState::kObjectScriptSlots)
    {
      state.DAT_00355cf4_objectScriptSlots[slot] = 0;
    }
    return 0;
  }

  // 0x9F (FUN_00261d88): is a slot occupied? The index is a raw stream byte, not
  // an expression -- one of the few opcodes that mixes the two.
  std::uint32_t SceneCommandInterpreter::FUN_00261d88_slot_occupied()
  {
    const std::uint8_t slot = readU8();
    if (halted_)
    {
      return 0;
    }
    if (slot > SceneScriptState::kLeadSlot)
    {
      return haltUnimplemented(currentOpcode_);
    }
    return environment_.state->DAT_00355cf4_objectScriptSlots[slot] != 0 ? 1u : 0u;
  }

  // 0xA0 (FUN_00261de0): the first free general slot, or -1 when all 62 are
  // taken. Only searches 0x00..0x3D; the late slots and the lead slot are not
  // allocatable.
  std::uint32_t SceneCommandInterpreter::FUN_00261de0_find_free_slot()
  {
    const SceneScriptState &state = *environment_.state;
    for (std::size_t slot = 0; slot < SceneScriptState::kGeneralSlotCount; ++slot)
    {
      if (state.DAT_00355cf4_objectScriptSlots[slot] == 0)
      {
        return static_cast<std::uint32_t>(slot);
      }
    }
    return static_cast<std::uint32_t>(-1);
  }

  // 0xA8 (FUN_00262f38): install the lead-bound slot and put the lead player
  // into the state that runs it.
  std::uint32_t SceneCommandInterpreter::FUN_00262f38_install_lead_slot()
  {
    const std::uint32_t offset = FUN_0025c1d0_readStreamU32();
    if (halted_)
    {
      return 0;
    }
    environment_.state->DAT_00355cf4_objectScriptSlots[SceneScriptState::kLeadSlot] = offset;
    if (environment_.entityPool != nullptr)
    {
      orphen::ported::entity::FUN_00225bf0_set_state_and_animation(
          environment_.entityPool->leadPlayer(), 10, 1);
    }
    return 0;
  }

  // 0xAA (FUN_00263118): cancel lead motion and clear the lead-bound slot. The
  // motion cancel is FUN_00252d88, which the port has not ported; clearing the
  // slot is the part that matters here and the part that is observable.
  std::uint32_t SceneCommandInterpreter::FUN_00263118_clear_lead_slot()
  {
    environment_.state->DAT_00355cf4_objectScriptSlots[SceneScriptState::kLeadSlot] = 0;
    return 0;
  }

  // 0x96 (FUN_002618c0): three expressions packed into uGpffffb6fc as 0xRRGGBB.
  void SceneCommandInterpreter::FUN_002618c0_set_global_rgb()
  {
    const std::uint32_t red = FUN_0025c258_evaluate();
    const std::uint32_t green = FUN_0025c258_evaluate();
    const std::uint32_t blue = FUN_0025c258_evaluate();
    environment_.state->uGpffffb6fc_globalRgb = (red << 16) | (green << 8) | blue;
  }

  // 0x97 (FUN_00261910): a direction vector then a colour. The vector is scaled
  // the same way world coordinates are; the colour packs like 0x96.
  void SceneCommandInterpreter::FUN_00261910_set_vector_with_rgb()
  {
    for (int axis = 0; axis < 3; ++axis)
    {
      environment_.state->DAT_003439c8_vector[axis] =
          static_cast<float>(static_cast<std::int32_t>(FUN_0025c258_evaluate())) / kScriptCoordinateScale;
    }
    const std::uint32_t red = FUN_0025c258_evaluate();
    const std::uint32_t green = FUN_0025c258_evaluate();
    const std::uint32_t blue = FUN_0025c258_evaluate();
    environment_.state->uGpffffb700_vectorRgb = (red << 16) | (green << 8) | blue;
  }

  // 0x36 / 0x38 (FUN_0025d768): read one word of the 128-entry work array, or
  // one byte of the flag array. The original range-checks and reports
  // ER_PARAM; the port clamps and returns zero instead of aborting the process.
  std::uint32_t SceneCommandInterpreter::FUN_0025d768_read_work_or_flag()
  {
    const std::uint32_t index = FUN_0025c258_evaluate();
    SceneScriptState &state = *environment_.state;

    if (currentOpcode_ == 0x36)
    {
      return index < SceneScriptState::kWorkWordCount ? state.DAT_00355060_work[index] : 0u;
    }

    const std::size_t bucket = static_cast<std::size_t>(static_cast<std::int32_t>(index)) >> 3;
    return bucket < SceneScriptState::kFlagBucketCount ? state.DAT_00342b70_flags[bucket] : 0u;
  }

  // 0x37 / 0x39 (FUN_0025d818): index expression, value expression, then a raw
  // sub-opcode byte in 0x25..0x2F selecting the assignment. Returns the result,
  // which scripts read back inside expressions.
  std::uint32_t SceneCommandInterpreter::FUN_0025d818_write_work_or_flag()
  {
    const bool targetsWork = (currentOpcode_ == 0x37);
    const std::uint32_t index = FUN_0025c258_evaluate();
    const std::uint32_t operand = FUN_0025c258_evaluate();
    const std::uint8_t operation = readU8();
    if (halted_)
    {
      return 0;
    }

    SceneScriptState &state = *environment_.state;
    const std::size_t bucket = static_cast<std::size_t>(static_cast<std::int32_t>(index)) >> 3;

    std::uint32_t value = 0;
    if (targetsWork)
    {
      value = index < SceneScriptState::kWorkWordCount ? state.DAT_00355060_work[index] : 0u;
    }
    else
    {
      value = bucket < SceneScriptState::kFlagBucketCount ? state.DAT_00342b70_flags[bucket] : 0u;
    }

    switch (operation)
    {
    case 0x25: value = operand; break;
    case 0x26: value = value * operand; break;
    case 0x27:
      if (operand == 0)
      {
        halted_ = true;
        overran_ = true;
        return 0;
      }
      value = static_cast<std::uint32_t>(static_cast<std::int32_t>(value) / static_cast<std::int32_t>(operand));
      break;
    case 0x28:
      if (operand == 0)
      {
        halted_ = true;
        overran_ = true;
        return 0;
      }
      value = static_cast<std::uint32_t>(static_cast<std::int32_t>(value) % static_cast<std::int32_t>(operand));
      break;
    case 0x29: value = value + operand; break;
    case 0x2A: value = value - operand; break;
    case 0x2B: value = value & operand; break;
    case 0x2C: value = value ^ operand; break;
    case 0x2D: value = value | operand; break;
    case 0x2E: value = value + 1; break;
    case 0x2F: value = value - 1; break;
    default:
      // The original reports an unknown-operation warning and leaves the target
      // untouched. Halt instead: an unknown sub-opcode means the stream is not
      // where we think it is.
      return haltUnimplemented(currentOpcode_);
    }

    if (targetsWork)
    {
      if (index < SceneScriptState::kWorkWordCount)
      {
        state.DAT_00355060_work[index] = value;
      }
    }
    else if (bucket < SceneScriptState::kFlagBucketCount)
    {
      state.DAT_00342b70_flags[bucket] = static_cast<std::uint8_t>(value);
    }

    return value;
  }

  // 0x3D..0x40 (FUN_0025e560): evaluate a resource id and answer whether it is
  // loaded, with 0x3E/0x3F/0x40 additionally requesting a load, release or
  // similar through FUN_002663a0 / FUN_002663d8 / FUN_00266418.
  //
  // The port has no resource manager, so it records the query and answers "not
  // loaded". That is the honest answer rather than a guessed one, and it is
  // stable: scripts branching on it take the same path every run.
  std::uint32_t SceneCommandInterpreter::FUN_0025e560_resource_flag()
  {
    const std::uint32_t resourceId = FUN_0025c258_evaluate();
    if (halted_)
    {
      return 0;
    }
    environment_.state->resourceQueries.push_back(resourceId);
    return 0;
  }

  // 0x77..0x7C (FUN_00260360): three expressions -- an object selector, a
  // register index, and a value -- then a read-modify-write on that object's
  // register bank. 0x77 assigns; 0x78..0x7C are AND, OR, XOR, ADD, SUB.
  //
  // FUN_0025d6c0 does the object selection and FUN_0025c548 / FUN_0025c8f8 the
  // register access. The port models the banks directly; what a given register
  // index means to the rest of the engine is not yet known, so nothing outside
  // the script reads them back.
  std::uint32_t SceneCommandInterpreter::FUN_00260360_modify_object_register()
  {
    const std::uint32_t selector = FUN_0025c258_evaluate();
    const std::uint32_t registerIndex = FUN_0025c258_evaluate();
    const std::uint32_t operand = FUN_0025c258_evaluate();
    if (halted_)
    {
      return 0;
    }

    // FUN_0025d6c0 selects the object the register writes go through. Index
    // 0x100 keeps whatever was already current.
    std::size_t bank = SceneScriptState::kObjectRegisterBanks - 1;
    if (selector != orphen::ported::entity::kCurrentEntityIndex)
    {
      resolveEntity(selector);
    }
    if (currentEntity_ < orphen::ported::entity::kEntitySlotCount)
    {
      bank = currentEntity_;
    }

    // The read-modify-write opcodes need the current value first, and every one
    // of them reads through FUN_0025c548 -- the same field, not a shadow copy.
    auto *entity = currentEntity_ < orphen::ported::entity::kEntitySlotCount && environment_.entityPool != nullptr
                       ? &environment_.entityPool->slot(currentEntity_)
                       : nullptr;

    std::uint32_t current = 0;
    const bool modelled = entity != nullptr &&
                          FUN_0025c548_read_object_register(*entity, registerIndex, current);
    if (!modelled)
    {
      current = environment_.state->objectRegister(bank, registerIndex);
    }

    std::uint32_t result = current;
    switch (currentOpcode_)
    {
    case 0x77: result = operand; break;
    case 0x78: result = current & operand; break;
    case 0x79: result = current | operand; break;
    case 0x7A: result = current ^ operand; break;
    case 0x7B: result = static_cast<std::uint32_t>(static_cast<std::int32_t>(current) + static_cast<std::int32_t>(operand)); break;
    case 0x7C: result = static_cast<std::uint32_t>(static_cast<std::int32_t>(current) - static_cast<std::int32_t>(operand)); break;
    default: return 0;
    }

    // Write to the entity field when the port models it, and to the side array
    // otherwise. The unmodelled case is counted rather than dropped quietly, so
    // --scr-report names the field a scene actually wanted.
    trace_.recordObjectRegisterAccess(registerIndex, true);
    if (entity == nullptr || !FUN_0025c8f8_write_object_register(*entity, registerIndex, result))
    {
      environment_.state->objectRegister(bank, registerIndex) = result;
      if (objectRegisterFieldName(registerIndex) != nullptr)
      {
        trace_.recordUnmodelledObjectRegister(registerIndex, entity == nullptr);
      }
    }
    return result;
  }

  // 0x76 (FUN_00260318): the read half of the object register family -- select
  // an object, then return one of its registers. Ghidra types it void because
  // the value comes back from a tail call into FUN_0025c548 in $v0.
  std::uint32_t SceneCommandInterpreter::FUN_00260318_read_object_register()
  {
    const std::uint32_t selector = FUN_0025c258_evaluate();
    const std::uint32_t registerIndex = FUN_0025c258_evaluate();
    if (halted_)
    {
      return 0;
    }

    std::size_t bank = SceneScriptState::kObjectRegisterBanks - 1;
    if (selector != orphen::ported::entity::kCurrentEntityIndex)
    {
      resolveEntity(selector);
    }
    if (currentEntity_ < orphen::ported::entity::kEntitySlotCount)
    {
      bank = currentEntity_;
    }

    trace_.recordObjectRegisterAccess(registerIndex, false);
    if (currentEntity_ < orphen::ported::entity::kEntitySlotCount && environment_.entityPool != nullptr)
    {
      std::uint32_t value = 0;
      if (FUN_0025c548_read_object_register(environment_.entityPool->slot(currentEntity_), registerIndex, value))
      {
        return value;
      }
    }
    // An index with no case at all reads 0 in the original too, so only a real
    // gap -- a named field the port has not modelled -- counts against us.
    if (objectRegisterFieldName(registerIndex) != nullptr)
    {
      trace_.recordUnmodelledObjectRegister(registerIndex, currentEntity_ >= orphen::ported::entity::kEntitySlotCount);
    }
    return environment_.state->objectRegister(bank, registerIndex);
  }

  // 0x70 (FUN_00260038): the angle from an object to the lead player, in script
  // units. One expression operand selects the object; FUN_0023a480 is
  // atan2(leadZ - objectZ, leadX - objectX) and the result is scaled by
  // fGpffff8c84 (100000.0) and truncated, the same scale everything else on this
  // path uses.
  //
  // Header word 3 -- the interaction hook -- reaches this immediately, which is
  // how a party member turns to face you when you talk to it.
  std::uint32_t SceneCommandInterpreter::FUN_00260038_angle_to_lead()
  {
    const std::uint32_t selector = FUN_0025c258_evaluate();
    if (halted_ || environment_.entityPool == nullptr)
    {
      return 0;
    }

    const auto *object = resolveEntity(selector);
    if (object == nullptr)
    {
      return 0;
    }

    const auto &lead = environment_.entityPool->leadPlayer();
    const float angle = std::atan2(lead.positionZ24 - object->positionZ24,
                                   lead.positionX20 - object->positionX20);
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(angle * kScriptCoordinateScale));
  }

  // 0x6D (FUN_0025fd10): take away or give back player control. One signed byte
  // operand. This is the outcome s01_e024's mask-0x1 floor panel reaches, at
  // script offset 0x495 -- the panel takes control away, and whatever the panel
  // is for then runs with the player parked.
  //
  //   <= 0  lead into state 10 / animation 1 (the locked pose), plus, for
  //         operands below -2, a battle-party handoff this port has no model
  //         for, and for -1 / -2, a countdown seed at uGpffffbd8c.
  //   == 1  release: FUN_00252d88 puts the lead back to idle.
  //
  // The port drives the state write and the release. What it cannot yet do is
  // *hold* the lock: the lead's state table entry 10 (0x00254cf0) is not ported,
  // so OriginalPlayerController's update falls into the grounded state and
  // resets +0x60 to 0 on the very next frame. So the write lands and is
  // immediately undone. That is a missing state handler, not a missing opcode,
  // and it is reported as such rather than papered over.
  std::uint32_t SceneCommandInterpreter::FUN_0025fd10_set_player_lock()
  {
    const auto mode = static_cast<std::int8_t>(readU8());
    if (halted_ || environment_.entityPool == nullptr)
    {
      return 0;
    }

    auto &lead = environment_.entityPool->leadPlayer();
    if (mode < 1)
    {
      orphen::ported::entity::FUN_00225bf0_set_state_and_animation(lead, 10, 1);
    }
    else if (mode == 1)
    {
      // FUN_00252d88: back to the idle state and animation.
      orphen::ported::entity::FUN_00225bf0_set_state_and_animation(lead, 0, 1);
    }
    trace_.recordPlayerLock(mode);
    return 0;
  }

  // 0xE1 (FUN_00265000): raise the save/menu mode. No operands at all. This is
  // what s01_e024's mask-0x2 floor panel reaches, at script offset 0x4ce.
  //
  // The opcode table calls this "boot_party_for_battle", and that name is wrong.
  // Confirmed against the game: the mask-0x2 quad at (-6.00, -11.25) is the
  // *save point* -- standing on it brings up the save dialog. DAT_00354d2c is a
  // mode selector (0 on a freshly loaded map, per the EE dump) that this raises
  // to 0x10, and FUN_002686a0 is the handoff into that mode.
  //
  // Clears event flag 0x8EE, raises the mode, puts the lead into state 10 /
  // animation 1, and walks the seven party slots at DAT_00343692 restaging every
  // member whose type is 0x37. The port has no party-slot table and no menu, so
  // the flag, the mode global and the lead's state are done for real and the
  // party walk is recorded as absent.
  std::uint32_t SceneCommandInterpreter::FUN_00265000_boot_party_for_battle()
  {
    if (halted_ || environment_.state == nullptr)
    {
      return 0;
    }

    environment_.state->FUN_002663a0_clearEventFlag(0x8EE);
    environment_.state->DAT_00354d2c_battleState = 0x10;
    if (environment_.entityPool != nullptr)
    {
      orphen::ported::entity::FUN_00225bf0_set_state_and_animation(
          environment_.entityPool->leadPlayer(), 10, 1);
    }
    trace_.recordBattleBoot();
    return 0;
  }

  // 0x85 / 0x87 (FUN_00260c20): raise a dialogue/message event. Two expression
  // operands -- an event id and a three-bit colour selector -- which the handler
  // expands to a packed RGB, each channel 0x00 or 0xFF, before calling
  // FUN_0025d1c0(buffer, eventId, rgb). 0x85 uses buffer 1, 0x87 buffer 0.
  //
  // This is the second half of s01_e024's mask-0x1 floor panel: 0x6D parks the
  // player, then this raises the message. FUN_0025d1c0 is the same primitive the
  // chest-opening player state 0xC calls, so it is the engine's general "put
  // something on screen and wait" entry.
  //
  // The port has no dialogue system, so the request is recorded with its
  // operands and execution continues. Both operands are consumed exactly as the
  // original consumes them, which is what makes continuing safe.
  std::uint32_t SceneCommandInterpreter::FUN_00260c20_dispatch_rgb_event()
  {
    const std::uint32_t fadeRate = FUN_0025c258_evaluate();
    const std::uint32_t colourSelector = FUN_0025c258_evaluate();
    if (halted_)
    {
      return 0;
    }

    const std::uint32_t red = (colourSelector & 1u) != 0 ? 0xFFu : 0u;
    const std::uint32_t green = (colourSelector & 2u) != 0 ? 0xFFu : 0u;
    const std::uint32_t blue = (colourSelector & 4u) != 0 ? 0xFFu : 0u;
    const std::uint32_t packedRgb = (green << 16) | (blue << 8) | red;

    const std::uint32_t bank = currentOpcode_ == 0x85 ? 1u : 0u;
    environment_.state->FUN_0025d1c0_arm_fade(bank, static_cast<std::uint16_t>(fadeRate), packedRgb);
    trace_.recordFadeArmed(bank, fadeRate, packedRgb);
    return 0;
  }

  // 0x86 (FUN_00260ca0): step the fullscreen fade and return whether it has
  // finished. A tail call into FUN_0025d238 with no operands. The scene's
  // transition branch spins on this until it reports done, so a port that
  // stubbed it would loop forever on the panel.
  std::uint32_t SceneCommandInterpreter::FUN_00260ca0_advance_fade()
  {
    if (halted_ || environment_.state == nullptr)
    {
      return 0;
    }
    return environment_.state->FUN_0025d238_step_fade(environment_.frameTicks);
  }

  // 0xBC (FUN_00263e30): increment a byte counter, capped at 99, and return
  // whether it still had room. Event scripts use it as a one-shot gate.
  std::uint32_t SceneCommandInterpreter::FUN_00263e30_increment_event_counter()
  {
    const std::uint32_t index = FUN_0025c258_evaluate();
    if (halted_ || index >= SceneScriptState::kEventCounterCount)
    {
      return 0;
    }
    std::uint8_t &counter = environment_.state->DAT_003437b8_eventCounters[index];
    const std::uint8_t previous = counter;
    if (previous < 99)
    {
      counter = static_cast<std::uint8_t>(previous + 1);
    }
    return previous < 99 ? 1u : 0u;
  }

  // 0xB9 / 0xBA (FUN_00263d10, FUN_00263d60): three expressions packed 0xRRGGBB
  // into one of two global colour registers, exactly like 0x96.
  void SceneCommandInterpreter::FUN_00263d10_set_global_color()
  {
    const std::uint32_t red = FUN_0025c258_evaluate();
    const std::uint32_t green = FUN_0025c258_evaluate();
    const std::uint32_t blue = FUN_0025c258_evaluate();
    const std::uint32_t packed = (red << 16) | (green << 8) | blue;
    if (currentOpcode_ == 0xB9)
    {
      environment_.state->uGpffffb704_color1 = packed;
    }
    else
    {
      environment_.state->uGpffffb708_color2 = packed;
    }
  }

  // 0xBB (FUN_00263db0): a near and far fade radius, on the same 100000 scale as
  // world coordinates. The original warns when the near radius is under 2.0.
  void SceneCommandInterpreter::FUN_00263db0_set_fade_radius_pair()
  {
    const float nearRadius =
        static_cast<float>(static_cast<std::int32_t>(FUN_0025c258_evaluate())) / kScriptCoordinateScale;
    const float farRadius =
        static_cast<float>(static_cast<std::int32_t>(FUN_0025c258_evaluate())) / kScriptCoordinateScale;
    environment_.state->fGpffffb70c_fadeNear = nearRadius;
    environment_.state->fGpffffb710_fadeFar = farRadius;
  }

  // 0xB8 (FUN_00263cb8): camera follow distance, on the same 100000 scale as
  // world coordinates. The original also derives fGpffffb6bc as distance - 5.
  // Recorded only: the ported field camera owns its own distance and taking a
  // script value into it without the rest of the script camera would be worse
  // than leaving it alone.
  void SceneCommandInterpreter::FUN_00263cb8_set_camera_distance()
  {
    const float distance =
        static_cast<float>(static_cast<std::int32_t>(FUN_0025c258_evaluate())) / kScriptCoordinateScale;
    environment_.state->DAT_0032538c_cameraDistance = distance;
  }

  // 0x58 (FUN_0025f0d8): select a pool slot by index.
  std::uint32_t SceneCommandInterpreter::FUN_0025f0d8_select_slot_by_index()
  {
    const std::uint32_t index = FUN_0025c258_evaluate();
    if (halted_)
    {
      return 0;
    }
    if (index < orphen::ported::entity::kEntitySlotCount)
    {
      currentEntity_ = index;
      return 1;
    }
    return 0;
  }

  // 0x5A (FUN_0025f150): select the first live pool object whose +0x4C record
  // index matches. The port does not carry the record index on the entity yet,
  // so this matches on slot instead and reports failure when nothing matches --
  // scripts branch on the return value, so a wrong "found" would be worse than
  // an honest "not found".
  std::uint32_t SceneCommandInterpreter::FUN_0025f150_select_by_record_index()
  {
    const std::uint32_t wanted = FUN_0025c258_evaluate();
    if (halted_ || environment_.entityPool == nullptr)
    {
      return 0;
    }

    // FUN_0025f150 scans the script slot range [10, 256) -- it starts at
    // &DAT_0058d120, which is the pool base plus 10 * 0x1D8, and runs 0xF6 slots
    // -- for the first active entity whose +0x98 matches. The operand is a
    // *placement record index*, not a pool slot: it is the value placeFromRecord
    // stores. The port previously read it as a slot index, which happened to
    // agree only because both count from the same place in simple scenes.
    const auto &pool = *environment_.entityPool;
    for (std::size_t slot = orphen::ported::entity::kFirstScriptSlot;
         slot < orphen::ported::entity::kEntitySlotCount;
         ++slot)
    {
      if (pool.status(slot) != orphen::ported::entity::SlotStatus::ScriptSpawned)
      {
        continue;
      }
      if (static_cast<std::uint32_t>(pool.slot(slot).placementRecordIndex98) != wanted)
      {
        continue;
      }
      currentEntity_ = slot;
      return 1;
    }
    return 0;
  }

  // 0x51 (FUN_0025eb48): one raw byte selects a group, then every object
  // placement record in the map whose +0x0D group byte matches is turned into an
  // entity at the record's position and angle.
  //
  // Group 3 spawns type 0x3A unconditionally. Every other group looks the
  // record's +0x0E id up in the 0x4E table and spawns the type stored there,
  // skipping entries whose type is the 0x55 sentinel.
  std::uint32_t SceneCommandInterpreter::FUN_0025eb48_set_pw_all()
  {
    const std::uint8_t group = readU8();
    if (halted_ || environment_.map == nullptr || environment_.entityPool == nullptr)
    {
      return 0;
    }

    const auto &placements = environment_.map->DAT_003556e8_objectPlacements;
    for (std::size_t recordIndex = 0; recordIndex < placements.size(); ++recordIndex)
    {
      const auto &record = placements[recordIndex];
      if (static_cast<std::uint8_t>(record.group) != group)
      {
        continue;
      }

      std::int32_t typeId = 0;
      if (group == 3)
      {
        typeId = kGroup3TypeId;
      }
      else
      {
        bool matched = false;
        const SceneScriptState &state = *environment_.state;
        for (std::size_t entry = 0; entry < state.DAT_0035504c_lookupCount; ++entry)
        {
          if (static_cast<std::int32_t>(state.DAT_00571d00_lookup[entry].value1) != record.id)
          {
            continue;
          }
          if (state.DAT_00571d00_lookup[entry].value3 == kNonSpawningTypeId)
          {
            break; // a marker, deliberately not spawned
          }
          typeId = static_cast<std::int32_t>(state.DAT_00571d00_lookup[entry].value3);
          matched = true;
          break;
        }
        if (!matched)
        {
          continue;
        }
      }

      SpawnRecord &spawnRecord = trace_.beginSpawn();
      spawnRecord.scriptOffset = streamOffset_;
      spawnRecord.typeId = typeId;

      const std::size_t slot =
          environment_.entityPool->FUN_00265e28_allocate_and_initialize(typeId, *environment_.descriptors);
      if (slot >= orphen::ported::entity::kEntitySlotCount)
      {
        continue; // pool full; the original returns a null pointer here too
      }
      currentEntity_ = slot;
      placeFromRecord(slot, recordIndex, record, spawnRecord);

      if (group == 3)
      {
        // The group-3 tail of FUN_0025eb48: the record's id byte and its param
        // byte biased into the event-flag space. Type 0x3A reads the latter
        // every frame to decide whether it is a closed or an opened chest.
        auto &entity = environment_.entityPool->slot(slot);
        entity.recordId130 = static_cast<std::int16_t>(record.id);
        entity.eventFlagId198 =
            static_cast<std::uint32_t>(static_cast<std::uint8_t>(record.param)) + kChestFlagBase;
      }
    }

    return 0;
  }

  // 0x52 (FUN_0025edc8): evaluate a type id and spawn it into a free script
  // slot. Type 0x55 is refused outright. Returns 1 when something was spawned,
  // which scripts branch on.
  std::uint32_t SceneCommandInterpreter::FUN_0025edc8_spawn_by_type()
  {
    const std::uint32_t typeId = FUN_0025c258_evaluate();
    if (halted_ || environment_.entityPool == nullptr)
    {
      return 0;
    }
    if (typeId == kNonSpawningTypeId)
    {
      return 0;
    }

    SpawnRecord &spawnRecord = trace_.beginSpawn();
    spawnRecord.scriptOffset = streamOffset_;
    spawnRecord.typeId = static_cast<std::int32_t>(typeId);

    const std::size_t slot = environment_.entityPool->FUN_00265e28_allocate_and_initialize(
        static_cast<std::int32_t>(typeId), *environment_.descriptors);
    if (slot >= orphen::ported::entity::kEntitySlotCount)
    {
      return 0;
    }

    currentEntity_ = slot;
    spawnRecord.slot = slot;
    spawnRecord.allocated = true;
    spawnRecord.descriptorResolved = environment_.entityPool->slot(slot).modelIndex >= 0;
    return 1;
  }

  // 0x50 (FUN_0025eaf0): like 0x52 but the slot comes from an explicit
  // selection expression and the type from a following raw byte.
  std::uint32_t SceneCommandInterpreter::FUN_0025eaf0_init_selected()
  {
    const std::uint32_t selector = FUN_0025c258_evaluate();
    const std::uint8_t typeId = readU8();
    if (halted_ || environment_.entityPool == nullptr)
    {
      return 0;
    }

    auto *entity = resolveEntity(selector);
    if (entity == nullptr)
    {
      return 0;
    }

    SpawnRecord &spawnRecord = trace_.beginSpawn();
    spawnRecord.scriptOffset = streamOffset_;
    spawnRecord.typeId = typeId;
    spawnRecord.slot = currentEntity_;
    spawnRecord.allocated = true;

    environment_.entityPool->FUN_00229c40_initialize(currentEntity_, typeId, *environment_.descriptors);
    spawnRecord.descriptorResolved = environment_.entityPool->slot(currentEntity_).modelIndex >= 0;
    return 1;
  }

  // 0x54 / 0x55 (FUN_0025eeb0): entity index then x, y, z, each divided by the
  // script coordinate scale. Index 0x100 means "the current entity". 0x55 also
  // resamples terrain into +0x4C.
  void SceneCommandInterpreter::FUN_0025eeb0_set_entity_position()
  {
    const bool sampleTerrain = (currentOpcode_ == 0x55);

    const std::uint32_t index = FUN_0025c258_evaluate();
    const float x = static_cast<float>(static_cast<std::int32_t>(FUN_0025c258_evaluate())) / kScriptCoordinateScale;
    const float y = static_cast<float>(static_cast<std::int32_t>(FUN_0025c258_evaluate())) / kScriptCoordinateScale;
    const float z = static_cast<float>(static_cast<std::int32_t>(FUN_0025c258_evaluate())) / kScriptCoordinateScale;
    if (halted_)
    {
      return;
    }

    auto *entity = resolveEntity(index);
    if (entity == nullptr)
    {
      return;
    }

    // FUN_002662e0 writes +0x20/+0x24/+0x28 and mirrors z into +0x4C.
    entity->positionX20 = x;
    entity->positionZ24 = y;
    entity->positionY28 = z;
    entity->groundHeight4c = z;

    bool grounded = false;
    if (sampleTerrain && environment_.terrainHeight)
    {
      const auto height = environment_.terrainHeight(x, y);
      if (height.has_value())
      {
        entity->groundHeight4c = *height;
        grounded = true;
      }
    }

    if (SpawnRecord *spawnRecord = trace_.lastSpawn();
        spawnRecord != nullptr && spawnRecord->allocated && spawnRecord->slot == currentEntity_)
    {
      spawnRecord->positioned = true;
      spawnRecord->grounded = grounded;
      spawnRecord->x = x;
      spawnRecord->y = y;
      spawnRecord->z = z;
    }
  }

  // 0x59 (FUN_0025f120): the selected pool slot index, or 0x100 when nothing is
  // selected.
  std::uint32_t SceneCommandInterpreter::FUN_0025f120_get_slot_index()
  {
    return currentEntity_ < orphen::ported::entity::kEntitySlotCount
               ? static_cast<std::uint32_t>(currentEntity_)
               : orphen::ported::entity::kCurrentEntityIndex;
  }

  // 0xAB (FUN_00263148): mode byte then x, y, z; teleports the lead player and,
  // in mode 0, detaches the camera first.
  void SceneCommandInterpreter::FUN_00263148_teleport_lead()
  {
    const std::uint8_t mode = readU8();
    const float x = static_cast<float>(static_cast<std::int32_t>(FUN_0025c258_evaluate())) / kScriptCoordinateScale;
    const float y = static_cast<float>(static_cast<std::int32_t>(FUN_0025c258_evaluate())) / kScriptCoordinateScale;
    const float z = static_cast<float>(static_cast<std::int32_t>(FUN_0025c258_evaluate())) / kScriptCoordinateScale;
    if (halted_)
    {
      return;
    }
    (void)mode;

    trace_.recordLeadTeleport(x, y, z);
    if (environment_.teleportLead)
    {
      environment_.teleportLead(x, y, z);
    }
  }

  std::uint32_t SceneCommandInterpreter::dispatchStandard(std::uint8_t opcode)
  {
    switch (opcode)
    {
    case 0x4D:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      FUN_0025e628_process_resource_ids();
      return 0;

    case 0x4E:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      FUN_0025e730_push_lookup_entry();
      return 0;

    case 0x36:
    case 0x38:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      return FUN_0025d768_read_work_or_flag();

    case 0x37:
    case 0x39:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      return FUN_0025d818_write_work_or_flag();

    case 0x3D:
    case 0x3E:
    case 0x3F:
    case 0x40:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      return FUN_0025e560_resource_flag();

    case 0x4F:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      FUN_0025e7c0_process_placements();
      return 0;

    case 0x50:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      return FUN_0025eaf0_init_selected();

    case 0x51:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      return FUN_0025eb48_set_pw_all();

    case 0x52:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      return FUN_0025edc8_spawn_by_type();

    case 0x54:
    case 0x55:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      FUN_0025eeb0_set_entity_position();
      return 0;

    case 0x58:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      return FUN_0025f0d8_select_slot_by_index();

    case 0x59:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      return FUN_0025f120_get_slot_index();

    case 0x5A:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      return FUN_0025f150_select_by_record_index();

    case 0x76:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      return FUN_00260318_read_object_register();

    case 0x77:
    case 0x78:
    case 0x79:
    case 0x7A:
    case 0x7B:
    case 0x7C:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      return FUN_00260360_modify_object_register();

    case 0xB8:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      FUN_00263cb8_set_camera_distance();
      return 0;

    case 0xB9:
    case 0xBA:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      FUN_00263d10_set_global_color();
      return 0;

    case 0xBB:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      FUN_00263db0_set_fade_radius_pair();
      return 0;

    case 0xBC:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      return FUN_00263e30_increment_event_counter();

    case 0x96:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      FUN_002618c0_set_global_rgb();
      return 0;

    case 0x97:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      FUN_00261910_set_vector_with_rgb();
      return 0;

    case 0x61:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      return FUN_0025f4b8_test_lead_flag_word();

    case 0x6D:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      return FUN_0025fd10_set_player_lock();

    case 0x70:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      return FUN_00260038_angle_to_lead();

    case 0x85:
    case 0x87:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      return FUN_00260c20_dispatch_rgb_event();

    case 0x86:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      return FUN_00260ca0_advance_fade();

    case 0xE1:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      return FUN_00265000_boot_party_for_battle();

    case 0x9D:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      return FUN_00261cb8_install_slot();

    case 0x9E:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      return FUN_00261d18_clear_slot();

    case 0x9F:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      return FUN_00261d88_slot_occupied();

    case 0xA0:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      return FUN_00261de0_find_free_slot();

    case 0xA8:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      return FUN_00262f38_install_lead_slot();

    case 0xAA:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      return FUN_00263118_clear_lead_slot();

    case 0xAB:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      FUN_00263148_teleport_lead();
      return 0;

    default:
      trace_.recordOpcode(opcode, streamOffset_ - 1, false);
      return haltUnimplemented(opcode);
    }
  }

  std::uint32_t SceneCommandInterpreter::dispatchExtended(std::uint8_t extension)
  {
    const std::uint16_t opcode = static_cast<std::uint16_t>(extension + 0x100);
    trace_.recordOpcode(opcode, streamOffset_ - 2, false);
    return haltUnimplemented(opcode);
  }

} // namespace orphen::ported::script

// Statement opcode handlers for the SCR VM. Split out of
// scene_command_interpreter.cpp only for file size; they are members of the same
// class because they all share the one stream pointer.
//
// Every handler is named for the original it came from. Opcodes with no
// implementation halt rather than fall through, because their operands would go
// unconsumed and everything after them would decode as nonsense.

#include "ported/script/scene_command_interpreter.h"

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
        static_cast<float>(record.angle) * kPlacementAngleStep + kPlacementAngleBias;

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
    for (const auto &record : placements)
    {
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
      placeFromRecord(slot, record, spawnRecord);
    }
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

    // FUN_0025d6c0 selects the object the register bank belongs to. Index 0x100
    // keeps whatever was already current.
    std::size_t bank = SceneScriptState::kObjectRegisterBanks - 1;
    if (selector != orphen::ported::entity::kCurrentEntityIndex)
    {
      resolveEntity(selector);
    }
    if (currentEntity_ < orphen::ported::entity::kEntitySlotCount)
    {
      bank = currentEntity_;
    }

    std::uint32_t &target = environment_.state->objectRegister(bank, registerIndex);
    switch (currentOpcode_)
    {
    case 0x77: target = operand; break;
    case 0x78: target = target & operand; break;
    case 0x79: target = target | operand; break;
    case 0x7A: target = target ^ operand; break;
    case 0x7B: target = static_cast<std::uint32_t>(static_cast<std::int32_t>(target) + static_cast<std::int32_t>(operand)); break;
    case 0x7C: target = static_cast<std::uint32_t>(static_cast<std::int32_t>(target) - static_cast<std::int32_t>(operand)); break;
    default: return 0;
    }
    return target;
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
    for (const auto &record : placements)
    {
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
      placeFromRecord(slot, record, spawnRecord);
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

    case 0x59:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      return FUN_0025f120_get_slot_index();

    case 0x77:
    case 0x78:
    case 0x79:
    case 0x7A:
    case 0x7B:
    case 0x7C:
      trace_.recordOpcode(opcode, streamOffset_ - 1, true);
      return FUN_00260360_modify_object_register();

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

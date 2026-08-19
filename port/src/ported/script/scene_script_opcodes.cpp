// Statement opcode handlers for the SCR VM. Split out of
// scene_command_interpreter.cpp only for file size; they are members of the same
// class because they all share the one stream pointer.
//
// Every handler is named for the original it came from. Opcodes with no
// implementation halt rather than fall through, because their operands would go
// unconsumed and everything after them would decode as nonsense.

#include "ported/script/scene_command_interpreter.h"

#include "ported/camera/original_camera_path.h"
#include "ported/entity/actor_frame_update.h"
#include "ported/entity/original_entity_sound.h"
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

    // fGpffff8d78 / fGpffff8d7c, both 1/81920. With the nominal 32-tick frame
    // that makes 0xF0's walk 0.01875 world units a frame and 0xF1's run 0.05 --
    // the same order as the player's own 0.045 run scalar.
    constexpr float kNpcPaceScale = 1.220703143189894e-05f;

    // DAT_003525f0 / DAT_003525f4, +/- half a degree: FUN_0023a320 calls a turn
    // finished once the remaining angle is inside this.
    constexpr float kTurnDeadzone = 0.008726644329726696f;

    // FUN_00216690 now lives in object_registers.* because the register writes
    // need it too. Both placement paths apply it and the port previously did not
    // -- invisible to a cos/sin, but it matters to the opcodes that read +0x5C
    // back.
    using orphen::ported::script::FUN_00216690_wrapAngle;
  } // namespace

  void SceneCommandInterpreter::noteOpcode(std::uint16_t opcode, OpcodeSupport support)
  {
    trace_.recordOpcode(opcode, streamOffset_ ? streamOffset_ - 1 : 0, support);
  }

  std::uint32_t SceneCommandInterpreter::consumeOnly(std::uint16_t opcode,
                                                     int expressionCount,
                                                     std::size_t inlineBytes)
  {
    for (int index = 0; index < expressionCount; ++index)
    {
      FUN_0025c258_evaluate();
    }
    for (std::size_t index = 0; index < inlineBytes; ++index)
    {
      readU8();
    }
    (void)opcode;
    return 0;
  }

  std::uint32_t SceneCommandInterpreter::haltUnimplemented(std::uint16_t opcode)
  {
    halted_ = true;
    haltedOnUnimplemented_ = true;
    haltOpcode_ = opcode;
    // The opcode byte has already been consumed by the dispatch loop.
    haltOffset_ = streamOffset_ ? streamOffset_ - 1 : 0;
    return 0;
  }

  bool SceneCommandInterpreter::decodePathWaypoints(
      std::uint32_t blobOffset, std::vector<orphen::ported::psm2::Vec3> &waypoints)
  {
    if (static_cast<std::size_t>(blobOffset) + 4 > blob_.size())
    {
      return false;
    }
    std::uint32_t count = 0;
    std::memcpy(&count, blob_.data() + blobOffset, sizeof(count));
    // FUN_00266a78 refuses more than 16 points, and a bad offset reads as a
    // huge count, so this is a validity test as much as a clamp.
    if (count == 0 || count > orphen::ported::camera::kMaxSplinePoints)
    {
      return false;
    }

    // FUN_0025d618 saves DAT_00355cd0, aims it just past the count, evaluates,
    // and restores it. Same dance, with the interpreter's own cursor.
    const std::uint32_t savedOffset = streamOffset_;
    streamOffset_ = blobOffset + 4;

    waypoints.clear();
    waypoints.reserve(count);
    bool ok = true;
    for (std::uint32_t index = 0; index < count && ok; ++index)
    {
      float component[3] = {0.0f, 0.0f, 0.0f};
      for (float &value : component)
      {
        const std::uint32_t raw = FUN_0025c258_evaluate();
        if (halted_)
        {
          ok = false;
          break;
        }
        // The evaluator already applies the 0x0F literal's *100. FUN_0025d618
        // divides by 100 and FUN_002443f8 by 1000; doing it in one step keeps
        // the integer truncation out, which the original only has because it
        // stores through an int array in between.
        value = static_cast<float>(static_cast<std::int32_t>(raw)) / kScriptCoordinateScale;
      }
      if (ok)
      {
        waypoints.push_back({component[0], component[1], component[2]});
      }
    }

    streamOffset_ = savedOffset;
    return ok;
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

  // 0x4D (FUN_0025e628): count byte, then that many raw dwords, each an entity
  // type id handed to FUN_002661f8 -> FUN_002661a8, which resolves the type's
  // model record and loads its model and texture. Negative ids are skipped by
  // the original's walker and are skipped here too.
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
        if (environment_.FUN_002661a8_preload_model)
        {
          environment_.FUN_002661a8_preload_model(static_cast<std::uint16_t>(narrowed));
        }
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
      // The record's own z is where this thing was authored to stand, so it is
      // the band to look in. FUN_0025e7c0 does not sample terrain at all -- it
      // just writes the record's z into +0x28 and +0x4C -- so this only exists
      // to tell the report whether the placement landed on anything.
      const auto height = environment_.terrainHeight(record.position.x, record.position.y,
                                                     record.position.z,
                                                     record.position.z + entity.height58);
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
    return static_cast<std::uint32_t>(environment_.state->findFreeObjectScriptSlot());
  }

  // 0xA1 (FUN_00261e30), 0xA2 (FUN_00261ea8), 0xA3 (FUN_00261f08): arm, disarm
  // and read one of FUN_0025ce30's four channels.
  //
  // All three take the channel as an expression and reduce it modulo 4 with the
  // MIPS signed-division idiom, which truncates toward zero; for the
  // non-negative values scenes actually use that is a plain `% 4`. 0xA1 then
  // reads an *inline* u32 -- the stream offset -- not an expression.
  std::uint32_t SceneCommandInterpreter::FUN_00261e30_arm_event_channel()
  {
    const std::uint32_t selector = FUN_0025c258_evaluate();
    if (halted_)
    {
      return 0;
    }
    const std::uint32_t streamOffset = FUN_0025c1d0_readStreamU32();
    if (halted_)
    {
      return 0;
    }

    const std::size_t channel = selector % SceneScriptState::kEventChannelCount;
    auto &state = *environment_.state;
    state.DAT_00571e40_eventChannels[channel].cursor = streamOffset;
    state.DAT_00571e40_eventChannels[channel].timer = 0;

    trace_.recordEventStreamArmed(ScriptTrace::EventStreamArmed{
        trace_.frame(), static_cast<std::uint8_t>(channel), streamOffset});
    return 0;
  }

  std::uint32_t SceneCommandInterpreter::FUN_00261ea8_clear_event_channel()
  {
    const std::uint32_t selector = FUN_0025c258_evaluate();
    if (halted_)
    {
      return 0;
    }
    const std::size_t channel = selector % SceneScriptState::kEventChannelCount;
    environment_.state->DAT_00571e40_eventChannels[channel].cursor = 0;
    return 0;
  }

  std::uint32_t SceneCommandInterpreter::FUN_00261f08_read_event_channel()
  {
    const std::uint32_t selector = FUN_0025c258_evaluate();
    if (halted_)
    {
      return 0;
    }
    const std::size_t channel = selector % SceneScriptState::kEventChannelCount;
    return environment_.state->DAT_00571e40_eventChannels[channel].consumed;
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

    // FUN_00216510(0x3439c8): normalise in place through VU0 macro mode, and
    // leave a zero vector alone rather than dividing by zero. This is not
    // cosmetic -- VU1 dots the vector against unit normals expecting a unit
    // length, and s01_e024's script writes (1, 0, -1), which the save state
    // shows in VU memory as (0.7071, 0, -0.7071).
    float *vector = environment_.state->DAT_003439c8_vector;
    const float lengthSquared =
        vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2];
    if (lengthSquared > 0.0f)
    {
      const float scale = 1.0f / std::sqrt(lengthSquared);
      vector[0] *= scale;
      vector[1] *= scale;
      vector[2] *= scale;
    }
  }

  // 0x9A (FUN_00261b80): eight expressions in stream order -- track index, the
  // three channels of the start colour, the three of the end colour, then the
  // duration in frames. The original packs each triple as
  // `c2 << 16 | c1 << 8 | c0` before handing it to FUN_0025d408, which splits
  // it straight back apart; the packing is preserved here only because the
  // channel *order* rides on it, and FUN_0025d590 indexes the bytes positionally.
  void SceneCommandInterpreter::FUN_00261b80_arm_fade_track()
  {
    const std::uint32_t track = FUN_0025c258_evaluate();
    std::uint32_t start[3]{};
    std::uint32_t end[3]{};
    for (std::uint32_t &channel : start)
    {
      channel = FUN_0025c258_evaluate() & 0xFFu;
    }
    for (std::uint32_t &channel : end)
    {
      channel = FUN_0025c258_evaluate() & 0xFFu;
    }
    const std::int32_t duration = static_cast<std::int32_t>(FUN_0025c258_evaluate());
    if (halted_)
    {
      return;
    }

    // The original reports ER_PARAM above 15 and then indexes anyway.
    environment_.state->DAT_00572078_fadeTracks.FUN_0025d408_arm(
        track,
        (start[2] << 16) | (start[1] << 8) | start[0],
        (end[2] << 16) | (end[1] << 8) | end[0],
        duration);

    trace_.noteFadeTrackArmed(track, duration);
  }

  // 0xBF / 0xC0 (FUN_00263f28): r, g, b, radius. The opcode picks the
  // allocator -- 0xC0 scans from slot 0, 0xBF from slot 3 -- and the shared
  // body then writes the colour and the radius, but *only* if a slot was free.
  // A full table returns -1 and drops the light silently, which is why the
  // return value is the script's handle for every later 0xC2..0xC7.
  std::int32_t SceneCommandInterpreter::FUN_00263f28_allocate_light()
  {
    // The opcode has to be captured before any operand is evaluated.
    const std::uint16_t allocateOpcode = currentOpcode_;
    const std::uint32_t red = FUN_0025c258_evaluate();
    const std::uint32_t green = FUN_0025c258_evaluate();
    const std::uint32_t blue = FUN_0025c258_evaluate();
    const std::int32_t rawRadius = static_cast<std::int32_t>(FUN_0025c258_evaluate());
    if (halted_)
    {
      return -1;
    }

    auto &table = environment_.state->DAT_00343888_lights;
    const std::int32_t slot = allocateOpcode == 0xBF ? table.FUN_00266008_allocateFromThree()
                                                     : table.FUN_00266050_allocateFromZero();
    if (slot < 0)
    {
      return -1;
    }

    auto &light = table.slot(static_cast<std::uint32_t>(slot));
    light.red = static_cast<std::uint8_t>(red);
    light.green = static_cast<std::uint8_t>(green);
    light.blue = static_cast<std::uint8_t>(blue);
    light.radius = static_cast<float>(rawRadius) / kScriptCoordinateScale;
    table.noteRadius(static_cast<std::uint32_t>(slot), light.radius);
    return slot;
  }

  // 0x36 / 0x38 (FUN_0025d768): read one word of the 128-entry work array, or
  // one byte of the flag array. The original range-checks and reports
  // ER_PARAM; the port clamps and returns zero instead of aborting the process.
  std::uint32_t SceneCommandInterpreter::FUN_0025d768_read_work_or_flag()
  {
    // Capture the opcode *before* evaluating any operand. An operand expression
    // can contain another statement opcode -- work reads (0x36) are the common
    // case -- and evaluating it overwrites currentOpcode_ / DAT_00355cd8. Every
    // original that branches on its own opcode saves it in its first instruction
    // for exactly this reason. Reading it afterwards made this handler fall
    // through its switch and silently do nothing.
    const std::uint16_t opcode = currentOpcode_;
    const std::uint32_t index = FUN_0025c258_evaluate();
    SceneScriptState &state = *environment_.state;

    if (opcode == 0x36)
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

  // 0x3D..0x40 (FUN_0025e560): the event-flag query / set / clear / toggle.
  //
  // **These are not resource queries.** The port read them that way -- always
  // answering "not loaded" and writing nothing -- which meant every gate in
  // every scene saw a cleared flag and no script could ever latch its own
  // progress. It is why s01_e012's opening stalled on record 2 of its cutscene
  // stream waiting for flag 3, and it would have blocked every later scene the
  // same way.
  //
  // The mode is the opcode byte itself, which FUN_0025e560 reads back off the
  // stream as a character:
  //
  //   0x3D '='  query only
  //   0x3E '>'  set        (FUN_002663a0)
  //   0x3F '?'  clear      (FUN_002663d8)
  //   0x40 '@'  toggle     (FUN_00266418)
  //
  // All four return the flag's value **as it was before the write**, so a script
  // can test and latch in one instruction.
  std::uint32_t SceneCommandInterpreter::FUN_0025e560_event_flag()
  {
    const std::uint16_t opcode = currentOpcode_;
    const std::uint32_t flagId = FUN_0025c258_evaluate();
    if (halted_)
    {
      return 0;
    }

    auto &state = *environment_.state;
    const bool before = state.FUN_00266368_eventFlag(flagId);

    switch (opcode)
    {
    case 0x3E:
      state.FUN_002663a0_setEventFlag(flagId);
      break;
    case 0x3F:
      state.FUN_002663d8_clearEventFlag(flagId);
      break;
    case 0x40:
      state.FUN_00266418_toggleEventFlag(flagId);
      break;
    default:
      break;
    }

    if (opcode != 0x3D)
    {
      const bool after = state.FUN_00266368_eventFlag(flagId);
      if (after != before)
      {
        trace_.recordEventFlagChange(ScriptTrace::EventFlagChange{
            trace_.frame(), flagId, streamOffset_, after});
      }
    }
    return before ? 1u : 0u;
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
    // Capture the opcode *before* evaluating any operand. An operand expression
    // can contain another statement opcode -- work reads (0x36) are the common
    // case -- and evaluating it overwrites currentOpcode_ / DAT_00355cd8. Every
    // original that branches on its own opcode saves it in its first instruction
    // for exactly this reason. Reading it afterwards made this handler fall
    // through its switch and silently do nothing.
    const std::uint16_t opcode = currentOpcode_;
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
    switch (opcode)
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
    trace_.recordObjectRegisterValue(
        registerIndex,
        currentEntity_ < orphen::ported::entity::kEntitySlotCount ? static_cast<std::int32_t>(currentEntity_) : -1,
        result);
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
        trace_.recordObjectRegisterValue(registerIndex, static_cast<std::int32_t>(currentEntity_), value);
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

    environment_.state->FUN_002663d8_clearEventFlag(0x8EE);
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
    // See FUN_00260360: the opcode has to be read before any operand is
    // evaluated, because an operand expression can contain another opcode.
    const std::uint16_t fadeOpcode = currentOpcode_;
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

    const std::uint32_t bank = fadeOpcode == 0x85 ? 1u : 0u;
    if (environment_.DAT_00571dc0_screenFade != nullptr)
    {
      environment_.DAT_00571dc0_screenFade->FUN_0025d1c0_arm(bank != 0,
                                                             static_cast<std::uint16_t>(fadeRate),
                                                             packedRgb);
    }
    trace_.recordFadeArmed(bank, fadeRate, packedRgb);
    return 0;
  }

  // 0x86 (FUN_00260ca0): step the fullscreen fade and return whether it has
  // finished. A tail call into FUN_0025d238 with no operands. The scene's
  // transition branch spins on this until it reports done, so a port that
  // stubbed it would loop forever on the panel.
  std::uint32_t SceneCommandInterpreter::FUN_00260ca0_advance_fade()
  {
    if (halted_ || environment_.DAT_00571dc0_screenFade == nullptr)
    {
      return 0;
    }
    return environment_.DAT_00571dc0_screenFade->FUN_0025d238_step_fade_out(environment_.frameTicks)
               ? 1u
               : 0u;
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
    // Capture the opcode *before* evaluating any operand. An operand expression
    // can contain another statement opcode -- work reads (0x36) are the common
    // case -- and evaluating it overwrites currentOpcode_ / DAT_00355cd8. Every
    // original that branches on its own opcode saves it in its first instruction
    // for exactly this reason. Reading it afterwards made this handler fall
    // through its switch and silently do nothing.
    const std::uint16_t opcode = currentOpcode_;
    const std::uint32_t red = FUN_0025c258_evaluate();
    const std::uint32_t green = FUN_0025c258_evaluate();
    const std::uint32_t blue = FUN_0025c258_evaluate();
    const std::uint32_t packed = (red << 16) | (green << 8) | blue;
    if (opcode == 0xB9)
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

  // 0x45 (FUN_0025dfc8): one expression into FUN_00217e18, which drops an
  // installed script camera. A non-zero argument restores the saved pose; zero
  // instead seeds DAT_0058c0ea with 0x1e, a countdown the port does not carry.
  //
  // Both branches are inside `if (cGpffffb6e1 != 0)`, so this does nothing at
  // all unless a script camera is actually installed -- which is why s01_e012's
  // per-frame entry can reach it on frame 2 without a camera ever having been
  // set up.
  std::uint32_t SceneCommandInterpreter::FUN_0025dfc8_release_camera()
  {
    const std::uint32_t restore = FUN_0025c258_evaluate();
    if (halted_)
    {
      return 0;
    }
    if (environment_.FUN_00217e18_release_camera)
    {
      environment_.FUN_00217e18_release_camera(restore != 0);
    }
    return 0;
  }

  // 0xB7 (FUN_00263c58): three expressions, then select and write two fields.
  //
  // The original evaluates all three into one 12-byte stack block *before* it
  // selects, and the selector is the first of them; FUN_0025d6c0's 0x100 case
  // restores the selection that was current on entry, which is what the saved
  // iGpffffb0d4 is for. The port's resolveEntity already has that rule.
  //
  // +0x130 is the party/party-slot short the chest cutscene's item path also
  // uses, and +0x198 is the same word opcode 0x51 fills with an event flag id
  // for a treasure chest. s01_e012's init writes both on five cast members.
  void SceneCommandInterpreter::FUN_00263c58_set_entity_short_and_word()
  {
    const std::uint32_t selector = FUN_0025c258_evaluate();
    const std::uint32_t shortValue = FUN_0025c258_evaluate();
    const std::uint32_t wordValue = FUN_0025c258_evaluate();
    if (halted_)
    {
      return;
    }

    orphen::ported::entity::OriginalEntity *entity = resolveEntity(selector);
    if (entity == nullptr)
    {
      return;
    }
    entity->recordId130 = static_cast<std::int16_t>(shortValue);
    entity->eventFlagId198 = wordValue;
  }

  // 0x149 (FUN_00265790): one expression, low byte to DAT_00355656.
  //
  // Nothing in the retail executable reads it back -- FUN_0022a418 zeroes it at
  // scene load and this is the only other writer -- so the store *is* the whole
  // behaviour. It is kept rather than skipped because the value is scene state a
  // later slice may find a consumer for, and because storing it costs nothing.
  std::uint32_t SceneCommandInterpreter::FUN_00265790_set_global_byte()
  {
    const std::uint32_t value = FUN_0025c258_evaluate();
    if (halted_)
    {
      return 0;
    }
    environment_.state->DAT_00355656_sceneByte = static_cast<std::uint8_t>(value);
    return 0;
  }

  // 0x63 (FUN_0025f5d8): attach one entity to another, and place it.
  //
  // The analyzed file calls this "tracking", which undersells it. The two
  // fields it writes are +0x192 and +0x194 -- the bone-attachment pair
  // FUN_0020cdc0 branches on, the same one the player's bandana rides. So this
  // is the script's way of parenting a prop to a character: a lantern to a
  // hand, a hat to a head.
  //
  // Seven expressions, in the original's stack order:
  //   0  tracker selector   -- who is attached *to* (0x100 keeps the selection)
  //   1  bone byte          -- lands at +0x194; negative means position-only
  //   2  target selector    -- who gets attached (0x100 means the current one)
  //   3  animation id       -- lands at +0xA0
  //   4,5,6  x, y, z        -- scaled by DAT_00352bd8, which is 100000.0
  //
  // The subtlety worth spelling out: `written` is the *target*, but +0x192 is
  // filled from FUN_0025f120 -- the index of whatever is selected at that
  // moment, which the branch above has just pointed at the tracker. So the
  // selector order reads backwards from the assignment order, and getting it
  // the wrong way round parents the character to the prop.
  void SceneCommandInterpreter::FUN_0025f5d8_attach_and_place_entity()
  {
    const std::size_t savedEntity = currentEntity_;

    const std::uint32_t trackerSelector = FUN_0025c258_evaluate();
    const std::uint32_t boneByte = FUN_0025c258_evaluate();
    const std::uint32_t targetSelector = FUN_0025c258_evaluate();
    const std::uint32_t animation = FUN_0025c258_evaluate();
    const float x = static_cast<float>(static_cast<std::int32_t>(FUN_0025c258_evaluate())) / kScriptCoordinateScale;
    const float y = static_cast<float>(static_cast<std::int32_t>(FUN_0025c258_evaluate())) / kScriptCoordinateScale;
    const float z = static_cast<float>(static_cast<std::int32_t>(FUN_0025c258_evaluate())) / kScriptCoordinateScale;
    if (halted_)
    {
      return;
    }

    constexpr std::uint32_t kKeepSelection = orphen::ported::entity::kCurrentEntityIndex;
    std::size_t writtenSlot = orphen::ported::entity::kEntitySlotCount;

    if (trackerSelector == kKeepSelection)
    {
      if (targetSelector == kKeepSelection)
      {
        // Both sides say "whatever is selected"; the original restores the
        // selection and returns without writing anything.
        currentEntity_ = savedEntity;
        return;
      }
      writtenSlot = targetSelector;
      currentEntity_ = savedEntity;
    }
    else
    {
      writtenSlot = (targetSelector == kKeepSelection) ? savedEntity : targetSelector;
      currentEntity_ = trackerSelector;
    }

    if (environment_.entityPool == nullptr ||
        writtenSlot >= orphen::ported::entity::kEntitySlotCount)
    {
      return;
    }
    auto &written = environment_.entityPool->slot(writtenSlot);

    written.animationA0 = static_cast<std::uint16_t>(animation);
    // FUN_0025f120 reads the selection the branch above just set.
    written.parentSlot192 = static_cast<std::int16_t>(
        currentEntity_ < orphen::ported::entity::kEntitySlotCount
            ? static_cast<std::int32_t>(currentEntity_)
            : static_cast<std::int32_t>(kKeepSelection));
    written.attachBone194 = static_cast<std::int8_t>(boneByte);

    // FUN_002662e0, the same writer opcodes 0x54/0x55 use.
    written.positionX20 = x;
    written.positionZ24 = y;
    written.positionY28 = z;
    written.groundHeight4c = z;
  }

  // 0x66 (FUN_0025f950): turn an entity into a script-driven NPC.
  //
  // One expression (the selector) and then an *inline* u32 id -- not an
  // expression, so the operand widths differ and the order matters.
  //
  // Type 0x38 is the shape the choreography opcodes (0xE9..0xF5) drive. The
  // conversion stashes the entity's real type at +0x1CE so the class lookup can
  // still find it, parks the id at +0x130, raises +0x02 bit 0x4000 -- the same
  // bit the chest cutscene's item entity needs to keep the type dispatcher off
  // it -- and clears the whole NPC scratch block so the first movement opcode
  // starts from a known state.
  //
  // The original treats an already-0x37 entity as fatal. The port reports it and
  // carries on: a hard stop here would take the whole scene down over one actor.
  void SceneCommandInterpreter::FUN_0025f950_convert_to_npc()
  {
    const std::size_t savedEntity = currentEntity_;
    const std::uint32_t selector = FUN_0025c258_evaluate();
    if (halted_)
    {
      return;
    }
    const std::uint32_t recordId = FUN_0025c1d0_readStreamU32();
    if (halted_)
    {
      return;
    }

    orphen::ported::entity::OriginalEntity *entity =
        (selector == orphen::ported::entity::kCurrentEntityIndex)
            ? (savedEntity < orphen::ported::entity::kEntitySlotCount
                   ? &environment_.entityPool->slot(savedEntity)
                   : nullptr)
            : resolveEntity(selector);
    if (entity == nullptr)
    {
      return;
    }
    if (selector == orphen::ported::entity::kCurrentEntityIndex)
    {
      currentEntity_ = savedEntity;
    }

    entity->descriptorFlags02 |= 0x4000u;
    entity->recordId130 = static_cast<std::int16_t>(recordId);
    if (entity->typeId00 != 0x38)
    {
      entity->originalType1ce = entity->typeId00;
    }

    entity->stepCounter1bc = 0;
    entity->lastMoveOpcode1be = 0;
    entity->attackChance1c0 = 0; // +0x1C0, shared with the enemy reading
    entity->alertState1c4 = 0;   // +0x1C4
    entity->repathTimer1c6 = 0;  // +0x1C6
    entity->enemyFlags1c8 = 0;   // +0x1C8
    entity->npcWord1ca = 0;
    entity->interactPulse1cc = 0;
    entity->typeId00 = 0x38;
  }

  // 0x44 (FUN_0025dd60, shared with 0x42): advance the scripted camera move.
  //
  // One expression, the duration in frames. Returns 1 once elapsed has reached
  // it and 0 while it is still running, so a script slot polls this every frame
  // and branches on the answer -- which is how a camera move "waits" in a VM
  // with no yield.
  //
  // The elapsed accumulator is in *ticks* and the comparison shifts it down by
  // five to frames, so `duration << 5` is the same units. Both branches hand
  // FUN_00218158 the frame count and the duration; the disassembly at 0x25dd78
  // shows a1 loaded from the duration slot and never clobbered, which is the
  // argument Ghidra drops.
  //
  // 0x42's own interpolator, FUN_00217f38, is a different curve type and is not
  // ported; that opcode still halts rather than silently sharing this one.
  std::uint32_t SceneCommandInterpreter::FUN_0025dd60_step_camera_path()
  {
    const std::uint32_t durationFrames = FUN_0025c258_evaluate();
    if (halted_)
    {
      return 0;
    }

    auto &state = *environment_.state;
    if (state.uGpffffbd78_pathElapsed >= (durationFrames << 5))
    {
      return 1;
    }

    if (environment_.FUN_00218158_step_camera_path)
    {
      environment_.FUN_00218158_step_camera_path(
          static_cast<int>(state.uGpffffbd78_pathElapsed >> 5), static_cast<int>(durationFrames));
    }
    state.uGpffffbd78_pathElapsed += environment_.frameTicks;
    return 0;
  }

  // FUN_002589c0: release the entity bound to a party slot.
  //
  // The dispatch table calls the two opcodes that reach this "speak_for_party_slot"
  // and "speak_and_flag_party_slot", and analyzed/ops/0xAD_speak_for_party_slot.c
  // names this function `entity_speak`. **It has nothing to do with speech.**
  // It turns a type 0x37 follower back into whatever it was before opcode 0xAC
  // bound it, drops the bone attachments it was holding, empties the slot and
  // clears the slot's event flag. It is a party member leaving the group.
  //
  // Not modelled here: FUN_00251db8 (the follower re-sort) and the two
  // FUN_0020d9c8 bone releases, which need the attachment side the port only
  // has for the bandana.
  void SceneCommandInterpreter::FUN_002589c0_release_party_slot(std::size_t slot)
  {
    auto &state = *environment_.state;
    if (slot >= SceneScriptState::kPartySlotCount)
    {
      return;
    }

    const std::uint16_t poolIndex = state.DAT_00343692_partySlots[slot];
    if (poolIndex != 0 && poolIndex < 0x100 && environment_.entityPool != nullptr)
    {
      auto &entity = environment_.entityPool->slot(poolIndex);
      if (entity.typeId00 == 0x37)
      {
        entity.state60 = 0;
        entity.animationA0 = 0;
        entity.typeId00 = entity.partyOriginalType1a0;
        entity.descriptorFlags02 &= static_cast<std::uint16_t>(0xFFFDu);
        entity.halfword04 &= static_cast<std::uint16_t>(0xDFEEu);
      }
    }

    state.DAT_00343692_partySlots[slot] = 0x100;
    state.FUN_002663d8_clearEventFlag(static_cast<std::uint32_t>(slot) + 0x501);
    if (slot == 1)
    {
      // Slot 1 carries a second flag and a second table entry. Both are cleared
      // together everywhere they are touched.
      state.FUN_002663d8_clearEventFlag(0x507);
    }
  }

  // 0xAC (FUN_002631f0): bind an entity into a party slot, or clear one.
  //
  // Four expressions; the first is the slot (0..6) and the fourth is the entity.
  // The whole thing is a no-op when the slot's own flag (0x501 + slot) is
  // already set, and it refuses to bind once three slots are taken -- the party
  // size cap lives in the flags, not in a counter.
  //
  // A negative entity clears the slot instead. Otherwise the entity's real type
  // is parked at +0x1A0 and it becomes a type 0x37 follower, which is why
  // effectiveTypeId() has to alias 0x37 the same way it aliases 0x38.
  //
  // Not modelled: the free-position scan that assigns +0x1C6 (which of three
  // formation spots the follower walks in), the +0x1A2 / +0x1C7 bookkeeping and
  // FUN_0023a518's per-slot parameter block. Those shape *where* a follower
  // stands, which the port has no follow behaviour to use yet.
  std::uint32_t SceneCommandInterpreter::FUN_002631f0_bind_party_slot()
  {
    const std::size_t savedEntity = currentEntity_;
    const std::uint32_t slot = FUN_0025c258_evaluate();
    FUN_0025c258_evaluate();
    FUN_0025c258_evaluate();
    const std::int32_t entitySelector = static_cast<std::int32_t>(FUN_0025c258_evaluate());
    if (halted_ || environment_.entityPool == nullptr)
    {
      return 0;
    }
    auto &state = *environment_.state;
    if (slot >= SceneScriptState::kPartySlotCount)
    {
      return 0;
    }

    const std::uint32_t slotFlag = static_cast<std::uint32_t>(slot) + 0x501;
    if (state.FUN_00266368_eventFlag(slotFlag))
    {
      // Already bound. The original only tidies the "released" sentinel.
      if (state.DAT_00343692_partySlots[slot] == 0x100)
      {
        state.DAT_00343692_partySlots[slot] = 0xFFFF;
      }
      return 0;
    }

    // Skipping index 6 is the original's own exception, so the seventh slot
    // does not count against the cap.
    int bound = 0;
    for (std::size_t index = 0; index < SceneScriptState::kPartySlotCount; ++index)
    {
      if (index != 6 && state.FUN_00266368_eventFlag(static_cast<std::uint32_t>(index) + 0x501))
      {
        ++bound;
      }
    }
    if (bound >= 3)
    {
      return 0;
    }

    if (entitySelector < 0)
    {
      state.FUN_002663d8_clearEventFlag(slotFlag);
      state.DAT_00343692_partySlots[slot] = 0xFFFF;
      currentEntity_ = savedEntity;
      return 0;
    }

    auto *entity = resolveEntity(static_cast<std::uint32_t>(entitySelector));
    if (entity == nullptr)
    {
      return 0;
    }

    entity->partyOriginalType1a0 =
        (entity->typeId00 == 0x38) ? entity->originalType1ce : entity->typeId00;
    entity->typeId00 = 0x37;
    entity->descriptorFlags02 = static_cast<std::uint16_t>((entity->descriptorFlags02 & 0xBFFFu) | 0x1002u);
    entity->halfword04 = static_cast<std::uint16_t>((entity->halfword04 & 0xFFEEu) | 0x2000u);
    entity->state60 = 0;

    state.DAT_00343692_partySlots[slot] = static_cast<std::uint16_t>(currentEntity_);
    state.FUN_002663a0_setEventFlag(slotFlag);
    return 0;
  }

  // 0xAD (FUN_00263498) and 0xAE (FUN_00263518): release a party slot. 0xAE
  // takes a second expression and, when it is non-zero, also parks the released
  // entity in state 1.
  std::uint32_t SceneCommandInterpreter::FUN_00263498_release_party_slot()
  {
    const bool withState = (currentOpcode_ == 0xAE);

    const std::uint32_t slot = FUN_0025c258_evaluate();
    std::uint32_t setState = 0;
    if (withState)
    {
      setState = FUN_0025c258_evaluate();
    }
    if (halted_)
    {
      return 0;
    }
    if (slot >= SceneScriptState::kPartySlotCount)
    {
      // The original treats this as fatal. Reporting and carrying on keeps one
      // bad actor from taking the scene down.
      return 0;
    }

    const std::uint16_t poolIndex = environment_.state->DAT_00343692_partySlots[slot];
    FUN_002589c0_release_party_slot(slot);

    if (withState && setState != 0 && poolIndex != 0 && poolIndex < 0x100 &&
        environment_.entityPool != nullptr)
    {
      environment_.entityPool->slot(poolIndex).state60 = 1;
    }
    return 0;
  }

  // 0x140 / 0x141 (FUN_00260578): spawn a prop and hang it off a character.
  //
  // This is how a cutscene puts something in someone's hand. It allocates an
  // entity, parents it to the selected character's bone, and **writes the new
  // pool index into script work memory** -- that last part is the reason this
  // has to be modelled rather than consumed. The scene reads those work slots
  // back later to destroy the props again (s01_e012's handoff runs 0x5C over
  // work[10..12]); leaving them zero would send the destroy at slot 0, the lead
  // player.
  //
  // Operand counts differ between the two: 0x141 reads the bone index as its
  // second expression, 0x140 instead looks up bone role 6 on the parent.
  //
  //   selector, [bone], hideCount, typeId, workSlot
  //
  // The trailing FUN_0020dc38 loop hides `hideCount` of the parent's own bones
  // from the bone the prop took over, which is the visual half of the swap. It
  // is not decoration: this is the mechanism behind every talking head in the
  // game. s01_e012 runs it four times -- Orphen's head onto his bone 32 with
  // five bones hidden (32..36, confirmed against eeMemory.bin's entity +0x168),
  // a second layer onto the head's own bone 1, and one each for two party
  // members -- and the replacement head is what carries the mouth and eye
  // animation. Skip the hide and the character's original head draws inside the
  // new one; skip the attach and the new one sits at the world origin.
  std::uint32_t SceneCommandInterpreter::FUN_00260578_spawn_attached_prop()
  {
    const bool boneFromStream = (currentOpcode_ == 0x141);
    const std::size_t savedEntity = currentEntity_;

    const std::uint32_t selector = FUN_0025c258_evaluate();
    std::uint32_t bone = 0;
    if (boneFromStream)
    {
      bone = FUN_0025c258_evaluate();
    }
    const std::uint32_t hideCount = FUN_0025c258_evaluate();
    const std::uint32_t typeId = FUN_0025c258_evaluate();
    const std::uint32_t workSlot = FUN_0025c258_evaluate();
    if (halted_ || environment_.entityPool == nullptr || environment_.descriptors == nullptr)
    {
      return 0;
    }

    // FUN_0025d6c0: pick the parent, falling back to the current selection.
    if (selector != orphen::ported::entity::kCurrentEntityIndex)
    {
      if (selector >= orphen::ported::entity::kEntitySlotCount)
      {
        return 0;
      }
      currentEntity_ = selector;
    }
    else
    {
      currentEntity_ = savedEntity;
    }
    if (currentEntity_ >= orphen::ported::entity::kEntitySlotCount)
    {
      return 0;
    }
    const std::size_t parentSlot = currentEntity_;

    // FUN_0020dd78(DAT_00355044, 6). The original does this after the allocation
    // succeeds, but it only reads the parent, which nothing between here and
    // there touches.
    if (!boneFromStream && environment_.FUN_0020dd78_bone_for_role)
    {
      bone = static_cast<std::uint32_t>(environment_.FUN_0020dd78_bone_for_role(parentSlot, 6));
    }

    SpawnRecord &spawnRecord = trace_.beginSpawn();
    spawnRecord.scriptOffset = streamOffset_;
    spawnRecord.typeId = static_cast<std::int32_t>(static_cast<std::uint16_t>(typeId));

    const std::size_t slot = environment_.entityPool->FUN_00265e28_allocate_and_initialize(
        static_cast<std::int32_t>(static_cast<std::uint16_t>(typeId)), *environment_.descriptors);
    if (slot >= orphen::ported::entity::kEntitySlotCount)
    {
      return 0;
    }
    spawnRecord.slot = slot;
    spawnRecord.allocated = true;
    spawnRecord.descriptorResolved = environment_.entityPool->slot(slot).modelIndex >= 0;

    auto &prop = environment_.entityPool->slot(slot);
    prop.parentSlot192 = static_cast<std::int16_t>(parentSlot);
    prop.halfword04 = 0x19;
    prop.attachBone194 = static_cast<std::int8_t>(bone);
    prop.halfword08 = 0;

    if (workSlot < SceneScriptState::kWorkWordCount)
    {
      environment_.state->DAT_00355060_work[workSlot] = static_cast<std::uint32_t>(slot);
    }

    // The FUN_0020dc38 loop. The original decrements before it tests, so a
    // hideCount of N hides exactly N bones starting at the attach bone, and a
    // hideCount of 0 hides none.
    if (environment_.FUN_0020dc38_hide_bones && hideCount > 0)
    {
      environment_.FUN_0020dc38_hide_bones(parentSlot, static_cast<int>(bone),
                                           static_cast<int>(hideCount));
    }
    return 0;
  }

  // ---- 0xE9..0xF5, the choreography family ---------------------------------
  //
  // This is how a cutscene walks a character across a room. The VM has no
  // yield, so none of these can block; instead each one **advances the focus
  // entity's +0x1BC step counter when it completes** and returns true on that
  // frame. A cutscene body then reads +0x1BC (opcode 0xED) and switches on it,
  // so the counter is the program counter of the choreography and the body is
  // re-entered from the top every frame until the whole sequence is done.
  //
  // +0x1BE holds which of these ran last. A move that finds a different opcode
  // there knows it has just been entered and does its one-time setup -- stamp
  // the animation, latch the duration, compute the target angle -- and a move
  // that finds its own opcode is continuing. On completion it is cleared, which
  // re-arms the setup for whatever runs next.
  //
  // None of it needs the non-player physics step: FUN_002658c0 computes its own
  // per-frame displacement and writes straight into +0x20 / +0x24.

  // 0xEB (FUN_00265840): one expression, the pool index to focus.
  std::uint32_t SceneCommandInterpreter::FUN_00265840_set_focus()
  {
    const std::uint32_t index = FUN_0025c258_evaluate();
    if (halted_)
    {
      return 0;
    }
    environment_.state->puGpffffb0d8_focusEntity = index;
    return 0;
  }

  orphen::ported::entity::OriginalEntity *SceneCommandInterpreter::focusEntity()
  {
    const std::size_t slot = environment_.state->puGpffffb0d8_focusEntity;
    if (environment_.entityPool == nullptr || slot >= orphen::ported::entity::kEntitySlotCount)
    {
      return nullptr;
    }
    return &environment_.entityPool->slot(slot);
  }

  // 0xEC (FUN_00265880): one expression, written to the step counter and
  // handed back. A body uses it to jump its own sequence to a given step.
  std::uint32_t SceneCommandInterpreter::FUN_00265880_set_step()
  {
    const std::uint32_t value = FUN_0025c258_evaluate();
    if (halted_)
    {
      return 0;
    }
    if (auto *focus = focusEntity(); focus != nullptr)
    {
      focus->stepCounter1bc = static_cast<std::uint8_t>(value);
    }
    return value;
  }

  // 0xED (FUN_002658b0): no operands, returns the step counter.
  std::uint32_t SceneCommandInterpreter::FUN_002658b0_get_step()
  {
    const auto *focus = const_cast<SceneCommandInterpreter *>(this)->focusEntity();
    return focus != nullptr ? focus->stepCounter1bc : 0u;
  }

  // 0xE9 (FUN_00265d88): read and clear +0x1CC, the "was interacted with" pulse.
  std::uint32_t SceneCommandInterpreter::FUN_00265d88_consume_interact()
  {
    auto *focus = focusEntity();
    if (focus == nullptr)
    {
      return 0;
    }
    const std::uint32_t value = focus->interactPulse1cc;
    focus->interactPulse1cc = 0;
    return value;
  }

  // 0xEA (FUN_00265818): the focus entity's pool index. The original recovers it
  // by dividing the pointer difference by 0x1D8; the port already has it.
  std::uint32_t SceneCommandInterpreter::FUN_00265818_focus_index()
  {
    return static_cast<std::uint32_t>(environment_.state->puGpffffb0d8_focusEntity);
  }

  // 0xEE / 0xEF / 0xF0 / 0xF1 (FUN_002658c0): walk the focus toward a target XY.
  //
  // The operand list differs per opcode and the differences are the whole point:
  //
  //   0xEE  animation, speed, x, y
  //   0xEF  speed, x, y            -- and it does *not* touch the facing, so it
  //                                   is the "keep looking where you are" walk
  //   0xF0  x, y                   -- animation 4, walk pace
  //   0xF1  x, y                   -- run pace, and the animation depends on the
  //                                   character class behind a type 0x38 role
  std::uint32_t SceneCommandInterpreter::FUN_002658c0_step_toward_xy()
  {
    const std::uint16_t opcode = currentOpcode_;

    std::int32_t animation = 0;
    float step = 0.0f;
    const float ticks = static_cast<float>(environment_.frameTicks);

    if (opcode == 0xF0 || opcode == 0xF1)
    {
      if (opcode == 0xF0)
      {
        animation = 4;
        step = ticks * 48.0f * kNpcPaceScale;
      }
      else
      {
        const auto *focus = focusEntity();
        // FUN_002298d0's type 0x38 alias: the class comes from the real type.
        // The test is on the *class* the lookup returns, not on the type id --
        // `lVar5 = FUN_002298d0(sVar7); iStack_80 = 0xe; if (6 < lVar5) ...`.
        const std::int16_t type = focus != nullptr ? focus->effectiveTypeId() : 0;
        const int characterClass = orphen::ported::entity::FUN_002298d0_character_class(type);
        animation = (characterClass > 6) ? 8 : 14;
        step = ticks * 128.0f * kNpcPaceScale;
      }
    }
    else
    {
      if (opcode == 0xEE)
      {
        animation = static_cast<std::int32_t>(FUN_0025c258_evaluate());
      }
      const std::int32_t speed = static_cast<std::int32_t>(FUN_0025c258_evaluate());
      step = (static_cast<float>(speed) / kScriptCoordinateScale) * ticks * 0.03125f;
    }
    const std::int32_t targetX = static_cast<std::int32_t>(FUN_0025c258_evaluate());
    const std::int32_t targetY = static_cast<std::int32_t>(FUN_0025c258_evaluate());
    if (halted_)
    {
      return 0;
    }

    auto *focus = focusEntity();
    if (focus == nullptr)
    {
      return 0;
    }

    if (focus->lastMoveOpcode1be != static_cast<std::int16_t>(opcode))
    {
      focus->lastMoveOpcode1be = static_cast<std::int16_t>(opcode);
      if (opcode != 0xEF && animation >= 0 && focus->animationA0 != animation)
      {
        focus->animationA0 = static_cast<std::uint16_t>(animation);
      }
    }

    const float deltaX = static_cast<float>(targetX) / kScriptCoordinateScale - focus->positionX20;
    const float deltaY = static_cast<float>(targetY) / kScriptCoordinateScale - focus->positionZ24;
    const float facing = std::atan2(deltaY, deltaX);
    const float distance = std::sqrt(deltaX * deltaX + deltaY * deltaY);

    std::uint32_t arrived = 0;
    float move = step;
    if (distance < step)
    {
      arrived = 1;
      ++focus->stepCounter1bc;
      focus->lastMoveOpcode1be = 0;
      move = distance;
    }
    else if (opcode != 0xEF)
    {
      focus->facingRadians5c = facing;
    }

    // **A request, not a teleport.** The original accumulates into +0x30/+0x34
    // -- `psGpffffb0d8[0x18]` and `[0x1a]` over a short pointer -- and leaves it
    // to the physics pass to spend. Writing +0x20/+0x24 here instead skips
    // collision and the ground follow entirely, and it moves the actor a frame
    // early relative to everything that reads its position.
    //
    // It also only works at all because FUN_00239ce0 leaves these alone: the
    // scene tick runs before the actor loop, so the request has to survive it.
    focus->desiredDeltaX30 += move * std::cos(facing);
    focus->desiredDeltaZ34 += move * std::sin(facing);
    return arrived;
  }

  // 0xF2 (FUN_00265b90): hold an animation for a frame count. Two expressions,
  // the animation and the duration; the duration counts down in +0x1C0.
  std::uint32_t SceneCommandInterpreter::FUN_00265b90_anim_for_duration()
  {
    const std::uint16_t opcode = currentOpcode_;
    const std::int32_t animation = static_cast<std::int32_t>(FUN_0025c258_evaluate());
    const std::uint32_t duration = FUN_0025c258_evaluate();
    if (halted_)
    {
      return 0;
    }
    auto *focus = focusEntity();
    if (focus == nullptr)
    {
      return 1;
    }

    if (focus->lastMoveOpcode1be != static_cast<std::int16_t>(opcode))
    {
      focus->lastMoveOpcode1be = static_cast<std::int16_t>(opcode);
      focus->attackChance1c0 = static_cast<std::int16_t>(duration); // +0x1C0
      if (animation >= 0 && focus->animationA0 != animation)
      {
        focus->animationA0 = static_cast<std::uint16_t>(animation);
      }
    }

    const bool finished = static_cast<std::int16_t>(focus->attackChance1c0 - 1) < 1;
    focus->attackChance1c0 = static_cast<std::int16_t>(focus->attackChance1c0 - 1);
    if (finished)
    {
      ++focus->stepCounter1bc;
      focus->lastMoveOpcode1be = 0;
    }
    return finished ? 1u : 0u;
  }

  // 0xF3 (FUN_00265c30): play an animation and wait for it to finish. One
  // expression. The completion test is +0x06 bit 0, which FUN_00225c90 raises
  // once the timeline sits on its last keyframe.
  std::uint32_t SceneCommandInterpreter::FUN_00265c30_anim_until_done()
  {
    const std::uint16_t opcode = currentOpcode_;
    const std::int32_t animation = static_cast<std::int32_t>(FUN_0025c258_evaluate());
    if (halted_)
    {
      return 0;
    }
    auto *focus = focusEntity();
    if (focus == nullptr)
    {
      return 1;
    }

    if (focus->lastMoveOpcode1be != static_cast<std::int16_t>(opcode))
    {
      focus->lastMoveOpcode1be = static_cast<std::int16_t>(opcode);
      if (animation >= 0 && focus->animationA0 != animation)
      {
        focus->animationA0 = static_cast<std::uint16_t>(animation);
      }
    }

    const bool finished = (focus->flags06 & 1u) != 0;
    if (finished)
    {
      ++focus->stepCounter1bc;
      focus->lastMoveOpcode1be = 0;
    }
    return finished ? 1u : 0u;
  }

  // 0xF4 (FUN_00265cb0): turn the focus toward an angle at a rate. Two
  // expressions, both scaled by 100000. FUN_0023a320 clamps the wrapped
  // difference to the rate and returns zero inside a half-degree deadzone,
  // which is what "arrived" means here.
  std::uint32_t SceneCommandInterpreter::FUN_00265cb0_rotate_toward()
  {
    const std::uint16_t opcode = currentOpcode_;
    const std::int32_t targetRaw = static_cast<std::int32_t>(FUN_0025c258_evaluate());
    const std::int32_t rateRaw = static_cast<std::int32_t>(FUN_0025c258_evaluate());
    if (halted_)
    {
      return 0;
    }
    auto *focus = focusEntity();
    if (focus == nullptr)
    {
      return 1;
    }

    if (focus->lastMoveOpcode1be != static_cast<std::int16_t>(opcode))
    {
      focus->lastMoveOpcode1be = static_cast<std::int16_t>(opcode);
      focus->npcTurnRate1c4 = static_cast<float>(rateRaw) / kScriptCoordinateScale;
      focus->npcTargetAngle1c8 =
          FUN_00216690_wrapAngle(static_cast<float>(targetRaw) / kScriptCoordinateScale);
    }

    // FUN_0023a320(current, target, rate).
    const float difference = FUN_00216690_wrapAngle(focus->npcTargetAngle1c8 - focus->facingRadians5c);
    float stepAngle = 0.0f;
    if (difference > kTurnDeadzone)
    {
      stepAngle = std::min(difference, focus->npcTurnRate1c4);
    }
    else if (difference < -kTurnDeadzone)
    {
      stepAngle = std::max(difference, -focus->npcTurnRate1c4);
    }

    if (stepAngle == 0.0f)
    {
      ++focus->stepCounter1bc;
      focus->lastMoveOpcode1be = 0;
      return 1;
    }
    focus->facingRadians5c += stepAngle;
    return 0;
  }

  // 0xF5 (FUN_00265d98): a type 0x38 in state 0x38 is put back into whatever
  // state +0x1CE remembers. The port's +0x1CE holds the original *type*, which
  // is the same halfword the original reads here.
  std::uint32_t SceneCommandInterpreter::FUN_00265d98_promote_state()
  {
    auto *focus = focusEntity();
    if (focus == nullptr)
    {
      return 0;
    }
    if (focus->state60 == 0x38)
    {
      focus->state60 = static_cast<std::uint16_t>(focus->originalType1ce);
    }
    return 0;
  }

  // 0x53 (FUN_0025ee08): selector and axis, returning one component of an
  // entity's position scaled back up. 0/1/2 pick +0x20, +0x24, +0x28; anything
  // else returns zero rather than reading past the triple.
  std::uint32_t SceneCommandInterpreter::FUN_0025ee08_read_position()
  {
    const std::uint32_t selector = FUN_0025c258_evaluate();
    const std::uint32_t axis = FUN_0025c258_evaluate();
    if (halted_)
    {
      return 0;
    }
    if (selector != orphen::ported::entity::kCurrentEntityIndex)
    {
      resolveEntity(selector);
    }
    if (currentEntity_ >= orphen::ported::entity::kEntitySlotCount || environment_.entityPool == nullptr)
    {
      return 0;
    }
    const auto &entity = environment_.entityPool->slot(currentEntity_);
    float component = 0.0f;
    switch (axis)
    {
    case 0: component = entity.positionX20; break;
    case 1: component = entity.positionZ24; break;
    case 2: component = entity.positionY28; break;
    default: return 0;
    }
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(component * kScriptCoordinateScale));
  }

  // 0x74 / 0x75 (FUN_002601f8): the distance and the angle between two
  // entities. Two expressions, each either a pool index or 0x100 for "the one
  // already selected", and the selection rules are not symmetric:
  //
  //   A < 0x100   -- `to` is pool[A]; `from` is pool[B], or the current
  //                  selection when B is 0x100
  //   A == 0x100  -- `to` is the current selection (which A itself sets), and B
  //                  must be a real index
  //
  // 0x74 measures |B - A| and 0x75 is atan2 of the same difference, both over
  // +0x20 / +0x24 only -- these are flat, ground-plane queries, not 3D.
  //
  // Ghidra prints FUN_0023a4e8 with one argument; it takes two, the second
  // already being in the register. Reading it as one-argument would measure a
  // distance from the origin.
  std::uint32_t SceneCommandInterpreter::FUN_002601f8_entity_distance_or_angle()
  {
    const std::uint16_t opcode = currentOpcode_;
    const std::uint32_t selectorA = FUN_0025c258_evaluate();
    const std::uint32_t selectorB = FUN_0025c258_evaluate();
    if (halted_ || environment_.entityPool == nullptr)
    {
      return 0;
    }

    constexpr std::uint32_t kKeepSelection = orphen::ported::entity::kCurrentEntityIndex;
    const orphen::ported::entity::OriginalEntity *from = nullptr;
    const orphen::ported::entity::OriginalEntity *to = nullptr;

    if (selectorA < orphen::ported::entity::kEntitySlotCount)
    {
      to = &environment_.entityPool->slot(selectorA);
      if (selectorB < orphen::ported::entity::kEntitySlotCount)
      {
        from = &environment_.entityPool->slot(selectorB);
      }
      else
      {
        from = resolveEntity(selectorB);
      }
    }
    else
    {
      to = resolveEntity(selectorA);
      if (selectorB >= orphen::ported::entity::kEntitySlotCount)
      {
        // The original calls this fatal. Answering zero keeps the scene alive.
        return 0;
      }
      from = &environment_.entityPool->slot(selectorB);
    }
    if (from == nullptr || to == nullptr)
    {
      return 0;
    }

    const float deltaX = from->positionX20 - to->positionX20;
    const float deltaZ = from->positionZ24 - to->positionZ24;
    const float value = (opcode == 0x74) ? std::sqrt(deltaX * deltaX + deltaZ * deltaZ)
                                         : std::atan2(deltaZ, deltaX);
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(value * kScriptCoordinateScale));
  }

  // 0x41 (FUN_0025db20): the two-curve camera path -- eye and look-at, no
  // roll/zoom. Sibling of 0x43.
  //
  // Two expressions (the two list offsets) and then one **inline byte** of
  // flags, read after them. The flags splice the camera's live pose into the
  // curve so a cut can start or end where the camera already is:
  //
  //   bit 0  prepend the current eye, and -- only when a script camera is
  //          already installed (cGpffffb6e1 == 0x23) -- the current look-at
  //   bit 1  append the current eye
  //
  // The asymmetry is the original's: bit 0 can add a look-at point, bit 1 never
  // does, so the two curves can legitimately end up different lengths.
  std::uint32_t SceneCommandInterpreter::FUN_0025db20_build_camera_path_pair()
  {
    const std::uint32_t eyeListOffset = FUN_0025c258_evaluate();
    const std::uint32_t lookAtListOffset = FUN_0025c258_evaluate();
    if (halted_)
    {
      return 0;
    }
    const std::uint8_t flags = readU8();
    if (halted_)
    {
      return 0;
    }

    const std::uint32_t resumeOffset = streamOffset_;
    std::vector<orphen::ported::psm2::Vec3> eyePoints;
    std::vector<orphen::ported::psm2::Vec3> lookAtPoints;

    const auto readPoints = [&](std::uint32_t listOffset,
                                std::vector<orphen::ported::psm2::Vec3> &out) -> bool {
      if (listOffset + 4 > blob_.size())
      {
        return false;
      }
      const std::uint32_t pointCount = static_cast<std::uint32_t>(blob_[listOffset]) |
                                       (static_cast<std::uint32_t>(blob_[listOffset + 1]) << 8) |
                                       (static_cast<std::uint32_t>(blob_[listOffset + 2]) << 16) |
                                       (static_cast<std::uint32_t>(blob_[listOffset + 3]) << 24);
      if (pointCount > orphen::ported::camera::kMaxSplinePoints)
      {
        return false;
      }
      streamOffset_ = listOffset + 4;
      for (std::uint32_t index = 0; index < pointCount; ++index)
      {
        float component[3] = {0.0f, 0.0f, 0.0f};
        for (float &axis : component)
        {
          axis = static_cast<float>(static_cast<std::int32_t>(FUN_0025c258_evaluate())) /
                 kScriptCoordinateScale;
          if (halted_)
          {
            return false;
          }
        }
        out.push_back({component[0], component[1], component[2]});
      }
      return true;
    };

    ScriptEnvironment::CameraPose pose{};
    if (environment_.cameraPose)
    {
      pose = environment_.cameraPose();
    }

    bool ok = true;
    if ((flags & 1u) != 0)
    {
      eyePoints.push_back(pose.eye);
      if (pose.subMode == 0x23)
      {
        lookAtPoints.push_back(pose.lookAt);
      }
    }
    ok = readPoints(eyeListOffset, eyePoints) && ok;
    ok = readPoints(lookAtListOffset, lookAtPoints) && ok;
    streamOffset_ = resumeOffset;
    if (!ok || eyePoints.empty() || lookAtPoints.empty())
    {
      return 0;
    }
    if ((flags & 2u) != 0)
    {
      eyePoints.push_back(pose.eye);
    }

    if (environment_.FUN_00217e18_release_camera)
    {
      environment_.FUN_00217e18_release_camera(false);
    }
    if (environment_.FUN_00217fe8_set_camera_path)
    {
      // No roll/zoom curve: FUN_00217e88 builds only the two.
      environment_.FUN_00217fe8_set_camera_path(eyePoints, {}, {}, lookAtPoints);
    }
    environment_.state->uGpffffbd78_pathElapsed = 0;
    return 0;
  }

  // 0x43 (FUN_0025de08): build and install the scripted camera path.
  //
  // This is the cutscene camera. Three expressions, each a blob offset of a
  // point list; every list is a u32 count followed by that many entries, and the
  // entries are *expressions*, read by pointing the interpreter's own stream at
  // the list and putting it back afterwards. So the opcode consumes exactly
  // three expressions from the instruction stream no matter how long the curves
  // are.
  //
  //   list 0  eye        3 values per point
  //   list 1  roll/zoom  2 values per point, interleaved
  //   list 2  look-at    3 values per point
  //
  // FUN_00217fe8 then takes the *eye* count as the roll/zoom curve's count too,
  // so the two are authored in lockstep, and applies FUN_00218230 -- log2(2x) --
  // to the second of each pair. Everything downstream of that already exists:
  // this is the same spline pair the treasure chest's camera swing installs.
  std::uint32_t SceneCommandInterpreter::FUN_0025de08_build_camera_path()
  {
    const std::uint32_t eyeListOffset = FUN_0025c258_evaluate();
    const std::uint32_t rollZoomListOffset = FUN_0025c258_evaluate();
    const std::uint32_t lookAtListOffset = FUN_0025c258_evaluate();
    if (halted_)
    {
      return 0;
    }

    // Read `count` scaled values from a list elsewhere in the blob, leaving the
    // instruction stream where it was.
    const std::uint32_t resumeOffset = streamOffset_;
    const auto readList = [&](std::uint32_t listOffset, int componentsPerPoint,
                              std::vector<float> &out) -> bool {
      out.clear();
      if (listOffset + 4 > blob_.size())
      {
        return false;
      }
      const std::uint32_t pointCount = static_cast<std::uint32_t>(blob_[listOffset]) |
                                       (static_cast<std::uint32_t>(blob_[listOffset + 1]) << 8) |
                                       (static_cast<std::uint32_t>(blob_[listOffset + 2]) << 16) |
                                       (static_cast<std::uint32_t>(blob_[listOffset + 3]) << 24);
      if (pointCount == 0 || pointCount > orphen::ported::camera::kMaxSplinePoints)
      {
        return false;
      }
      const std::uint32_t valueCount = pointCount * static_cast<std::uint32_t>(componentsPerPoint);
      streamOffset_ = listOffset + 4;
      out.reserve(valueCount);
      for (std::uint32_t index = 0; index < valueCount; ++index)
      {
        const std::int32_t raw = static_cast<std::int32_t>(FUN_0025c258_evaluate());
        if (halted_)
        {
          return false;
        }
        out.push_back(static_cast<float>(raw) / kScriptCoordinateScale);
      }
      return true;
    };

    std::vector<float> eyeValues;
    std::vector<float> rollZoomValues;
    std::vector<float> lookAtValues;
    const bool ok = readList(eyeListOffset, 3, eyeValues) &&
                    readList(rollZoomListOffset, 2, rollZoomValues) &&
                    readList(lookAtListOffset, 3, lookAtValues);
    streamOffset_ = resumeOffset;
    if (!ok)
    {
      return 0;
    }

    std::vector<orphen::ported::psm2::Vec3> eyePoints;
    for (std::size_t index = 0; index + 2 < eyeValues.size(); index += 3)
    {
      eyePoints.push_back({eyeValues[index], eyeValues[index + 1], eyeValues[index + 2]});
    }
    std::vector<orphen::ported::psm2::Vec3> lookAtPoints;
    for (std::size_t index = 0; index + 2 < lookAtValues.size(); index += 3)
    {
      lookAtPoints.push_back({lookAtValues[index], lookAtValues[index + 1], lookAtValues[index + 2]});
    }
    std::vector<float> rollValues;
    std::vector<float> zoomScales;
    for (std::size_t index = 0; index + 1 < rollZoomValues.size(); index += 2)
    {
      rollValues.push_back(rollZoomValues[index]);
      zoomScales.push_back(rollZoomValues[index + 1]);
    }

    // FUN_00217e18(0) drops whatever camera was there before the path goes in.
    if (environment_.FUN_00217e18_release_camera)
    {
      environment_.FUN_00217e18_release_camera(false);
    }
    if (environment_.FUN_00217fe8_set_camera_path)
    {
      environment_.FUN_00217fe8_set_camera_path(eyePoints, rollValues, zoomScales, lookAtPoints);
    }
    environment_.state->uGpffffbd78_pathElapsed = 0;
    return 0;
  }

  // 0x90 (FUN_00261100): arm a parameter ramp. Four expressions -- index,
  // target, step, current -- and note the stack order is not the argument
  // order: the *second* expression is the target, the *fourth* the starting
  // value. The step is stored negative when the ramp has to count down.
  std::uint32_t SceneCommandInterpreter::FUN_00261100_arm_ramp()
  {
    const std::uint32_t index = FUN_0025c258_evaluate();
    const std::int32_t target = static_cast<std::int32_t>(FUN_0025c258_evaluate());
    const std::int32_t step = static_cast<std::int32_t>(FUN_0025c258_evaluate());
    const std::int32_t current = static_cast<std::int32_t>(FUN_0025c258_evaluate());
    if (halted_ || index >= SceneScriptState::kParameterRampCount)
    {
      return 0;
    }

    auto &ramp = environment_.state->DAT_00571de0_ramps[index];
    ramp.target = static_cast<float>(target) / kScriptCoordinateScale;
    ramp.current = static_cast<float>(current) / kScriptCoordinateScale;
    ramp.step = static_cast<float>(step) / kScriptCoordinateScale;
    if (ramp.target < ramp.current)
    {
      ramp.step = -ramp.step;
    }
    return 0;
  }

  // 0x91 (FUN_002611b8): advance a ramp by one frame and report whether it has
  // arrived. **The return value is the whole point** -- a script polls this
  // every frame and only moves on when it reads 1, so answering it wrongly
  // either hangs the cutscene or skips the beat.
  std::uint32_t SceneCommandInterpreter::FUN_002611b8_step_ramp()
  {
    const std::uint32_t index = FUN_0025c258_evaluate();
    if (halted_ || index >= SceneScriptState::kParameterRampCount)
    {
      return 1;
    }

    auto &ramp = environment_.state->DAT_00571de0_ramps[index];
    if (ramp.current == ramp.target)
    {
      return 1;
    }

    ramp.current += ramp.step * static_cast<float>(environment_.frameTicks) * 0.03125f;
    const bool arrived = ramp.step > 0.0f ? ramp.current >= ramp.target : ramp.current <= ramp.target;
    if (arrived)
    {
      ramp.current = ramp.target;
    }
    return 0;
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
    if (sampleTerrain && environment_.FUN_00227070_sample_ground)
    {
      // FUN_00227070 is handed the entity, and reads +0x28 / +0x58 off it for
      // the scan band -- plus +0x54 and +0x04, which decide whether it samples
      // one point or the four corners of the collision radius. Those are the
      // position just written and the entity's own body, so the sample is the
      // floor *under this entity*, not the nearest surface to sea level.
      const auto height = environment_.FUN_00227070_sample_ground(
          x, y, z, entity->height58, entity->radius54, entity->halfword04,
          entity->rejectTerrainMask74);
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
      noteOpcode(opcode, OpcodeSupport::Modelled);
      FUN_0025e628_process_resource_ids();
      return 0;

    case 0x4E:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      FUN_0025e730_push_lookup_entry();
      return 0;

    case 0x36:
    case 0x38:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_0025d768_read_work_or_flag();

    case 0x37:
    case 0x39:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_0025d818_write_work_or_flag();

    case 0x3D:
    case 0x3E:
    case 0x3F:
    case 0x40:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_0025e560_event_flag();

    case 0x4F:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      FUN_0025e7c0_process_placements();
      return 0;

    case 0x50:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_0025eaf0_init_selected();

    case 0x51:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_0025eb48_set_pw_all();

    case 0x52:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_0025edc8_spawn_by_type();

    case 0x54:
    case 0x55:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      FUN_0025eeb0_set_entity_position();
      return 0;

    case 0x58:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_0025f0d8_select_slot_by_index();

    case 0x59:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_0025f120_get_slot_index();

    case 0x5A:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_0025f150_select_by_record_index();

    case 0x76:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_00260318_read_object_register();

    case 0x77:
    case 0x78:
    case 0x79:
    case 0x7A:
    case 0x7B:
    case 0x7C:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_00260360_modify_object_register();

    case 0xB8:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      FUN_00263cb8_set_camera_distance();
      return 0;

    case 0xB9:
    case 0xBA:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      FUN_00263d10_set_global_color();
      return 0;

    case 0xBB:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      FUN_00263db0_set_fade_radius_pair();
      return 0;

    case 0xBC:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_00263e30_increment_event_counter();

    case 0x96:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      FUN_002618c0_set_global_rgb();
      return 0;

    case 0x97:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      FUN_00261910_set_vector_with_rgb();
      return 0;

    // 0x33 (FUN_0025d6f8): inline dialogue. The operand layout is
    // [rel32][text...] -- the dword at the opcode is the jump that skips the
    // text -- so the stream can never desync here no matter what the text
    // contains. FUN_00237b38 gets `streamOffset_ + 4`, the text itself.
    case 0x33:
    {
      noteOpcode(opcode, OpcodeSupport::Modelled);
      if (environment_.FUN_00237b38_start_dialogue)
      {
        environment_.FUN_00237b38_start_dialogue(streamOffset_ + 4);
      }
      FUN_0025c220_relativeJump();
      return 0;
    }

    // 0x34 (FUN_0025d728) and 0x35 (FUN_0025d748): the two predicates a script
    // polls while it waits for a line. No operands.
    case 0x34:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return environment_.FUN_00237c60_dialogue_busy && environment_.FUN_00237c60_dialogue_busy() ? 1u : 0u;

    case 0x35:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return environment_.FUN_00237c70_dialogue_complete && environment_.FUN_00237c70_dialogue_complete() ? 1u : 0u;

    // 0x92 (FUN_00261258): one expression, the ramp index. Submits that ramp's
    // current value scaled back up by DAT_00352c30 (100000.0), which is how a
    // script reads a ramp it armed with 0x90.
    case 0x92:
    {
      noteOpcode(opcode, OpcodeSupport::Modelled);
      const std::uint32_t index = FUN_0025c258_evaluate();
      if (halted_ || index >= SceneScriptState::kParameterRampCount)
      {
        return 0;
      }
      const float current = environment_.state->DAT_00571de0_ramps[index].current;
      return static_cast<std::uint32_t>(static_cast<std::int32_t>(current * kScriptCoordinateScale));
    }

    // 0x95 (FUN_00261890): no operands. |FUN_00216868()|, the engine RNG.
    //
    // The port's generator is a stand-in -- FUN_00216868 is a lagged xor over a
    // 0x209-entry ring whose seed table is filled at boot and is not ported --
    // so the *sequence* differs from the original even though the shape does
    // not. It is seeded once and stepped deterministically, which is what
    // --frames reproducibility needs.
    case 0x95:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return environment_.FUN_00216868_random ? environment_.FUN_00216868_random() : 0u;

    // 0x6C (FUN_0025fca0): two expressions. The first becomes the camera's
    // zoom through FUN_00218230's log2(2x); the second is the rate a later
    // 0x6B would ramp at. This is the opcode that pushes in for a close-up.
    case 0x6C:
    {
      noteOpcode(opcode, OpcodeSupport::Modelled);
      const std::int32_t scaled = static_cast<std::int32_t>(FUN_0025c258_evaluate());
      FUN_0025c258_evaluate(); // the ramp rate, fGpffffb6ec
      if (halted_)
      {
        return 0;
      }
      if (environment_.FUN_00218230_set_zoom)
      {
        environment_.FUN_00218230_set_zoom(
            orphen::ported::camera::FUN_00218230_zoomLog2(static_cast<float>(scaled) / kScriptCoordinateScale));
      }
      return 0;
    }

    // 0xBE (FUN_00263ee0): two expressions -- an index into PTR_FUN_0031e730
    // and one argument -- then an indirect call. A second function table the
    // port has not read out of the executable.
    case 0xBE:
      noteOpcode(opcode, OpcodeSupport::OperandsOnly);
      return consumeOnly(opcode, 2);

    case 0x90:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_00261100_arm_ramp();

    case 0x91:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_002611b8_step_ramp();

    // 0x5C (FUN_0025f238): one expression. Below 0x100 it names a pool slot,
    // otherwise the current selection is destroyed. s01_e012's handoff runs
    // this over work[10..12] to clear away the props it spawned.
    case 0x5C:
    {
      noteOpcode(opcode, OpcodeSupport::Modelled);
      const std::uint32_t index = FUN_0025c258_evaluate();
      if (halted_ || environment_.entityPool == nullptr)
      {
        return 0;
      }
      const std::size_t slot = index < orphen::ported::entity::kEntitySlotCount
                                   ? static_cast<std::size_t>(index)
                                   : currentEntity_;
      if (slot < orphen::ported::entity::kEntitySlotCount)
      {
        environment_.entityPool->releaseSlot(slot);
      }
      return 0;
    }

    // 0x4C (FUN_0025e520): one expression into DAT_0035564c, a projection
    // distance the port's renderer does not read.
    case 0x4C:
      noteOpcode(opcode, OpcodeSupport::OperandsOnly);
      return consumeOnly(opcode, 1);

    case 0x53:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_0025ee08_read_position();

    // 0x5E / 0x5F (FUN_0025f380): magnitude and angle in, one cartesian
    // component out -- cos for 0x5E, sin for 0x5F. (FUN_00305130 is cosf and
    // FUN_00305218 is sinf, not the other way round; two files under analyzed/
    // have that pair backwards.)
    case 0x5E:
    case 0x5F:
    {
      const bool useCos = (opcode == 0x5E);
      noteOpcode(opcode, OpcodeSupport::Modelled);
      const std::int32_t magnitudeRaw = static_cast<std::int32_t>(FUN_0025c258_evaluate());
      const std::int32_t angleRaw = static_cast<std::int32_t>(FUN_0025c258_evaluate());
      if (halted_)
      {
        return 0;
      }
      const float magnitude = static_cast<float>(magnitudeRaw) / kScriptCoordinateScale;
      const float angle = FUN_00216690_wrapAngle(static_cast<float>(angleRaw) / kScriptCoordinateScale);
      const float value = magnitude * (useCos ? std::cos(angle) : std::sin(angle));
      return static_cast<std::uint32_t>(static_cast<std::int32_t>(value * kScriptCoordinateScale));
    }

    // 0x60 (FUN_0025f428): three coordinates into FUN_00227798, which is the
    // **terrain height query**, not the 3D magnitude the dispatch table claims.
    // A cutscene uses it to put a mark on the floor.
    case 0x60:
    {
      noteOpcode(opcode, OpcodeSupport::Modelled);
      const std::int32_t rawX = static_cast<std::int32_t>(FUN_0025c258_evaluate());
      const std::int32_t rawY = static_cast<std::int32_t>(FUN_0025c258_evaluate());
      const std::int32_t rawZ = static_cast<std::int32_t>(FUN_0025c258_evaluate());
      if (halted_)
      {
        return 0;
      }
      float height = 0.0f;
      if (environment_.terrainHeight)
      {
        // FUN_00227798 writes its z into *both* the feet (+0x2C) and the head
        // (+0x30) of the scan workspace, so the answer is the highest surface
        // at or below that z -- the script picks which storey it means by what
        // it passes here. Dropping the z is what put this scene's cast on the
        // upper deck.
        const float probeZ = static_cast<float>(rawZ) / kScriptCoordinateScale;
        const auto hit = environment_.terrainHeight(static_cast<float>(rawX) / kScriptCoordinateScale,
                                                    static_cast<float>(rawY) / kScriptCoordinateScale,
                                                    probeZ, probeZ);
        if (hit.has_value())
        {
          height = *hit;
        }
      }
      return static_cast<std::uint32_t>(static_cast<std::int32_t>(height * kScriptCoordinateScale));
    }

    case 0xE9:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_00265d88_consume_interact();

    case 0xEA:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_00265818_focus_index();

    case 0xEB:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_00265840_set_focus();

    case 0xEC:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_00265880_set_step();

    case 0xED:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_002658b0_get_step();

    case 0xEE:
    case 0xEF:
    case 0xF0:
    case 0xF1:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_002658c0_step_toward_xy();

    case 0xF2:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_00265b90_anim_for_duration();

    case 0xF3:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_00265c30_anim_until_done();

    case 0xF4:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_00265cb0_rotate_toward();

    case 0xF5:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_00265d98_promote_state();

    // 0x7D / 0x7E (FUN_00260738): entity index, then an **inline byte** (the
    // channel, 0..2), then the value -- interleaved, so consumeOnly's shape
    // does not fit. Writes a timed parameter track at +0x3C or +0x48; the port
    // has no consumer for those.
    //
    // The arity came out of the disassembly at 0x260754..0x260770, not the
    // decompile: `src/FUN_00260738.c` is hand-annotated and its statements have
    // been reordered, and analyzed/update_entity_timed_parameter.c reads it as
    // three expressions. Consuming three desyncs the stream -- which is how
    // this was found, as an overrun at 0x6099 nine thousand frames in.
    case 0x7D:
    case 0x7E:
    {
      noteOpcode(opcode, OpcodeSupport::OperandsOnly);
      FUN_0025c258_evaluate();
      readU8();
      FUN_0025c258_evaluate();
      return 0;
    }

    case 0x74:
    case 0x75:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_002601f8_entity_distance_or_angle();

    case 0x41:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_0025db20_build_camera_path_pair();

    case 0x43:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_0025de08_build_camera_path();

    case 0x44:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_0025dd60_step_camera_path();

    // 0x88 (FUN_00260cc0): no operands. Steps fade bank 0 and returns whether it
    // has bottomed out, which is the completion test s01_e012's handoff polls.
    case 0x88:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return environment_.DAT_00571dc0_screenFade != nullptr &&
                     environment_.DAT_00571dc0_screenFade->FUN_0025d2f8_step_fade_in(
                         environment_.frameTicks)
                 ? 1u
                 : 0u;

    case 0x45:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_0025dfc8_release_camera();

    case 0x61:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_0025f4b8_test_lead_flag_word();

    case 0x63:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      FUN_0025f5d8_attach_and_place_entity();
      return 0;

    case 0x66:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      FUN_0025f950_convert_to_npc();
      return 0;

    case 0xB7:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      FUN_00263c58_set_entity_short_and_word();
      return 0;

    case 0x6D:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_0025fd10_set_player_lock();

    case 0x70:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_00260038_angle_to_lead();

    // 0x6E (FUN_0025fe98): the angle from (x2, y2) to (x1, y1). Four
    // expressions in stream order x1, y1, x2, y2, and the subtraction runs
    // first-minus-second, so this points *from* the later pair to the earlier
    // one. FUN_00305408 is atan2f(y, x) -- the same order FUN_002658c0 calls it
    // with -- and the result is wrapped before being scaled back.
    case 0x6E:
    {
      noteOpcode(opcode, OpcodeSupport::Modelled);
      const std::int32_t firstX = static_cast<std::int32_t>(FUN_0025c258_evaluate());
      const std::int32_t firstY = static_cast<std::int32_t>(FUN_0025c258_evaluate());
      const std::int32_t secondX = static_cast<std::int32_t>(FUN_0025c258_evaluate());
      const std::int32_t secondY = static_cast<std::int32_t>(FUN_0025c258_evaluate());
      if (halted_)
      {
        return 0;
      }
      const float deltaY = (static_cast<float>(firstY) - static_cast<float>(secondY)) / kScriptCoordinateScale;
      const float deltaX = (static_cast<float>(firstX) - static_cast<float>(secondX)) / kScriptCoordinateScale;
      const float angle = FUN_00216690_wrapAngle(std::atan2(deltaY, deltaX));
      return static_cast<std::uint32_t>(static_cast<std::int32_t>(angle * kScriptCoordinateScale));
    }

    // 0x72 (FUN_002600c8): ease an angle toward another, one frame's worth.
    // Three expressions -- from, to, rate -- and the result is the *step* to
    // apply, not the new angle: FUN_0023a320 returns the capped, dead-zoned
    // difference. The rate is per-second-ish, scaled by the frame's tick count
    // exactly the way the walk opcodes scale their pace.
    case 0x72:
    {
      noteOpcode(opcode, OpcodeSupport::Modelled);
      const std::int32_t fromRaw = static_cast<std::int32_t>(FUN_0025c258_evaluate());
      const std::int32_t toRaw = static_cast<std::int32_t>(FUN_0025c258_evaluate());
      const std::int32_t rateRaw = static_cast<std::int32_t>(FUN_0025c258_evaluate());
      if (halted_)
      {
        return 0;
      }
      const float from = FUN_00216690_wrapAngle(static_cast<float>(fromRaw) / kScriptCoordinateScale);
      const float to = FUN_00216690_wrapAngle(static_cast<float>(toRaw) / kScriptCoordinateScale);
      const float rate = (static_cast<float>(rateRaw) / kScriptCoordinateScale) * 0.03125f *
                         static_cast<float>(environment_.frameTicks);
      // FUN_0023a320, inline: the actor loop has the same four lines but keeps
      // them in its own translation unit. DAT_003525f0 is the half-degree dead
      // zone that stops a turn rather than letting it jitter around the target.
      const float difference = FUN_00216690_wrapAngle(to - from);
      float step = 0.0f;
      if (difference > kTurnDeadzone)
      {
        step = std::min(difference, rate);
      }
      else if (difference < -kTurnDeadzone)
      {
        step = std::max(difference, -rate);
      }
      return static_cast<std::uint32_t>(static_cast<std::int32_t>(step * kScriptCoordinateScale));
    }

    // 0x73 (FUN_00260188): the shortest signed angle from a to b.
    // FUN_002166e8 is `FUN_00216690(b - a)` and nothing more.
    case 0x73:
    {
      noteOpcode(opcode, OpcodeSupport::Modelled);
      const std::int32_t fromRaw = static_cast<std::int32_t>(FUN_0025c258_evaluate());
      const std::int32_t toRaw = static_cast<std::int32_t>(FUN_0025c258_evaluate());
      if (halted_)
      {
        return 0;
      }
      const float delta = FUN_00216690_wrapAngle(static_cast<float>(toRaw) / kScriptCoordinateScale -
                                                 static_cast<float>(fromRaw) / kScriptCoordinateScale);
      return static_cast<std::uint32_t>(static_cast<std::int32_t>(delta * kScriptCoordinateScale));
    }

    // 0x94 (FUN_002612e0): two expressions into FUN_0022dcf0, audio
    // positioning. Same story as 0xDE.
    case 0x94:
      noteOpcode(opcode, OpcodeSupport::OperandsOnly);
      return consumeOnly(opcode, 2);

    // 0xDE (FUN_00264f50): two expressions -- a channel id and a level -- into
    // FUN_0023bbd8, which sets audio channel state. The mixer does not model
    // per-channel state, so the operands are consumed and the effect is not.
    case 0xDE:
      noteOpcode(opcode, OpcodeSupport::OperandsOnly);
      return consumeOnly(opcode, 2);

    case 0x85:
    case 0x87:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_00260c20_dispatch_rgb_event();

    case 0x86:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_00260ca0_advance_fade();

    case 0xE1:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_00265000_boot_party_for_battle();

    case 0x9D:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_00261cb8_install_slot();

    case 0x9E:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_00261d18_clear_slot();

    case 0x9F:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_00261d88_slot_occupied();

    case 0xA0:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_00261de0_find_free_slot();

    case 0xA1:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_00261e30_arm_event_channel();

    case 0xA2:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_00261ea8_clear_event_channel();

    case 0xA3:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_00261f08_read_event_channel();

    case 0xAC:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_002631f0_bind_party_slot();

    case 0xAD:
    case 0xAE:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_00263498_release_party_slot();

    case 0xA8:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_00262f38_install_lead_slot();

    case 0xAA:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return FUN_00263118_clear_lead_slot();

    case 0xAB:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      FUN_00263148_teleport_lead();
      return 0;

    // ---- operands consumed, effect not modelled ----------------------------
    //
    // Each arity below is read out of the matching src/FUN_*.c, never guessed.
    // The scene keeps running past these and --scr-report names every one.

    // 0x9A (FUN_00261b80): eight expressions -- a track index, two colour
    // triples and a duration -- into FUN_0025d408.
    case 0x9A:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      FUN_00261b80_arm_fade_track();
      return 0;

    // 0xA4 / 0xA6 (FUN_00261f60): one expression, then one *inline* byte.
    //
    // The dispatch table files call these "audio_submit". They are not audio.
    // FUN_0022dbc8 and FUN_0022dc68 walk the map's primitive array --
    // DAT_00355688 records of 0x78 bytes at DAT_003556b0, the same ones
    // FUN_00209140 culls and FUN_002262c0 collides against -- and for every
    // record whose +0x04 terrain word intersects the mask, they flip a bit:
    // 0xA4 ORs 0x20 into the parallel 0x80-byte GS packet's +0x70, and 0xA6
    // sets or clears 0x800 in the record's own word 0. That is a script turning
    // map geometry on and off.
    //
    // Worth modelling once the report says a scene depends on it; the port's
    // visibility pass already owns both arrays. Consumed rather than modelled
    // for now because the animatic is about who moves where, and because the
    // operand widths are unambiguous either way.
    // 0xA4 / 0xA6 (FUN_00261f60): a group mask and an inline on/off byte. Not
    // audio, whatever the analyzed filename says -- FUN_0022dbc8 and
    // FUN_0022dc68 walk the map's primitive tables and flip one bit each.
    // Together they open and close a door: 0xA4 the geometry, 0xA6 the
    // collision. See ScriptEnvironment for the bits.
    case 0xA4:
    case 0xA6:
    {
      noteOpcode(opcode, OpcodeSupport::Modelled);
      const std::uint32_t groupMask = FUN_0025c258_evaluate();
      if (halted_)
      {
        return 0;
      }
      const std::uint8_t enableByte = readU8();
      if (opcode == 0xA4)
      {
        if (environment_.FUN_0022dbc8_show_map_primitives)
        {
          environment_.FUN_0022dbc8_show_map_primitives(groupMask, enableByte != 0);
        }
      }
      else if (environment_.FUN_0022dc68_enable_map_terrain)
      {
        environment_.FUN_0022dc68_enable_map_terrain(groupMask, enableByte != 0);
      }
      return 0;
    }

    // 0x9B (FUN_00261c38): one expression into FUN_0025d480. Ghidra types the
    // handler `void`, but it ends on the call and never touches `v0`, so
    // FUN_0025d480's "finished" reaches the script -- which branches on it.
    case 0x9B:
    {
      noteOpcode(opcode, OpcodeSupport::Modelled);
      const std::uint32_t track = FUN_0025c258_evaluate();
      if (halted_)
      {
        return 0;
      }
      return environment_.state->DAT_00572078_fadeTracks.FUN_0025d480_step(track,
                                                                           environment_.frameTicks)
                 ? 1u
                 : 0u;
    }

    // 0xBF / 0xC0 (FUN_00263f28): four expressions -- r, g, b, radius. Both go
    // through the same body; only the allocator differs.
    case 0xBF:
    case 0xC0:
      noteOpcode(opcode, OpcodeSupport::Modelled);
      return static_cast<std::uint32_t>(FUN_00263f28_allocate_light());

    // 0x9C (FUN_00261c60): one expression (the track) then one inline byte (the
    // channel, which the original diagnoses above 2). It *reads* the track's
    // current colour -- FUN_0025d590 is a one-line load, not a configure, which
    // the dispatch-table note calls "bind a track/mode combination". The script
    // passes three of these straight into 0x97's or 0xC3's colour operands.
    case 0x9C:
    {
      noteOpcode(opcode, OpcodeSupport::Modelled);
      const std::uint32_t track = FUN_0025c258_evaluate();
      if (halted_)
      {
        return 0;
      }
      const std::uint8_t channel = readU8();
      return environment_.state->DAT_00572078_fadeTracks.FUN_0025d590_channel(track, channel);
    }

    // 0xC2 (FUN_00264148): two expressions -- a light slot and an alpha byte.
    // Note the original does *not* range-check this one, unlike every other
    // member of the family; the port clamps rather than writing past the table.
    case 0xC2:
    {
      noteOpcode(opcode, OpcodeSupport::Modelled);
      const std::uint32_t slot = FUN_0025c258_evaluate();
      const std::uint32_t alpha = FUN_0025c258_evaluate();
      if (halted_)
      {
        return 0;
      }
      environment_.state->DAT_00343888_lights.slot(slot).alpha = static_cast<std::uint8_t>(alpha);
      return 0;
    }

    // The dynamic light slots, DAT_00343888..DAT_00343898, sixteen entries of
    // five words. The port's renderer takes its lighting from the scene
    // environment block and has nowhere to put these, so the whole family is
    // consumed and counted together:
    //
    //   0xC1 FUN_00263fe8  8  bind a light to an entity bone
    //   0xC3 FUN_00264190  4  slot, r, g, b
    //   0xC4 FUN_00264218  2  slot, intensity
    //   0xC5 FUN_00264298  4  slot, x, y, z
    //   0xC6 FUN_00264360  2  slot, entity -- copy that entity's position
    //   0xC7 FUN_002643f0  1  slot -- intensity to zero
    // 0xC1 stays operands-only. It is the one member of the family that is not
    // a plain table write: it selects an entity, claims that entity's own light
    // index at +0x195 if it has none, and runs the offset through FUN_0020dc88
    // -- the attachment-chain matrix walk -- to place the light on a bone. The
    // port has the bone palettes but not that walk on this path, and neither
    // scene calls it, so modelling it would be untested code.
    case 0xC1:
      noteOpcode(opcode, OpcodeSupport::OperandsOnly);
      return consumeOnly(opcode, 8);

    case 0xC3:
    {
      noteOpcode(opcode, OpcodeSupport::Modelled);
      const std::uint32_t slot = FUN_0025c258_evaluate();
      const std::uint32_t red = FUN_0025c258_evaluate();
      const std::uint32_t green = FUN_0025c258_evaluate();
      const std::uint32_t blue = FUN_0025c258_evaluate();
      if (halted_)
      {
        return 0;
      }
      auto &light = environment_.state->DAT_00343888_lights.slot(slot);
      light.red = static_cast<std::uint8_t>(red);
      light.green = static_cast<std::uint8_t>(green);
      light.blue = static_cast<std::uint8_t>(blue);
      return 0;
    }

    case 0xC5:
    {
      noteOpcode(opcode, OpcodeSupport::Modelled);
      const std::uint32_t slot = FUN_0025c258_evaluate();
      const float x = static_cast<float>(static_cast<std::int32_t>(FUN_0025c258_evaluate())) /
                      kScriptCoordinateScale;
      const float y = static_cast<float>(static_cast<std::int32_t>(FUN_0025c258_evaluate())) /
                      kScriptCoordinateScale;
      const float z = static_cast<float>(static_cast<std::int32_t>(FUN_0025c258_evaluate())) /
                      kScriptCoordinateScale;
      if (halted_)
      {
        return 0;
      }
      auto &light = environment_.state->DAT_00343888_lights.slot(slot);
      light.x = x;
      light.y = y;
      light.z = z;
      return 0;
    }

    case 0xC4:
    {
      noteOpcode(opcode, OpcodeSupport::Modelled);
      const std::uint32_t slot = FUN_0025c258_evaluate();
      const float radius = static_cast<float>(static_cast<std::int32_t>(FUN_0025c258_evaluate())) /
                           kScriptCoordinateScale;
      if (halted_)
      {
        return 0;
      }
      environment_.state->DAT_00343888_lights.slot(slot).radius = radius;
      environment_.state->DAT_00343888_lights.noteRadius(slot, radius);
      return 0;
    }

    // 0xC6 (FUN_00264360): slot, then an entity selector. The original latches
    // DAT_00355044 into a local *before* evaluating the operands and re-reads
    // it after FUN_0025d6c0 -- the first read is only the fallback handed to the
    // selector for the 0x100 "keep the current one" case, and the position
    // copied is the newly selected entity's.
    case 0xC6:
    {
      noteOpcode(opcode, OpcodeSupport::Modelled);
      const std::uint32_t slot = FUN_0025c258_evaluate();
      const std::uint32_t selector = FUN_0025c258_evaluate();
      if (halted_)
      {
        return 0;
      }
      if (const orphen::ported::entity::OriginalEntity *source = resolveEntity(selector))
      {
        auto &light = environment_.state->DAT_00343888_lights.slot(slot);
        light.x = source->positionX20;
        light.y = source->positionZ24;
        light.z = source->positionY28;
      }
      return 0;
    }

    case 0xC7:
    {
      noteOpcode(opcode, OpcodeSupport::Modelled);
      const std::uint32_t slot = FUN_0025c258_evaluate();
      if (halted_)
      {
        return 0;
      }
      environment_.state->DAT_00343888_lights.slot(slot).radius = 0.0f;
      return 0;
    }

    // Single-value stores into globals the port has no consumer for.
    //
    //   0xC8 FUN_00264448  text colour index
    //   0xCA FUN_00264500  the dialogue speaker byte
    //   0xD6 FUN_00264d68  renderer byte uGpffffb08c
    //   0xDA FUN_00264ea0  uGpffffb6f0
    //   0xE2 FUN_002650e0  DAT_003556fc
    //   0xE3 FUN_00265120  DAT_00355641, which s01_e012 clears on its way out
    case 0xC8:
    case 0xCA:
    case 0xD6:
    case 0xDA:
    case 0xE2:
    case 0xE3:
      noteOpcode(opcode, OpcodeSupport::OperandsOnly);
      return consumeOnly(opcode, 1);

    // 0xC9 FUN_00264470  six values: a colour index and a palette triple.
    case 0xC9:
      noteOpcode(opcode, OpcodeSupport::OperandsOnly);
      return consumeOnly(opcode, 6);

    // 0xD7 FUN_00264d90  a palette slot and its byte.
    case 0xD7:
      noteOpcode(opcode, OpcodeSupport::OperandsOnly);
      return consumeOnly(opcode, 2);

    // 0xD5 (FUN_00264d40): one expression into uGpffffb084, a renderer mode
    // byte. s01_e012's own handoff sets it to 1 on the way out of the opening.
    case 0xD5:
      noteOpcode(opcode, OpcodeSupport::OperandsOnly);
      return consumeOnly(opcode, 1);

    // 0xBD (FUN_00263e80): selector, method, and two arguments, into
    // FUN_00242a18 -- a second dispatcher with its own instruction set. Methods
    // 0x70..0x72 are voice play / tune / poll, and VOICE.BIN is not in the disc
    // root, so nothing could be played even with the call modelled. The method
    // id is recorded so --scr-report can say what the scene asked for.
    case 0xBD:
    {
      noteOpcode(opcode, OpcodeSupport::Modelled);
      const std::uint32_t selector = FUN_0025c258_evaluate();
      const std::uint32_t method = FUN_0025c258_evaluate();
      const std::uint32_t arg3 = FUN_0025c258_evaluate();
      const std::uint32_t arg4 = FUN_0025c258_evaluate();
      if (halted_)
      {
        return 0;
      }
      if (selector != orphen::ported::entity::kCurrentEntityIndex)
      {
        resolveEntity(selector);
      }
      trace_.recordObjectMethod(method);

      // FUN_00242a18 is a method table on the *selected entity* -- param_1 is
      // uGpffffb0d4, not an audio handle. **Methods 0x70/0x72 are waypoint
      // path-follow, not voice.** An earlier reading here called them voice and
      // that was wrong; it is why s01_e012's Dortin never walks.
      //
      //   0x70 -> FUN_002443f8(entity, blobOffset, duration)
      //           Allocates a follower slot, evaluates the expression list at
      //           `blobOffset + DAT_00355058` -- a u32 count followed by
      //           count * 3 VM expressions, x/y/z at 1000-scale -- and stores
      //           them as the path. Third operand is the duration.
      //   0x72 -> FUN_002445c8(entity)
      //           Finds the entity's follower slot and returns its progress,
      //           ((total - remaining) * 1000) / total + 1, so non-zero while
      //           it is still walking and 0 once the slot is gone.
      //
      // The scene script's shape is: start with 0x70, and only if that succeeds
      // install a subproc that polls 0x72 until it reads 0. Dortin's is subproc
      // 0x53D at blob 0x4e71, started from 0x4e34 with path 0x366C over 400 --
      // three waypoints walking him from (5.652, -3.472) to (5.084, -2.217),
      // toward Volcan. Confirmed against a save state: he sits 77% along the
      // first segment, 0.018 off the line.
      //
      switch (method)
      {
      case 0x70:
      {
        // FUN_002443f8. The path lives at `arg3 + DAT_00355058`: a u32 count,
        // then count*3 VM expressions. FUN_0025d618 evaluates them with the
        // stream pointer temporarily aimed there and divides each by 100;
        // FUN_002443f8 then divides by 1000. Net /100000, which is the world
        // scale the 0x0F literal already carries, so this reuses the ordinary
        // evaluator rather than re-deriving it.
        if (currentEntity_ == kNoEntity || !environment_.FUN_002443f8_start_path)
        {
          return -1;
        }
        std::vector<orphen::ported::psm2::Vec3> waypoints;
        if (!decodePathWaypoints(arg3, waypoints) || waypoints.empty())
        {
          return -1;
        }
        return environment_.FUN_002443f8_start_path(currentEntity_, waypoints, arg4);
      }
      case 0x72:
        // FUN_002445c8. Non-zero while walking; 0 once the follower slot is
        // released, which is the value the script's wait subproc spins for.
        if (currentEntity_ == kNoEntity || !environment_.FUN_002445c8_path_progress)
        {
          return 0;
        }
        return environment_.FUN_002445c8_path_progress(currentEntity_);
      default: return 0;
      }
    }

    // 0xA5 (FUN_00262058): the same family, but the operands interleave --
    // expression, inline byte, expression -- so it cannot go through
    // consumeOnly's "all expressions then all bytes" shape. FUN_0022db50 sets
    // or clears the mask bits in the record's +0x04 itself.
    case 0xA5:
    {
      noteOpcode(opcode, OpcodeSupport::OperandsOnly);
      FUN_0025c258_evaluate();
      readU8();
      FUN_0025c258_evaluate();
      return 0;
    }

    default:
      noteOpcode(opcode, OpcodeSupport::Unimplemented);
      return haltUnimplemented(opcode);
    }
  }

  std::uint32_t SceneCommandInterpreter::dispatchExtended(std::uint8_t extension)
  {
    // PTR_LAB_0031e538. The 0xFF prefix and the extension byte have both been
    // consumed, so the opcode starts two bytes back.
    const std::uint16_t opcode = static_cast<std::uint16_t>(extension + 0x100);
    const auto note = [&](OpcodeSupport support) {
      trace_.recordOpcode(opcode, streamOffset_ >= 2 ? streamOffset_ - 2 : 0, support);
    };

    switch (opcode)
    {
    case 0x140:
    case 0x141:
      note(OpcodeSupport::Modelled);
      return FUN_00260578_spawn_attached_prop();

    // 0x146 (FUN_00265738 -> FUN_00213640): one expression, the bandana mode.
    // Named submit_single_word_b in the dispatch table, which says nothing; the
    // target is the cloth simulation on pool slot 4.
    case 0x146:
    {
      note(OpcodeSupport::Modelled);
      const std::uint32_t mode = FUN_0025c258_evaluate();
      if (!halted_ && environment_.FUN_00213640_set_bandana)
      {
        environment_.FUN_00213640_set_bandana(static_cast<std::int32_t>(mode));
      }
      return 0;
    }

    // 0x100 (FUN_002620a8): no expressions, one inline byte selecting which of
    // six globals at uGpffffad38..ad4c to zero.
    case 0x100:
      note(OpcodeSupport::OperandsOnly);
      return consumeOnly(opcode, 0, 1);

    // 0x102 / 0x106 / 0x108 (FUN_00262250): eight expressions -- four
    // coordinates, three values and an entity selector -- into one of the
    // particle emitters. No emitters in the port.
    case 0x102:
    case 0x106:
    case 0x108:
      note(OpcodeSupport::OperandsOnly);
      return consumeOnly(opcode, 8);

    // 0x125 / 0x126 (FUN_00261330): an inline **u16** cue id first, then the
    // expression selecting the entity to play it on. The inline read comes
    // before the expression, so the order cannot be swapped. 0x126 takes one
    // more expression and goes to a different player; the port plays both the
    // same way.
    case 0x125:
    case 0x126:
    {
      const bool twoArgs = (opcode == 0x126);
      note(OpcodeSupport::Modelled);
      const std::uint32_t cue = readU8() | (static_cast<std::uint32_t>(readU8()) << 8);
      const std::uint32_t selector = FUN_0025c258_evaluate();
      if (twoArgs)
      {
        FUN_0025c258_evaluate();
      }
      if (halted_)
      {
        return 0;
      }
      if (selector != orphen::ported::entity::kCurrentEntityIndex)
      {
        resolveEntity(selector);
      }
      if (environment_.FUN_00267d38_play_at_entity &&
          currentEntity_ < orphen::ported::entity::kEntitySlotCount)
      {
        environment_.FUN_00267d38_play_at_entity(static_cast<std::uint16_t>(cue), currentEntity_);
      }
      return 0;
    }

    // 0x13D (FUN_00265410): selector, then an animation id, through
    // FUN_00225bc8 -- the *proper* animation setter, which also resets the
    // state timer, clears +0xA2 and zeroes the timeline cursor. Writing +0xA0
    // through object register 8 skips all of that; a cutscene uses this when it
    // wants the animation to restart from the top.
    case 0x13D:
    {
      note(OpcodeSupport::Modelled);
      const std::uint32_t selector = FUN_0025c258_evaluate();
      const std::uint32_t animation = FUN_0025c258_evaluate();
      if (halted_)
      {
        return 0;
      }
      if (selector != orphen::ported::entity::kCurrentEntityIndex)
      {
        resolveEntity(selector);
      }
      if (currentEntity_ < orphen::ported::entity::kEntitySlotCount && environment_.entityPool != nullptr)
      {
        orphen::ported::entity::FUN_00225bc8_set_animation(
            environment_.entityPool->slot(currentEntity_), static_cast<std::uint16_t>(animation));
      }
      return 0;
    }

    // The remaining audio dispatchers (FUN_00261500 / 530 / 570 / 5b0 / 5d8 /
    // 600 / 638 / 7c0 / 868). All end at a SIF command to the IOP, which the
    // port's mixer does not model.
    // 0x142 (FUN_002606d0): one expression. Destroys everything attached to the
    // entity, rebuilds the bandana when it is the lead (FUN_002298d0 answers 0
    // for type 1 and nothing else), and clears all 42 of its bone hides. This is
    // how a cutscene gives a character its own head back.
    case 0x142:
    {
      note(OpcodeSupport::Modelled);
      const std::uint32_t selector = FUN_0025c258_evaluate();
      if (halted_)
      {
        return 0;
      }
      if (selector != orphen::ported::entity::kCurrentEntityIndex)
      {
        resolveEntity(selector);
      }
      if (currentEntity_ < orphen::ported::entity::kEntitySlotCount &&
          environment_.FUN_002606d0_detach_children)
      {
        environment_.FUN_002606d0_detach_children(currentEntity_);
      }
      return 0;
    }

    // 0x10B (FUN_00262780): **ten** expressions into FUN_002198a0, a graphics
    // submitter -- seven coordinates scaled by DAT_00352c74 and three raw
    // parameters, interleaved in the original's stack frame but read in stream
    // order. The count is the thing that matters here; getting it wrong
    // desyncs everything after it.
    case 0x10B:
      note(OpcodeSupport::OperandsOnly);
      return consumeOnly(opcode, 10);

    // 0x10A (FUN_00262690): eight expressions into FUN_00219fc8, the sibling
    // submitter to 0x10B's. Same reasoning -- the arity is what matters.
    case 0x10A:
      note(OpcodeSupport::OperandsOnly);
      return consumeOnly(opcode, 8);

    // 0x129 (FUN_00261500): slot, then fader, into FUN_00205d90 -- start a
    // sequence the scene already loaded into that slot. s01_e012 issues one of
    // these, `(6, 1000)`, which is the piece under Sephy's scene.
    case 0x129:
    {
      note(OpcodeSupport::Modelled);
      const auto slot = static_cast<std::int32_t>(FUN_0025c258_evaluate());
      const auto fader = static_cast<std::int32_t>(FUN_0025c258_evaluate());
      if (halted_)
      {
        return 0;
      }
      if (environment_.FUN_00205d90_play_music_slot && slot >= 0)
      {
        environment_.FUN_00205d90_play_music_slot(static_cast<std::size_t>(slot), fader);
      }
      return 0;
    }

    // 0x12A (FUN_00261530) and 0x12B (FUN_00261570): slot, speed, fader. The
    // first ramps a slot up through FUN_002063c8, the second down through
    // FUN_00206260 -- which is how a scene ducks its bed under a cue and brings
    // it back, and how music stops without a click.
    case 0x12A:
    case 0x12B:
    {
      note(OpcodeSupport::Modelled);
      const auto slot = static_cast<std::int32_t>(FUN_0025c258_evaluate());
      const auto speed = static_cast<std::int32_t>(FUN_0025c258_evaluate());
      const auto fader = static_cast<std::int32_t>(FUN_0025c258_evaluate());
      if (halted_)
      {
        return 0;
      }
      if (slot >= 0)
      {
        const auto &ramp = opcode == 0x12A ? environment_.FUN_002063c8_ramp_music_up
                                           : environment_.FUN_00206260_ramp_music_down;
        if (ramp)
        {
          ramp(static_cast<std::size_t>(slot), speed, fader);
        }
      }
      return 0;
    }

    case 0x12C:
    case 0x12D:
    case 0x137:
      note(OpcodeSupport::OperandsOnly);
      return consumeOnly(opcode, 1);

    case 0x12E:
    case 0x12F:
      note(OpcodeSupport::OperandsOnly);
      return consumeOnly(opcode, 2);

    case 0x134:
      note(OpcodeSupport::OperandsOnly);
      return consumeOnly(opcode, 0);

    case 0x149:
      note(OpcodeSupport::Modelled);
      return FUN_00265790_set_global_byte();

    default:
      note(OpcodeSupport::Unimplemented);
      return haltUnimplemented(opcode);
    }
  }

} // namespace orphen::ported::script

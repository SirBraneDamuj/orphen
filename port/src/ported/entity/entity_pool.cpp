#include "ported/entity/entity_pool.h"

#include "ported/model/psc3_skeleton.h"

#include <cstring>

namespace orphen::ported::entity
{

  void EntityPool::setBoneOverrideTable(orphen::ported::model::EntityBoneOverrides *table,
                                        std::size_t count)
  {
    boneOverrides_ = table;
    boneOverrideCount_ = count;
  }

  void EntityPool::clearBoneOverrides(std::size_t index)
  {
    if (boneOverrides_ != nullptr && index < boneOverrideCount_)
    {
      boneOverrides_[index].reset();
    }
  }

  void EntityPool::reset()
  {
    // FUN_0022a418 walks slots 10..255 calling FUN_00267e78(slot, 0x1d8) and
    // zeroing the matching status byte. Slot 0 and the party slots are rebuilt
    // by their own paths, but a fresh pool starts entirely clear.
    for (std::size_t index = 0; index < kEntitySlotCount; ++index)
    {
      slots_[index] = OriginalEntity{};
      status_[index] = SlotStatus::Free;
      clearBoneOverrides(index);
    }
  }

  void EntityPool::releaseSlot(std::size_t index)
  {
    if (index >= kEntitySlotCount)
    {
      return;
    }
    slots_[index] = OriginalEntity{};
    status_[index] = SlotStatus::Free;
    clearBoneOverrides(index);
  }

  std::size_t EntityPool::FUN_00265dc0_allocate_slot(std::size_t start, std::size_t count)
  {
    const std::size_t end = start + count;
    for (std::size_t index = start; index < end && index < kEntitySlotCount; ++index)
    {
      if (status_[index] == SlotStatus::Free)
      {
        status_[index] = SlotStatus::Allocated;
        return index;
      }
    }
    return kEntitySlotCount; // the original returns a null pointer here
  }

  void EntityPool::FUN_00229c40_initialize(std::size_t index,
                                           std::int32_t typeId,
                                           const EntityDescriptorTable &descriptors)
  {
    if (index >= kEntitySlotCount)
    {
      return;
    }

    OriginalEntity &entity = slots_[index];
    entity = OriginalEntity{};
    // FUN_00229c40:20's FUN_00267e78(param_1, 0x1d8) -- the whole slot, which
    // includes +0x168.
    clearBoneOverrides(index);
    entity.typeId00 = static_cast<std::int16_t>(typeId);

    // The status byte follows the sign of the type id, not the allocation.
    if (typeId > 0)
    {
      status_[index] = SlotStatus::ScriptSpawned;
    }
    else if (typeId < 0)
    {
      status_[index] = SlotStatus::Allocated;
    }
    else
    {
      status_[index] = SlotStatus::Free;
    }

    const auto descriptor = descriptors.FUN_00229980_resolve(static_cast<std::uint32_t>(typeId));
    if (!descriptor.has_value())
    {
      return;
    }

    entity.descriptorFlags02 = descriptor->flags0x04;
    entity.halfword04 = descriptor->halfword0x18;
    entity.halfword08 = descriptor->halfword0x16;
    if (static_cast<std::uint32_t>(typeId) < kFirstStreamedTypeId)
    {
      entity.halfword08 = static_cast<std::uint16_t>(entity.halfword08 | 0x20);
    }
    entity.flags06 = static_cast<std::uint16_t>(static_cast<std::int16_t>(descriptor->byte0x14));
    // FUN_00229c40:75. The billboard pass is the only reader; see +0x133.
    entity.depthBias133 = static_cast<std::int8_t>(descriptor->byte0x02);
    entity.radius54 = descriptor->radius0x08;
    entity.height58 = descriptor->height0x0c;
    // FUN_00229c40:67,70 writes the hit-test volume from the same two numbers.
    // It is a separate pair of fields, not an alias, and the hit tests read
    // only this one.
    entity.hitVolumeRadius11c = descriptor->radius0x08;
    entity.hitVolumeHeight120 = descriptor->height0x0c;
    entity.modelIndex = descriptor->modelIndex0x00;

    // Descriptor +0x10 is copied to entity +0x80 as raw bits, so it is a float,
    // not an integer -- and it is the **walkable-slope limit**, not a step
    // height. Type id 1 reads 0.8727, which is 50 degrees in radians and is the
    // same value FUN_0022d258 tests a stored slope against. FUN_002262c0's only
    // read of +0x80 compares it to the destination surface's slope, and the step
    // height it uses is the global DAT_00352434 = 0.26.
    float word0x10AsFloat = 0.0f;
    std::memcpy(&word0x10AsFloat, &descriptor->word0x10, sizeof(word0x10AsFloat));
    entity.slopeLimit80 = word0x10AsFloat;

    // FUN_00229c40 clears the whole 0x1D8-byte slot and then never writes
    // +0xA0, so a freshly spawned entity is on animation 0 and stays there
    // until a behavior calls FUN_00225bc8. It does set +0xA2 and +0xAE to
    // 0xFFFF, which is what makes the first real selection count as a change.
    //
    // The port used to default this to 1. The EE dump settles it: the five
    // party members and the streamed prop, none of which have selected an
    // animation, all read +0xA0 == 0.
    entity.animationA0 = 0;
    entity.previousSubstateA2 = 0xFFFF;

    // FUN_00229c40:79-87, the block the port had been leaving on the zeroes
    // FUN_00267e78 wrote. Every one of these is visible in an EE dump, and
    // `scripts/dump_ee_entities.py --compare` had all 84 occupied slots of
    // s01_e012 differing on the first two:
    //
    //   +0x74  the terrain reject mask. `0x04000000` is seeded here for
    //          everything, not by the spawn opcodes -- FUN_00266240 only ORs a
    //          caller's extra bits on top, the player adds `0x08000000` in
    //          FUN_002cb9a8 and a party member `0x0D000000`. Left at zero the
    //          query rejects *nothing*, so surface classes the original refuses
    //          to stand on become walkable floor.
    //   +0x64  the blocking-entity slot. The original clears the record and
    //          never writes -1 here; readers gate on +0x0C's 0x60 bits instead,
    //          which FUN_002262c0 rebuilds every frame.
    //   +0x0C  starts at 0x1000, not 0.
    //   +0x98  the placement record index, -1 until a spawn path fills it in.
    //   +0x7C  100.0.
    entity.rejectTerrainMask74 = 0x04000000u;
    entity.blockedBy64 = 0;
    entity.collisionFlags0c = 0x1000u;
    entity.placementRecordIndex98 = -1;

    // FUN_00229c40's last three lines. Everything whose +0x02 clears 0x200 --
    // which is every type this scene spawns -- starts with the keyframe blend
    // saturated and "not drawn last frame" raised, so the very first pose it
    // shows is the sampled one rather than something eased out of whatever the
    // slot held before. The port had been leaning on the pose filter's own
    // `seeded` flag for this; the original states it here.
    if ((entity.descriptorFlags02 & 0x0200u) == 0)
    {
      entity.animationBlend13c = 1.0f;
      entity.halfword08 = static_cast<std::uint16_t>(entity.halfword08 | 0x0010u);
    }
  }

  std::size_t EntityPool::FUN_00265e28_allocate_and_initialize(std::int32_t typeId,
                                                               const EntityDescriptorTable &descriptors)
  {
    const std::size_t index = FUN_00265dc0_allocate_slot(kFirstScriptSlot, kScriptSlotCount);
    if (index >= kEntitySlotCount)
    {
      return kEntitySlotCount;
    }
    if (typeId >= 0)
    {
      FUN_00229c40_initialize(index, typeId, descriptors);
    }
    return index;
  }

  std::size_t EntityPool::scriptSpawnedCount() const
  {
    std::size_t count = 0;
    for (std::size_t index = kFirstScriptSlot; index < kEntitySlotCount; ++index)
    {
      if (status_[index] != SlotStatus::Free)
      {
        ++count;
      }
    }
    return count;
  }

} // namespace orphen::ported::entity

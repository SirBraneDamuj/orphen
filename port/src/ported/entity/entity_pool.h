#pragma once

#include "ported/entity/entity_descriptor_table.h"
#include "ported/entity/original_entity.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace orphen::ported::entity
{

  // The original's entity pool: DAT_0058beb0, 256 slots, and the per-slot status
  // array DAT_005a96b0 that begins immediately after it.
  // See analyzed/entity_pool_and_descriptors.c.
  constexpr std::size_t kEntitySlotCount = 256;

  // FUN_00265dc0(10, 0xF6). Slot 0 is the lead player and 1..9 are reserved for
  // the party and system entities; scripts only ever allocate from [10, 256).
  constexpr std::size_t kFirstScriptSlot = 10;
  constexpr std::size_t kScriptSlotCount = 0xF6;

  // Script opcodes use this index to mean "the entity most recently selected or
  // spawned" (puGpffffb0d4) rather than a real slot.
  constexpr std::uint32_t kCurrentEntityIndex = 0x100;

  // DAT_005a96b0 values.
  enum class SlotStatus : std::int8_t
  {
    Free = 0,
    ScriptSpawned = 1, // FUN_00229c40 with a positive type id
    Allocated = -1,    // reserved by FUN_00265dc0, or a negative type id
  };

  class EntityPool
  {
  public:
    EntityPool() { reset(); }

    // FUN_0022a418 clears slots 10..255 on every map load and leaves slot 0
    // alone, because slot 0 is rebuilt separately from its own descriptor.
    void reset();

    OriginalEntity &slot(std::size_t index) { return slots_[index]; }
    const OriginalEntity &slot(std::size_t index) const { return slots_[index]; }
    SlotStatus status(std::size_t index) const { return status_[index]; }
    static constexpr std::size_t slotCount() { return kEntitySlotCount; }

    OriginalEntity &leadPlayer() { return slots_[0]; }
    const OriginalEntity &leadPlayer() const { return slots_[0]; }

    // FUN_00265dc0: linear scan for a free slot in [start, start + count),
    // marking it Allocated. Returns kEntitySlotCount when the pool is full.
    std::size_t FUN_00265dc0_allocate_slot(std::size_t start, std::size_t count);

    // FUN_00265e28: allocate from the script range and initialise, unless the
    // type id is negative.
    std::size_t FUN_00265e28_allocate_and_initialize(std::int32_t typeId,
                                                     const EntityDescriptorTable &descriptors);

    // FUN_00229c40: clear the slot and fill it from the type descriptor. When the
    // descriptor cannot be resolved -- no executable loaded, or an id streamed
    // with the map -- the slot is still initialised, but keeps default collision
    // dimensions and records modelIndex -1 so callers can say so rather than
    // silently drawing a wrong-sized box.
    void FUN_00229c40_initialize(std::size_t index,
                                 std::int32_t typeId,
                                 const EntityDescriptorTable &descriptors);

    // FUN_00265ec0, used by the map-load clear.
    void releaseSlot(std::size_t index);

    // Iteration helper for rendering and reporting: every slot currently holding
    // a script-spawned entity.
    template <typename Visitor>
    void forEachScriptSpawned(Visitor &&visitor) const
    {
      for (std::size_t index = kFirstScriptSlot; index < kEntitySlotCount; ++index)
      {
        if (status_[index] != SlotStatus::Free)
        {
          visitor(index, slots_[index]);
        }
      }
    }

    std::size_t scriptSpawnedCount() const;

  private:
    std::array<OriginalEntity, kEntitySlotCount> slots_{};
    std::array<SlotStatus, kEntitySlotCount> status_{};
  };

} // namespace orphen::ported::entity

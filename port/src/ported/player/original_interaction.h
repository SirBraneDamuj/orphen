#pragma once

#include "ported/entity/entity_pool.h"

#include <cstddef>
#include <cstdint>
#include <functional>

namespace orphen::ported::player
{

  // Native counterparts of the three functions behind the confirm button:
  //
  //   FUN_00252cc0 (0x00252cc0)  walk the pool, probe the nearest candidate
  //   FUN_00252a18 (0x00252a18)  find that candidate
  //   FUN_00252828 (0x00252828)  decide what interacting with it means
  //
  // FUN_00256bb8's grounded branch calls the first of these when the mapped
  // pressed word has bit 0x40 -- Cross, the confirm button -- and returns early
  // if it reports a hit, which is why you cannot walk and interact on the same
  // frame.
  //
  // The branch FUN_00252828 takes is chosen by the *descriptor* flag at entity
  // +0x02, not by the type id, and the two that matter in s01_e024 split cleanly:
  //
  //   0x4000 set   run the scene script's header word 3 with this entity
  //                selected. Types 0x03..0x07, the party members -- so talking
  //                to a party member is entirely script-driven.
  //   0x0100 set   a native branch chosen by type. Type 0x3A, the chest, puts
  //                the *player* into state 0xC.
  //
  // Read out of SLUS_200.11 those are 0x4004 for every party member and 0x0100
  // for the chest, so nothing here is inferred from behavior.

  enum class InteractionKind : std::uint8_t
  {
    None = 0,
    ScriptedEntity, // +0x02 bit 0x4000: header word 3
    Chest,          // type 0x3A
    StreamedProp,   // ids in the 0x272 / 0x373 / 0x474 bands
  };

  struct InteractionResult
  {
    InteractionKind kind = InteractionKind::None;
    std::size_t targetSlot = orphen::ported::entity::kEntitySlotCount;
    std::int16_t targetType = 0;

    // Type 0x3A only: the event flag id at the chest's +0x198. Setting it is
    // what actually opens the chest -- FUN_002d1ea8 only ever observes it.
    std::uint32_t chestFlagId = 0;

    bool handled() const { return kind != InteractionKind::None; }
  };

  // FUN_00252a18: the nearest interactable entity whose cylinder overlaps a
  // probe point placed fGpffff88f0 (0.30 world units) ahead of the actor's
  // facing. Returns kEntitySlotCount when there is nothing there.
  std::size_t FUN_00252a18_find_nearest_candidate(orphen::ported::entity::EntityPool &pool,
                                                  std::size_t actorSlot,
                                                  std::size_t firstSlot);

  // FUN_00252cc0 + FUN_00252828. Decides only; the caller applies the outcome,
  // because opening a chest is a flag write and talking to a party member is a
  // script entry, and neither belongs to the player controller.
  // eventFlag is FUN_00266368, needed to tell an already-opened chest from a
  // closed one. Passed in rather than reached for, the same way the actor tick
  // takes it.
  InteractionResult FUN_00252cc0_probe_for_interaction(orphen::ported::entity::EntityPool &pool,
                                                       std::size_t actorSlot,
                                                       const std::function<bool(std::uint32_t)> &eventFlag);

} // namespace orphen::ported::player

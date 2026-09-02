#pragma once

// src/FUN_00249610.c (0x00249610) and src/FUN_0024a360.c (0x0024A360): the
// battle counterpart of the field player update, and the action-to-state
// translation in front of it.
//
// FUN_002239c8:117 chooses between them:
//
//   if ((cGpffffb663 == 0) || (sGpffffb052 == 0))  FUN_00251ed8(0x58beb0, ...);
//   else                                           FUN_00249610(0x58beb0);
//
// **FUN_0024a360 is the whole of "a button becomes a state".** For a pending
// action byte in 0x84..0x92 it computes `state = action + 0x3FE5` -- states
// 105..119 -- and copies the byte into control block +0x0F. Bit 0x4000 rides on
// the state as a restart marker: a handler that sees it is being entered, and
// clears it itself.
//
// FUN_00249610 then dispatches through a table of 24 handlers per character
// class, selected by party record +0x00 and indexed by `(state & 0xBFFF) - 100`:
//
//   class 1 (Orphen) 0x0031DD60   class 5 0x0031DDC0   class 3 0x0031DE20
//   class 4          0x0031DE80   class 6 0x0031DEE0   class 7 0x0031DF40
//
// Only class 1 is ported. The handler is called with the entity and the
// character's charge halfword (+0x62) and its return value is written back
// there, so that round trip is load-bearing: break it and charging looks like
// it does nothing.
//
// **No target.** FUN_00249610:170 is `else if (lVar11 < 3)`, where lVar11 is
// control block +0x2C. With no enemy table every member keeps -1 there, the
// whole face-the-target block is skipped, and control falls straight to the
// state dispatch. That is the mode this slice runs in, and it costs no special
// case -- it is the original's own branch.

#include "ported/battle/battle_party.h"
#include "ported/battle/battle_trace.h"
#include "ported/entity/entity_pool.h"

#include <cstdint>
#include <functional>

namespace orphen::ported::battle
{

  struct BattleUpdateEnvironment
  {
    BattleParty *party = nullptr;
    orphen::ported::entity::EntityPool *pool = nullptr;
    const orphen::ported::entity::EntityDescriptorTable *descriptors = nullptr;
    BattleTrace *trace = nullptr;
    std::uint16_t frameTicks = 0x20;
    // FUN_00267d38: play a cue at an entity. The battle states ask for the
    // charge loop (0xD7..0xDA), the guard raise (0xE7) and the guard hit
    // (0xE8).
    std::function<void(std::uint16_t cue, std::size_t slot)> FUN_00267d38_play_at_entity;
    // FUN_00216868, through the runtime's seeded LCG.
    std::function<std::uint32_t()> FUN_00216868_random;
  };

  // FUN_0024a360: spend the pending action byte. Returns the original's three
  // codes -- 0 normal, 1 staggered, 2 dead, 3 knocked down.
  std::uint32_t FUN_0024a360_take_pending_action(const BattleUpdateEnvironment &environment,
                                                 std::size_t entitySlot);

  // FUN_00249610. Runs for one member; this slice only ever calls it for the
  // lead, pool slot 0.
  void FUN_00249610_battle_character_update(const BattleUpdateEnvironment &environment,
                                            std::size_t entitySlot);

  // The class-1 table at 0x0031DD60, exposed so --battle-report can say whether
  // a state it saw has a handler.
  bool class1StateIsPorted(std::uint16_t state);

} // namespace orphen::ported::battle

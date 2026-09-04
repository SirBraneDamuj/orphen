#pragma once

// src/FUN_002462c8.c (0x002462C8): the battle command input handler -- the
// function that turns a face button into an action byte.
//
// It is the last thing FUN_0023fd30 calls, and it drives exactly one member:
// DAT_00354ebe, the one the player has. Everything it writes is control block
// +0x0E, the pending action byte, which FUN_0024a360 spends on the next frame.
//
// **The five action pairs.** Each of the three assignable slots binds to one of
// four pairs by the item's kind byte, and Square is hardwired to the fifth:
//
//   trigger mask  held mask   press -> action   release -> action
//   +0x04         +0x00       0x86              0x84 / 0x85
//   +0x0C         +0x08       0x8A              0x8B
//   +0x14         +0x10       0x8C              0x8D
//   +0x1C         +0x18       0x8E              0x8F
//   +0x24         +0x20       0x90              0x91   (Square)
//
// A press ORs the button that fired into the *held* word beside its trigger
// word, and the release test is `held-word AND currently-held == 0`. That is
// the charge: the action stays at the press value for as long as the button is
// down, and the state handler behind it accumulates into entity +0x62.
//
// With the shipped loadout that is Triangle -> 0x8A/0x8B (Hand of Pyro),
// Circle -> 0x8C/0x8D (Bite of Lightning), Cross -> 0x86 -> 0x84/0x85 (Sword of
// the Fallen Devil) and Square -> 0x90/0x91.
//
// The pad words are DAT_003555f6 (newly pressed) and DAT_003555f4 (held), which
// the port already publishes as InputSnapshot::rawPressedPad / rawHeldPad in
// the same post-CONCAT11 bit layout. Target cycling additionally reads
// DAT_00355600, the movement stick's direction edge -- see the field comment.

#include "ported/battle/battle_encounter.h"
#include "ported/battle/battle_party.h"
#include "ported/battle/battle_tables.h"

#include <cstdint>
#include <functional>

namespace orphen::ported::battle
{

  struct CommandInputEnvironment
  {
    BattleParty *party = nullptr;
    // DAT_00354EB4 / DAT_00354EBA, the actor table target cycling walks.
    const BattleEncounter *encounter = nullptr;
    orphen::ported::entity::EntityPool *pool = nullptr;
    std::uint16_t DAT_003555f4_heldPad = 0;
    std::uint16_t DAT_003555f6_pressedPad = 0;
    // DAT_00355600: the **movement stick** quantised onto the same four
    // direction bits the D-pad occupies, not a second pad. FUN_0023b5d8 runs
    // the pair it just read into DAT_003555e8 / DAT_003555e4 -- the stick the
    // player walks on -- through FUN_0023b4e8 and keeps the result in
    // DAT_003555fe; DAT_00355600 is that word's newly-pressed edge.
    // FUN_002462c8 ORs it into the target-cycling test and nowhere else, which
    // is why the stick aims but never presses a button.
    std::uint16_t DAT_00355600_pressedPad2 = 0;
    std::uint16_t frameTicks = 0x20;
    // FUN_00216868. Reached only through the confusion branch
    // (DAT_0031da6c bit 0x1000), which randomises which of the three spell
    // buttons a press counts as. Routed through the runtime's seeded LCG so a
    // --frames run stays deterministic.
    std::function<std::uint32_t()> FUN_00216868_random;
  };

  // FUN_00248e48(0x78). Both target timers are armed with 0xF00 -- 120 frames
  // at 32 ticks each -- and both are stepped by FUN_00248e58.
  inline constexpr std::uint16_t kTargetDisplayTicks = 0x0F00;

  // FUN_002462c8. Returns the same diagnostic codes the original does; nothing
  // reads them (FUN_0023fd30 discards the value), they are kept because they
  // say which branch was taken and --battle-report prints the last one.
  std::int32_t FUN_002462c8_battle_command_input(const CommandInputEnvironment &environment);

} // namespace orphen::ported::battle

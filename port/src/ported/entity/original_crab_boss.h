#pragma once

// The giant crab -- type 0x7F, the boss of s14_e001 and the fight s01_e012
// hands off to. It is built like the other battle enemies (a wrapper, an
// action check, a state table) but it is much bigger than either of them, and
// it is the only actor in the game a *cutscene* drives directly.
//
//   src/FUN_00279298.c   the wrapper, in the secondary dispatch table
//   src/FUN_00279600.c   the action check: read the actor record's pending
//                        byte, dispatch it, clear it
//   src/FUN_002796c8.c   the action map, actions 2, 3, 6, 7, 12, 13 and 14
//   src/FUN_00279940.c   state 0, the one-shot init
//   0x00325930           PTR_FUN_00325930, its state handlers
//
// **The state table has sixteen entries, not twenty.** Reading it further gets
// 0x0027f978 / 0x0027fa88 / 0x0027fb30 / 0x0027fdf8, which are type 0x80's
// states 0..3 -- the next type's table starts immediately after this one and
// there is no terminator. States 0..15 are the crab's:
//
//   0  FUN_00279940  init: stats, attack records, the actor bind, and the
//                    partner link at +0x1C0
//   1  FUN_00279bf0  idle, flipping between animations 0 and 1
//   2  FUN_00279ca8  idle hold: roll 100..280 ticks, then pick the next move
//   3  FUN_00279d60  charge the player and slam
//   4  FUN_00279f50  the grab
//   5  FUN_0027a440  |
//   6  FUN_0027a7e8  |
//   7  FUN_0027a958  | the rest of the fight's moves
//   8  FUN_0027aaf8  |
//   9  FUN_0027acc8  |
//  10  FUN_0027ae98  |
//  11  FUN_0027b380  the one that writes work[0] = 2000 itself
//  12  FUN_0027b7c8  the hit reaction
//  13  FUN_0027bbe0  **the animatic's throw**: pick up the pair at +0x1C0,
//                    carry them, hurl them into the water, and hand the script
//                    back its cue
//  14  FUN_0027c458  **the animatic's swipe**, three of them and then a rest
//  15  FUN_0027ba90  the death
//
// ------------------------------------------------------ the animatic contract
//
// `s14_e001`'s preamble is a slot script switching on script work word 0 in
// beats ten apart, and it hands the crab its orders through opcode 0xBD method
// 0x6F -- FUN_00244248, the same action request the battle AI VM makes. Beats
// 110 and 150 then *wait*, and what they wait on is **FUN_0027cef8**, which
// writes script work word 1. Nothing else in the game writes it. So the script
// cannot advance past its own beat 110 until the crab has finished state 13,
// and cannot pass 150 until state 14 has run its three swipes.
//
// That is why the port sat still in this scene with every opcode implemented:
// the animatic is a duet and only one voice was singing.
//
// ------------------------------------------------------------- what it wants
//
// State 0 leans on three things the other enemies do not:
//
//   FUN_00248f18(0x0C)   the entity tagged 12 -- one half of the pair the crab
//                        throws -- parked at +0x1C0
//   FUN_00248f18(0x31)   the other half, parked in *that* entity's +0x198
//   FUN_00216078 x3      attack records 0, 1 and 2 into DAT_00573788/8c/90,
//                        a third per-type bank beside the 0x80's and the 0x8A's
//
// and the wrapper runs five helpers before the state every frame, gated on the
// crab's mode byte +0x94 (0 while it is a plain enemy, 12 while a throw is in
// flight, 14 once the fight proper has started).
//
// ----------------------------------------------------------- Orphen's half
//
// **FUN_0027d230 is not the crab.** The wrapper calls it every frame the mode
// byte is non-zero and it drives *the player*: the tumble Orphen does when a
// claw lands, along a Bezier to one of three scripted spots. It is also the
// only thing that clears DAT_0035526B, which FUN_0027c458 latches the moment a
// swipe connects and gates its own "ask for another" arm on. Leave it out and
// the crab takes exactly one swing and stops -- which is what the port did
// before it was ported.
//
// ------------------------------------------------------------ what is here
//
// States 0, 1, 2, 3, 5, 8, 12, 13 and 14, the action path, the move rotation,
// the leg thresholds, the body sweep, the splash and the script cue. States 4,
// 6, 7, 9, 10, 11 and 15 are the late-fight moves and the death; the crab only
// reaches them after it has shed a leg, and --actor-report names any it does.
//
// FUN_00277d30, the boss camera director, is deliberately absent: 1132 lines of
// camera poses behind a priority gate, changing nothing but the view. Its call
// sites are kept as comments in the states so the order is recoverable.

#include "ported/entity/actor_frame_update.h"
#include "ported/entity/original_entity.h"
#include "ported/resource/hit_parameter_table.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace orphen::ported::entity
{

  inline constexpr std::int32_t kCrabTypeId = 0x7F;
  inline constexpr std::uint32_t kFUN_00279298_crabBoss = 0x00279298;

  // PTR_FUN_00325930. Sixteen entries; see the note above about the seventeenth.
  inline constexpr std::uint32_t kPTR_FUN_00325930_crabStates = 0x00325930;
  inline constexpr std::size_t kCrabStateCount = 16;

  // DAT_00573788 / 8c / 90 -- the crab's own three attack records, filled by
  // state 0 out of SCR.BIN 0xBE exactly as the 0x80's and the 0x8A's are.
  // Record 0 is what FUN_0027c8a0 sweeps the body box with.
  struct CrabAttackRecords
  {
    std::array<orphen::ported::resource::HitParameters, 3> record{};
    bool filled = false;
  };
  CrabAttackRecords &DAT_00573788_crabAttacks();

  // FUN_00216078 x3 into the crab's own bank.
  void FUN_00216078_fill_crab_records(std::int16_t typeId, const ActorEnvironment &environment);

  // FUN_00279298 (0x00279298), type 0x7F.
  void FUN_00279298_crab_boss(OriginalEntity &entity,
                              std::size_t slot,
                              const ActorEnvironment &environment,
                              ActorTrace &trace);

} // namespace orphen::ported::entity

#pragma once

// Type 0x95, the boss of `s14_e002` -- the flyer that makes passes at the ship
// while Orphen fights it in the rigging. It is the scene: the script spawns it,
// then does nothing but poll its state.
//
//   src/FUN_00299390.c   the wrapper, in the secondary dispatch table at
//                        PTR_FUN_0031C8B0 + (0x95 - 0x7C)
//   src/FUN_002994e0.c   the action check
//   src/FUN_00299590.c   the action map -- actions 12, 13 and 14
//   src/FUN_002995e0.c   state 0, the one-shot init
//   0x00325E50           PTR_FUN_00325E50, fourteen state handlers
//
// **Fourteen entries, and three of them are bare `jr ra`.** 0x00299868 (state
// 1), 0x0029A4E0 (state 7) and 0x0029C190 (state 11) are two instructions each,
// read out of SLUS_200.11 -- real no-ops, not stubs. State 4 at 0x00299C98 is a
// real function Ghidra has no `src/` file for; it was recovered by
// disassembling the executable, and it turns out to be **state 3 mirrored** --
// +0x1C1 is 1 rather than 0, the orbit turns the other way, the close shot
// comes in on camera sub-shot 1 rather than 2, and its five tuning constants at
// 0x003538B4 hold the same values as state 3's at 0x0035389C.
//
//    0  FUN_002995E0  init: stats, the record bind, the nine body segments
//    1  0x00299868    no-op -- where the boss waits for its first order
//    2  FUN_00299870  the hold while the player is down; it has no exit
//    3  FUN_002999B0  an orbit pass, pulling in as it comes abeam
//    4  0x00299C98    the same mirrored
//    5  FUN_00299F80  **the dive**, seven of the rotation's eighteen entries
//    6  FUN_0029A2C0  the same orbit, descending, no pass
//    7  0x0029A4E0    no-op
//    8  FUN_0029A4E8  the strafing run off DAT_00325D38
//    9  FUN_0029A838  the transformation and the beam, thirteen phases
//   10  FUN_0029B628  smashing a mast section, which raises the water
//   11  0x0029C190    no-op
//   12  FUN_0029BC10  the death
//   13  FUN_0029C198  the intro
//
// **Nothing in the rotation picks state 10.** DAT_00325E28's eighteen entries
// top out at 9, and neither FUN_0029C468 nor the action map (actions 12, 13,
// 14) ever asks for 10 -- the only writer of +0x60 = 10 in the whole of `src/`
// is FUN_0029B628 itself, re-entering. It is ported because it is in the table
// and because +0x1BE, the mast-section counter it owns, is read nowhere else.
//
// The move rotation is DAT_00325E28, eighteen entries cycling:
// 4, 5, 6, 5, 3, 5, 6, 8, 6, 4, 5, 8, 5, 8, 3, 5, 6, 9. FUN_0029C468 walks it,
// and it is also where "the player is down" (state 2) and "I am dead" (state 12)
// override the pick.
//
// -------------------------------------------------------- the animatic contract
//
// The scene's object script is a `work[0]` state machine in beats of ten, and
// beats 10 and 20 are the handshake with this boss:
//
//   case 10   wait until the boss's +0x60 is non-zero -- that is, until state 0
//             has run and left it in state 1
//   case 20   opcode 0xBD method 0x6F: request action 12 on the entity in
//             work[2], which FUN_00299590 turns into +0x94 = 12, +0x60 = 13
//   case 30   wait on script work word 1, which only FUN_0029C510 writes
//
// So with no behaviour at all the port sat in beat 10 for ever, and the player
// -- whom beat 0 had already dropped at (-5.2, -2.3, 2.25), over a part of the
// map with no floor -- fell until the ground query bottomed out at -45. That is
// the whole of "Orphen falls into nothing and the scene softlocks".
//
// ------------------------------------------------------------- Orphen's half
//
// **The intro flies the player, not the boss.** FUN_0029C198 builds two natural
// cubic splines out of DAT_0034EB60 with the player's own position as the first
// control point, and for 2 x 4800 ticks it writes pool slot 0's +0x20/+0x24/
// +0x28 straight from the curve, its facing from the tangent, and swaps its
// animation from 12 to 13 at the halfway mark. That is the leap up the rigging
// to the crow's nest. FUN_0029D658 is the same move again for the fight proper,
// driven by the work block's mode byte rather than by the state.
//
// Verified against a PCSX2 save state of the real transition: the boss binds
// its record and reaches state 1 on the frame the pool reports it, the script
// asks for action 12 the frame after, and the curve's last control point --
// (-6.175, -0.894, 12.599) in the table -- is where slot 0 is standing once the
// carry ends, grounded onto the crow's nest at 12.4.
//
// ------------------------------------------------------- the body is nine bones
//
// FUN_0029C7A8 is the tail call every flight state ends on, and **all nine body
// bones get the same pair of angles**: the lag between where the head is
// actually pointing and where the entity says it is facing, yaw times eight and
// pitch times ten. The rotation compounds down the chain, which is what makes
// the body arc rather than kink. FUN_0029CA28 is the same walk with both fields
// zeroed, which every state opens with.
//
// That is a different nine from FUN_0029CCB8's. The bones the *segments* ride
// are DAT_0034EB30 (8, 3, 1, 0x12, 0x18, 0x1A, 0x1C, 0x1E, 0x20); the bones the
// body bends through are DAT_0034EB40 (0x13, 0x19..0x20).
//
// ---------------------------------------------------- the player is parked
//
// FUN_0029D658's mode 1 writes **0x0B into the player's battle control block
// +0x0E** before it stages the curve, and that write is load-bearing rather
// than cosmetic: it stops the player's own state machine running, and the first
// thing that machine does is reset +0x62 -- which is the very field the carry
// uses as its timer. Without it the curve never advances, state 9 waits on the
// carry for ever at phase 11, and the fight stops. The port reached that
// exactly once and it is why the write is no longer a comment.
//
// ------------------------------------------------------------- what is here
//
// All fourteen states, the wrapper, the action path, the move rotation, all
// four mode-14 helpers, FUN_0029D658's modes 1, 3 and 9, both spline builders,
// the body bend, the segment carry, the shed streaks and their trails, the
// thirty-piece debris pool, the beam chain and its blast, the mast-section
// break, the white-out, and all twelve shots of the camera director
// (original_mast_camera.h) -- though a fight driven by the move rotation only
// reaches eight of them, because shots 3, 4, 10 and 12 belong to states 10 and
// 12.
//
// Not here:
//
//   * **type 0x1AE, the wash a strafing run leaves.** Its own dispatch entry is
//     FUN_002EDC40, which the port does not have, so the entity is spawned and
//     then never expires -- fifteen of them accumulate over a long fight.
//   * FUN_0023BBD8 everywhere, the pad rumble. The port has no rumble path.
//
// Two blocks are ported but unreachable in the retail build, and are marked as
// such where they sit: state 12's cue 0x13D, which the original gates on a
// **null-pointer read of address 6**, and state 9 phase 12's `+0x1BF > 2` arm,
// which the guard at the top of state 9 keeps out of reach.

#include "ported/entity/actor_frame_update.h"
#include "ported/entity/actor_trace.h"
#include "ported/entity/original_entity.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace orphen::ported::entity
{

  inline constexpr std::int32_t kMastBossTypeId = 0x95;
  inline constexpr std::uint32_t kFUN_00299390_mastBoss = 0x00299390;

  // PTR_FUN_00325E50. Fourteen entries; the float run at 0x00325E88 starts
  // immediately after, which is what pins the count.
  inline constexpr std::uint32_t kPTR_FUN_00325E50_mastBossStates = 0x00325E50;
  inline constexpr std::size_t kMastBossStateCount = 14;

  // Type 0xBF, the nine body segments. Its dispatch entry is FUN_00239E78, the
  // shared no-op -- they have no behaviour of their own at all and are moved
  // entirely by the boss, out of FUN_0029CCB8. FUN_00265E28 builds them as type
  // 0xC0 and state 0 rewrites +0x00 afterwards, so they keep 0xC0's model.
  inline constexpr std::int32_t kMastSegmentTypeId = 0xBF;
  inline constexpr std::int32_t kMastSegmentSpawnTypeId = 0xC0;
  inline constexpr std::size_t kMastSegmentCount = 9;

  // How many frames the boss has spent in a move DAT_00325E28 picked and this
  // port has no handler for, and which state that was. The actor report prints
  // both, because a boss standing still is otherwise indistinguishable from one
  // that is waiting on purpose.
  std::uint32_t FUN_0029c468_unported_move_frames();
  std::uint16_t FUN_0029c468_unported_move_state();

  // FUN_002EDC40, type 0x1AE -- the wash the strafing run fires. It has no
  // src/ file; the dispatch word is at 0x0031CDD4. Dispatched from
  // actor_frame_update rather than from the boss, because the wash outlives
  // the pass that made it.
  void FUN_002edc40_mast_wash_entry(OriginalEntity &entity,
                                    std::size_t slot,
                                    const ActorEnvironment &environment);

  // FUN_00299390.
  void FUN_00299390_mast_boss(OriginalEntity &entity,
                              std::size_t slot,
                              const ActorEnvironment &environment,
                              ActorTrace &trace);

} // namespace orphen::ported::entity

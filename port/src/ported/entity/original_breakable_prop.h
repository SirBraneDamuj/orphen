#pragma once

// The map-streamed prop behaviour, and what a prop leaves behind when it
// breaks. s03_e001's trees are the first scene in the port that needs it: the
// "forest fire" cutscene (script 0x3F5C..0x40F0) runs opcode 0xD9 on each tree
// to make it something fire damage can burn down.
//
//   src/FUN_002cfe08.c  0x002CFE08  every streamed type's handler (ids 0x272..,
//                                   0x373.., 0x474..), no table between
//   src/FUN_002d0058.c  0x002D0058  the break: pieces, a light, state 3
//   0x002CFED8 / 0x002CFEE0         states 0 and 1, bare `jr ra; nop`
//   src/FUN_002cfee8.c  0x002CFEE8  state 2, the push
//   src/FUN_002cffa8.c  0x002CFFA8  state 3, the wait before the remnant
//   src/FUN_002d04e0.c  0x002D04E0  type 0x45, the remnant
//   0x002D05C0                      type 0x47, one piece. No src/ file; eight
//                                   instructions, quoted below.
//
// PTR_LAB_003266D0 has ten states. 4..9 (0x002D18D8 .. 0x002D1BC0) are the
// save point's -- FUN_002D1A40 opens the save screen on Cross -- and nothing in
// s03_e001 enters them: every streamed prop spawns in state 0 and the break
// break only ever writes 3 and 1. They are not ported; a prop found in one is
// reported by the actor trace as an unported state rather than run.
//
// ---------------------------------------------------------------- the damage
//
// A prop takes damage only once opcode 0xD9 has given it a break table at
// +0x1A8 and the +0x02 bit 0x2000 a player-side attack looks for. FUN_00216140
// then leaves the damage in +0xBE and the attack's element bits in +0xC2, and
// the wrapper here walks the table:
//
//   for each 0x24-byte record until a 0xFFFF halfword:
//     if +0xC2 has bit (1 << record[0]):
//       +0x12A -= +0xBE                 -- once per matching record
//       if that leaves it <= 0: +0x12A = 0, FUN_002D0058(prop, record),
//                               cue 0x2C5, stop
//   +0xC2 = 0, +0xBE = 0
//
// Damage of any other element is thrown away. s03_e001's one record is element
// 4, fire.
//
// --------------------------------------------------------------- the break
//
// FUN_002D0058 spawns record+0x04 entities of type record+0x02 at the offsets
// listed record+0x20 bytes past record+0x20, each a fixed-point triple over
// 100000. Every piece gets +0x198 = the prop, +0x04 |= 0x19, the ground under
// it in +0x4C, scale (rand % 25 + 50 + record+0x08) / 100, one FUN_00225C90
// step, and a random timeline cursor of 0, 2, 4 or 6 so the pieces do not
// animate in step. Then, when record+0x0C is non-zero, a light from
// FUN_00266008 in that colour, radius twice the prop's +0x54 (at least 2), at
// the prop plus record+0x14/+0x18/+0x1C over 100000 -- read as *unsigned*.
// Last, state 3 with +0x62 = record+0x06 << 5, +0x04 |= 0x10 so the hit tests
// skip it from now on, +0x1AC = record+0x10 and +0x1B0 = record+0x12 / 100.
//
// State 3 counts +0x62 down and, once it is spent and +0x1AC names a type,
// spawns that type on the prop's position and ground at scale +0x1B0, points
// it back at the prop, keys cue 0x2C6 and drops the prop to state 1. In
// s03_e001 that is 0x168 << 5 ticks -- six seconds -- then type 0x45 at 3.0.
//
// Type 0x45 frees itself when its animation finishes, and frees the *prop* on
// the frame its timeline cursor is 4 and its entry expires. Each type 0x47
// piece frees itself once the prop's +0x00 reads 0:
//
//   0x002D05C0  lw   v0, 0x198(a0)
//               beqz v0, ret
//               lh   v0, 0(v0)
//               bnez v0, ret
//               j    FUN_00265EC0       ; a0 is still the piece
//
// So the tree stands, burning, until the remnant reaches its third timeline
// entry (cursor 4; the cursor steps by 2), and
// the pieces go out the frame after it does.

#include "ported/entity/actor_frame_update.h"
#include "ported/entity/original_entity.h"

#include <cstddef>
#include <cstdint>

namespace orphen::ported::entity
{

  // FUN_002CFE08's tail picks the state handler by +0x60. True when it ran
  // one the port has; false for the save point's 4..9.
  bool FUN_002cfe08_streamed_prop(OriginalEntity &entity,
                                  std::size_t slot,
                                  const ActorEnvironment &environment);

  // FUN_002D0058(prop, record). `recordOffset` is a scene-script blob offset.
  void FUN_002d0058_break_prop(OriginalEntity &prop,
                               std::size_t slot,
                               std::uint32_t recordOffset,
                               const ActorEnvironment &environment);

  void FUN_002d04e0_break_remnant(OriginalEntity &entity,
                                  std::size_t slot,
                                  const ActorEnvironment &environment);

  void LAB_002d05c0_break_piece(OriginalEntity &entity,
                                std::size_t slot,
                                const ActorEnvironment &environment);

} // namespace orphen::ported::entity

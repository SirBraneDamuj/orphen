#pragma once

// The type 0x118 status aura -- the icon that spins over a character's head
// while a status is on them.
//
//   src/FUN_002d8ce0.c  0x002d8ce0  the behaviour: spin, then dispatch on +0x19A
//   src/FUN_002d9908.c  0x002d9908  status 1
//   src/FUN_002d90d8.c  0x002d90d8  status 4
//   src/FUN_002d9730.c  0x002d9730  status 5
//   src/FUN_002d9370.c  0x002d9370  statuses 6 and 10
//   src/FUN_002d8e30.c  0x002d8e30  status 9, the poison
//   src/FUN_002d9558.c  0x002d9558  status 12, the confusion
//
// One of these is built per party member alongside its other effects and parked
// hidden at DAT_0031DA7C; FUN_002d8b38 is what raises it (battle_character_
// update.cpp), and it is also what sets the member's bit in DAT_0031DA6C.
// **This is the only thing that clears that bit again.** Without it a status,
// once inflicted, lasts for the rest of the battle -- the icon never leaves the
// character's head and the command input goes on reading a confusion that
// should have worn off ten seconds ago.
//
// The six bodies are one body with a different light colour: put the icon on
// the victim's head, drag the light along with it, run +0x62 down at two ticks
// a frame, and when it reaches zero shrink by a tenth a frame until the icon is
// under four tenths of its size, then hide and clear the bit. Two of them do
// something on top:
//
//   4 and 9  drain a hit point off the victim per animation loop, and arm the
//            *lower* health bar with it. That is the poison ticking, and it is
//            the first live caller the port has for the player's own bar. It
//            stops at five points, so a status never kills.
//   6 and 10 cancel themselves the moment the victim's hit points drop below
//            what they were when the status landed -- being hit shakes it off.

#include "ported/entity/actor_frame_update.h"
#include "ported/entity/original_entity.h"

#include <cstddef>
#include <cstdint>

namespace orphen::ported::entity
{

  inline constexpr std::int32_t kStatusAuraTypeId = 0x118;
  inline constexpr std::uint32_t kFUN_002d8ce0_statusAura = 0x002D8CE0;

  void FUN_002d8ce0_status_aura(OriginalEntity &aura,
                                std::size_t slot,
                                const ActorEnvironment &environment);

} // namespace orphen::ported::entity

#pragma once

// The "SORCEROUS STABBER ORPHEN" battle logo: entity type 0x49 in pool slot 5.
//
//   src/FUN_00264fa0.c  opcode 0xDF, a bare call to FUN_0025d5b8
//   src/FUN_0025d5b8.c  spawn it into slot 5 and select it
//   src/FUN_00264fc0.c  opcode 0xE0, release it
//   src/FUN_002255b8.c  pin it six units in front of the camera, every frame
//   LAB_00239fb8        its actor handler (PTR_FUN_0031c6c0[0x48]); Ghidra has
//                       no function there, so it was read off SLUS_200.11
//
// == It is a model, not an overlay ==
//
// The descriptor (DAT_00318b68 + 0x48 * 0x1C) names model record 0x48 of
// DAT_0031ee48: grp_0183 with tex_0178 bound statically to texture slot 0x2A.
// FUN_002255B8 drags it to the camera eye plus six units of the view forward,
// turned to the camera's yaw, pitch and roll, so it reads as a flat card filling
// the middle of the screen while staying an ordinary 3D draw.
//
// == How the scripts use it ==
//
// The JP scripts call 0xDF 120 times; the US scripts keep 13 of those, each
// inside `if (0 == 1)`, and deleted the other 85 outright. A call is always
// followed by `reg(current, 8) = 1` -- animation 1 -- which is what lets the
// actor handler run at all. JP's generic encounters also call it once early
// with `reg(5, 4) |= 1` (+0x08 bit 0, hidden) and `reg(5, 3) |= 0x4000`
// (+0x04, suspended): a preload, released by `destroy(5)` before the real one.
// No script in either version uses 0xE0. --battle-logo puts all 98 back at
// script load (PortRuntime::restoreDisabledBattleLogo).

#include "ported/entity/original_entity.h"
#include "ported/entity/original_smoke_cloud.h"

#include <cstddef>
#include <cstdint>

namespace orphen::ported::entity
{
  class EntityPool;
  class EntityDescriptorTable;
  struct ActorEnvironment;

  // FUN_0025d5b8: `DAT_00355044 = &DAT_0058c7e8`, which is 0x0058BEB0 + 5 * 0x1D8.
  inline constexpr std::size_t kBattleLogoSlot = 5;
  inline constexpr std::int16_t kBattleLogoTypeId = 0x49;
  // FUN_0025d5b8:9, `FUN_00229ef0(0x41400000, ...)`.
  inline constexpr float kBattleLogoScale = 12.0f;
  // FUN_0025d5b8:8, +0x08 |= 0x40 -- sort bucket 0x1005, drawn over the scene.
  inline constexpr std::uint16_t kBattleLogoFlags08 = 0x0040;
  // FUN_00264fc0 parks the type word here so FUN_0025d5b8's `< 1` test respawns
  // it, and FUN_00224798 reads it as "the logo has gone".
  inline constexpr std::int16_t kBattleLogoReleased = -2;

  // FUN_002255b8:5-7. Six units down the view forward, and in height the eye
  // plus DAT_0058c0e0 - DAT_0035241c (0.6 - 0.2) less a further 1.5. The first
  // is kDAT_0058c0e0_cameraHeight, shared with the smoke cloud.
  inline constexpr float kBattleLogoDistance = 6.0f;
  inline constexpr float kDAT_0035241c_heightBias = 0.2f;
  inline constexpr float kBattleLogoDrop = 1.5f;

  // FUN_0025d5b8. Returns the slot, which the caller makes the current
  // selection. Respawns only when slot 5's type word is not positive, so a
  // second call while the logo is up selects it again and nothing more.
  std::size_t FUN_0025d5b8_spawn_battle_logo(EntityPool &pool, const EntityDescriptorTable &descriptors);

  // FUN_002255b8's type 0x49 branch. `forward` is DAT_0058bea0, the unit
  // vector from the eye to the look-at point.
  void FUN_002255b8_place_battle_logo(OriginalEntity &logo,
                                      float eyeX, float eyeY, float eyeZ,
                                      float forwardX, float forwardY, float forwardZ,
                                      float yaw, float pitch, float roll);

  // LAB_00239fb8, the actor handler. Idle until the script sets animation 1,
  // then: fade in from 3 by 8 a frame, hold until the animation timeline
  // passes entry 9 (+0xA8 >= 0x12), fade out from 0x7F by 8 a frame, and
  // release itself. It also releases itself the frame the animation reports
  // finished (+0x06 bit 0). +0x198 holds the phase: 1 fading in, 0 holding,
  // 2 fading out.
  void LAB_00239fb8_battle_logo(OriginalEntity &logo, std::size_t slot, const ActorEnvironment &environment);

} // namespace orphen::ported::entity

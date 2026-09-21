#pragma once

// The title screen: scene s12_e010 seen through scene module 12.
//
//   src/FUN_002000c0.c  the boot path, which names the scene
//   src/FUN_0022a360.c  picks the module out of PTR_LAB_003252B8
//   src/FUN_00271220.c  module 12: mode 3 builds the shot, mode 4 runs a state
//                       machine through PTR_FUN_003256F8
//   src/FUN_00271470.c  state 0, one frame: start the music, clear the timers
//   src/FUN_00271558.c  state 1, the title screen proper
//   src/FUN_00272010.c  the camera orbit
//   src/FUN_00272100.c  the "Press START button" sprite
//
// == There is no separate title mode ==
//
// The game mode word DAT_00354D2C reads 0 on the title screen -- verified
// against a running PCSX2 at the prompt -- so this is an ordinary field frame.
// FUN_002000C0:163-165 names the scene outright:
//
//     DAT_003551F4 = 0xC;  DAT_003551F0 = 10;  DAT_003551EC = 0x2001;
//
// which is section 12 entry 10, loaded with the same request bits a cold boot
// into any scene uses. Everything that makes it a title screen -- the orbit,
// the logo, the prompt -- is scene module 12's, and FUN_0022A418:371 even
// skips the navmesh build for `DAT_003551F4 == 0xC`.
//
// == The logo is an entity, not an overlay ==
//
// FUN_00271220's mode 3 spawns entity type 0x48 into the record at 0x0058C7E8,
// which is pool slot 5, and parks it at height DAT_00352EC4 = 1.6 over the
// pedestal. Its actor handler is FUN_00239E78, the no-op -- it is a billboard
// and nothing more. Two 300-frame-apart PCSX2 screenshots put the logo on
// identical pixels while the room rotates behind it, because the camera holds
// a constant radius and pitch and the billboard turns with it.
//
// == The prompt is one sprite off a map page ==
//
// FUN_00272100 draws the FUN_00239020 entry at 0x00325738, which is texture
// slot 5 -- an ordinary map texture page, 0x0103 for this scene, whose top
// 256x40 band is the words "Press START button". The rest of that page is the
// copyright block the legal screen uses.

#include "ported/text/original_dialogue_text.h"

#include <cstdint>

namespace orphen::ported::scene
{

  // FUN_0022A360's index into PTR_LAB_003252B8 for this scene. Read back from a
  // running PCSX2 at the prompt: DAT_0032536C held 0x00271220, table slot 12.
  inline constexpr int kTitleSceneModule = 12;

  // FUN_002000C0:163-164, the scene the boot path names.
  inline constexpr std::uint16_t kTitleSceneSection = 12;
  inline constexpr std::uint16_t kTitleSceneEntry = 10;

  // FUN_00271220:37. The billboard, and the pool slot the module builds it in:
  // 0x0058C7E8 is 0x0058BEB0 + 5 * 0x1D8.
  inline constexpr std::int32_t kLogoTypeId = 0x48;
  inline constexpr std::size_t kLogoSlot = 5;
  // DAT_00352EC4, the height it is parked at, written to the entity's +0x28.
  inline constexpr float kLogoHeight = 1.6f;
  // The three flag writes that follow the spawn (FUN_00271220:38-47). +0x04
  // bit 0x100 is the "no physics" gate FUN_002262C0 tests first, so the
  // billboard keeps the height rather than falling to the floor.
  inline constexpr std::uint16_t kLogoFlags04 = 0x0100;
  inline constexpr std::uint16_t kLogoFlags08 = 0x0040;
  inline constexpr std::uint16_t kLogoFlags06 = 0x0080;
  // FUN_00271220:41 and :48, the two event flags the mode-3 hook consults.
  inline constexpr std::uint16_t kReturnedFromGameFlag = 0x511;
  inline constexpr std::uint16_t kLogoSeenFlag = 0x000E;

  // FUN_00272010:20, `fGpffff8f58`. Radians of orbit per frame tick, so at the
  // nominal 0x20 ticks a revolution takes 2*pi / (0x20 * 0.0001875) ~= 1047
  // frames, about 17 seconds.
  inline constexpr float kOrbitRadiansPerTick = 0.0001875f;
  // DAT_00352EC0, the pi FUN_00271220:35 adds to the camera yaw to point the
  // kneeling actor away from the camera.
  inline constexpr float kFacingBias = 3.141592025756836f;

  // FUN_00271470:11, the music slot state 0 starts, and its fader.
  inline constexpr std::size_t kTitleMusicSlot = 1;
  inline constexpr int kTitleMusicFader = 1000;

  // FUN_00271558:24, `uGpffffb686 & 0x840` -- START or Cross leaves the title.
  inline constexpr std::uint16_t kStartOrConfirmMask = 0x0840;

  // FUN_00271558:41. The idle timer at the entity's +0x198 accumulates frame
  // ticks and hands the screen to the attract demo past this, which is ~1800
  // frames at the nominal 0x20.
  inline constexpr std::int32_t kAttractTimeout = 0xE101;

  // The FUN_00239020 entry at 0x00325738, dumped from SLUS_200.11:
  //
  //   [0] 0x00000005  texture, a plain 8-bit slot (no CLUT bank below 0x18)
  //   [1] 0xFFFFEFFA  the -0x1006 sort bucket
  //   [2] 0xFFFFFF80  entry x, -128
  //   [3] 0xFFFFFF90  entry y, -112  (FUN_00239020 negates it)
  //   [4] 0x00000100  width  256
  //   [5] 0x00000028  height  40
  //   [8] 0x00000100  source width  256
  //   [9] 0x00000028  source height  40
  //   [12] 0x80808080 colour, plain white at the GS's x1.0
  //
  // screenX = entryX + 320 and screenY = 224 - entryY, the same 640x448 space
  // the subtitles land in -- see original_dialogue_text.h.
  inline text::DialogueSprite FUN_00272100_prompt_sprite()
  {
    text::DialogueSprite sprite;
    sprite.textureSlot = 5;
    sprite.clutBank = -1;
    sprite.x = -128 + 320;
    sprite.y = 224 - (-112);
    sprite.width = 0x100;
    sprite.height = 0x28;
    sprite.u = 0;
    sprite.v = 0;
    sprite.sourceWidth = 0x100;
    sprite.sourceHeight = 0x28;
    sprite.color = text::kColorDefault;
    return sprite;
  }

} // namespace orphen::ported::scene

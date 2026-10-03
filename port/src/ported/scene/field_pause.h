#pragma once

// The field pause: Start in the field, game mode 2.
//
//   src/FUN_00224ff0.c  the field frame's gate. `uGpffffb686 & 0x800` -- Start
//                       newly pressed -- ducks music slots 0 and 1, raises the
//                       mode and plays cue 2. In mode 2 the same press puts
//                       the music back and returns 1
//   src/FUN_00224320.c  PTR_FUN_00318A88[2], the paused frame
//   src/FUN_00224218.c  PTR_FUN_00318A88[0], which the unpause runs at once
//
// == The gate ==
//
// FUN_00224FF0's guards come in two groups. The first -- event flag 0x508, the
// cinematic bars, a scene change in flight, anything in pool slot 5, the boot
// and title sections, the death latch -- applies to everything the function
// does. The second -- battle, the lead's +0x60, a dialogue or item window --
// applies only to the field menu and the area map, which are tested after
// Start. So Start pauses mid-action and with a dialogue up.
//
// Start or Cross takes the second branch; only Start does anything there
// outside the attract demo. The mode is 2 unless DAT_003555C7 (the debug byte)
// or DAT_003555D3 (a section-14 scene) is set, which gives mode 1:
// 0x00224268, not in src/, builds the battle pause panel at 0x005609C0 and
// sets mode 2 itself, so the battle pause draws FUN_00225340 in place of the
// caption below. See battle_pause.h.
//
// == The paused frame ==
//
// FUN_00224320: FUN_00224FF0 first; when it returns non-zero (Start, with the
// first group of guards passing) FUN_002241D8 and FUN_00224218, so the frame
// the pause ends on is a whole field frame's draw tail with FUN_002261E0 and
// FUN_002192C0 in it. Otherwise the 0x50 black dim, message 0x25 centred at
// y 0xB, the bars, and FUN_00225C20 followed by the draw tail with no
// FUN_002192C0 -- so unlike the field menu the effect pools stop too. Nothing
// is simulated.
//
// FUN_002239C8:190 does not add the frame tick to the play clock uGpffffb6c8
// in mode 2, so paused time is not play time.

#include <cstdint>

namespace orphen::ported::scene
{

  // DAT_00354D2C.
  inline constexpr int kGameModePauseMenu = 1;
  inline constexpr int kGameModePause = 2;

  // uGpffffb686, raw pressed.
  inline constexpr std::uint16_t kPausePadStart = 0x0800;

  // FUN_00224FF0:12-49, the guards every branch shares.
  inline constexpr int kPauseBlockedEventFlag = 0x508;
  // DAT_0058C7E8, pool slot 5's +0x00: a positive type blocks the gate.
  inline constexpr int kPauseBlockingSlot = 5;
  // iGpffffb284: sections 0, 0xC and 0xD never pause.
  inline constexpr int kPauseBlockedSectionBoot = 0x0;
  inline constexpr int kPauseBlockedSectionTitle = 0xC;
  inline constexpr int kPauseBlockedSectionD = 0xD;
  // iGpffffb288 in a battle.
  inline constexpr int kPauseBlockedBattleEntry = 0x1F;

  // FUN_00224FF0:122-142: music slots 0 and 1, ramped to half at speed 0x19,
  // and back to the saved fader at the same speed. gp-0x43A8 holds the saved
  // two, -1 for a slot that was stopped or already ramping.
  inline constexpr int kPausedMusicSlots = 2;
  inline constexpr int kPauseMusicSpeed = 0x19;
  inline constexpr int kPauseMusicFader = 500;

  // FUN_002256B0, `FUN_00267D38(2, 0)`.
  inline constexpr int kPauseCue = 2;

  // FUN_00224320:15, `FUN_0025D0E0(0x50000000, 1)`.
  inline constexpr std::uint32_t kPauseDimColour = 0x000000;
  inline constexpr std::uint8_t kPauseDimAlpha = 0x50;

  // FUN_00224320:17-19: message 0x25, measured at 0x28 and drawn at
  // (-width / 2, 0xB) in 0x28 x 0x2C cells.
  inline constexpr int kPauseCaptionMessage = 0x25;
  inline constexpr int kPauseCaptionY = 0xB;
  inline constexpr int kPauseCaptionCellWidth = 0x28;
  inline constexpr int kPauseCaptionCellHeight = 0x2C;

} // namespace orphen::ported::scene

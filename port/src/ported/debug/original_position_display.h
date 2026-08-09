#pragma once

// POSITION_DISP, the debug menu's position readout.
//
//   src/FUN_002239c8.c  the frame function; lines 140-165 are the whole
//                       feature, gated on cGpffffb128 (DAT_00355098)
//   src/FUN_00268d30.c  the debug menu entry that toggles it -- the
//                       "ON :POSITION DISP" row at 0x0034D5C0
//   src/FUN_00269fa8.c  print an entity's +0x20 position triple
//   src/FUN_0026a048.c  print a loose position triple
//
// There are two readouts and the original picks between them on the state of
// the debug gates:
//
//   cGpffffb66a == 0 || cGpffffb66c != 0   the detailed one
//   otherwise                              the compact one
//
// cGpffffb66a is DAT_003555da, the debug-active byte, and cGpffffb66c is
// DAT_003555dc, the output gate FUN_002681c0 checks. So the detailed readout
// is what shows once debug output is on; the compact branch is the case where
// debug is active but output is off, and it forces the gate on for the two
// lines it prints and then back off, which is why it is the one you get by
// default.
//
// Both scale positions by 1000 and truncate (FUN_0030bd20).

#include "ported/debug/original_debug_text.h"

#include <cstdint>

namespace orphen::ported::debug
{

  // Everything the two readouts read, with the original's addresses. The
  // entity fields are the lead player's, at DAT_0058beb0.
  struct PositionDisplayState
  {
    // DAT_0058bed0 / +0xD4 / +0xD8 -- the lead player entity's +0x20 triple.
    float DAT_0058bed0_playerX = 0.0f;
    float DAT_0058bed4_playerY = 0.0f;
    float DAT_0058bed8_playerZ = 0.0f;

    // The lead player's flag words, in the order the MF/AF/SF/NF line prints
    // them rather than in address order.
    std::uint32_t DAT_0058bebc_moveFlags = 0;  // entity +0x0C
    std::uint16_t DAT_0058beb4_attrFlags = 0;  // entity +0x04
    std::uint16_t DAT_0058beb8_stateFlags = 0; // entity +0x08
    std::uint16_t DAT_0058beb6_nowFlags = 0;   // entity +0x06

    // DAT_0058be90..98, the camera's look-at point.
    float DAT_0058be90_targetX = 0.0f;
    float DAT_0058be94_targetY = 0.0f;
    float DAT_0058be98_targetZ = 0.0f;

    // The camera entity at 0x0058C088 -- pool slot 1 -- and its +0x20 triple
    // at DAT_0058c0a8.
    float DAT_0058c0a8_cameraX = 0.0f;
    float DAT_0058c0ac_cameraY = 0.0f;
    float DAT_0058c0b0_cameraZ = 0.0f;

    // iGpffffb284 / uGpffffb280, which are DAT_003551f4 and DAT_003551f0: the
    // loaded map's section and entry. s01_e024 holds 1 and 24, so the line
    // reads MP0124.
    int iGpffffb284_mapSection = 0;
    int uGpffffb280_mapEntry = 0;

    // cGpffffb663 / DAT_003555d3. Set for the scenes that report a BG number
    // instead of a map number; the compact readout's other branch.
    bool cGpffffb663_backgroundScene = false;

    // The BG number that branch prints, via FUN_0022a238(0xD) indexed by
    // iGpffffb288. Neither is ported, so the branch prints what it is given.
    std::uint16_t backgroundId = 0;
  };

  // FUN_00269fa8 / FUN_0026a048. Both are "(%d, %d, %d)\n" of the triple
  // scaled by 1000; the first takes an entity and reads its +0x20.
  void FUN_0026a048_printPosition(DebugTextBuffer &buffer, float x, float y, float z);

  // FUN_002239c8 lines 142-149: the readout that shows once debug output is
  // on. Five lines -- the player's position, its flag words, the camera's
  // look-at, the camera's eye, and the map number.
  void emitDetailedPositionDisplay(DebugTextBuffer &buffer, const PositionDisplayState &state);

  // FUN_002239c8 lines 151-163: the two-line readout, right-aligned on the
  // bottom line by the '~' escape. Forces the output gate on around itself,
  // exactly as the original does.
  void emitCompactPositionDisplay(DebugTextBuffer &buffer, const PositionDisplayState &state);

  // FUN_002239c8 line 140 onward, including the branch between the two.
  void FUN_002239c8_emitPositionDisplay(DebugTextBuffer &buffer, const PositionDisplayState &state);

} // namespace orphen::ported::debug

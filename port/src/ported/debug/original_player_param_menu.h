#pragma once

// SET PLAYER PARAM, the debug menu page that edits the lead's combat numbers.
//
//   src/FUN_0026bc50.c  the whole page: draws three rows, moves a cursor over
//                       them and steps the row under it
//
// The rows come from a pointer table at 0x0034E228 and their labels from
// 0x0031ED78, stride 0x20 -- both read out of SLUS_200.11:
//
//   0x0058BFDA  "HP :%3d"   pool slot 0 +0x12A, hit points
//   0x0058BFDC  "STR:%3d"   pool slot 0 +0x12C, attack power
//   0x0058BFDE  "DEF:%3d"   pool slot 0 +0x12E, defence
//
// Slot 0 is the lead (the pool starts at 0x0058BEB0). Every one of the three is
// live in the port: +0x12C seeds each attack the lead throws and +0x12E is the
// defence FUN_00216140 subtracts from an incoming hit (original_hit_test.cpp).
//
// The pad bits the page reads off uGpffffb684, and what each does to the row
// under the cursor:
//
//   0x2000  Right  +1,  past 999 wraps to 0
//   0x8000  Left   -1,  below 0 wraps to 999
//   0x0008  R1     +10, past 999 lands on old - 989
//   0x0004  L1     -10, below 0 lands on old + 989
//
// Up and Down (0x1000 / 0x4000) move the cursor over the three rows. After the
// pad, on every call whether anything moved or not, +0x128 (maximum hit points)
// takes +0x12A, or 1 when that is not positive -- so editing HP sets both.

#include "ported/entity/original_entity.h"

namespace orphen::ported::debug
{

  enum class PlayerParam
  {
    HitPoints, // +0x12A
    Strength,  // +0x12C
    Defence,   // +0x12E
  };

  enum class PlayerParamStep
  {
    Up1,    // 0x2000
    Down1,  // 0x8000
    Up10,   // 0x0008
    Down10, // 0x0004
  };

  // One FUN_0026bc50 call with that one pad bit pressed and the cursor on
  // `param`: the step, then the +0x128 copy.
  void FUN_0026bc50_step_player_param(orphen::ported::entity::OriginalEntity &lead,
                                      PlayerParam param,
                                      PlayerParamStep step);

} // namespace orphen::ported::debug

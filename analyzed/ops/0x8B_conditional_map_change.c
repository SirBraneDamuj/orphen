// Opcode 0x8B — conditional map change (the trigger-volume warp)
// Original: FUN_00260e30 (0x00260e30)
//
// CORRECTION
// - This file previously described 0x8B as "trigger_positional_audio_with_coords".
//   It is not audio. The call it makes, FUN_0022b2c0, is the map-change request:
//   it stages a destination position and destination map ids and raises the
//   pending bit that drives the scene bootstrap FUN_0022a418.
//   See analyzed/map_bootstrap_sequence.c.
// - 0x8B is the *conditional* form of the warp: a gate tested against the lead
//   player's flags, which is what a floor trigger or doorway needs.
//
// Behavior
// - Evaluates seven expressions, in stream order:
//     1. mask        tested against DAT_0058bf1c (entity slot 0 +0x6C)
//     2. majorMap
//     3. minorMap
//     4. flags
//     5. x  \
//     6. y   >  divided by fGpffff8cb4 before staging
//     7. z  /
// - Gating, in order:
//     * iGpffffb27c != 0            -> return 0 (a map change is already pending)
//     * (DAT_0058bf1c & mask) == 0  -> return 0 (the lead does not match)
//     * (flags & 0x10000) == 0 and DAT_0058bebc bit 0 set -> return 0
//       (DAT_0058bebc is entity slot 0 +0x0C, the collision flags; bit 0 is the
//       grounded bit, so 0x10000 means "fire even while airborne")
// - When it does fire, the same two side-effect bits as 0x8C apply:
//     flags & 0x200000 -> FUN_0025d610()
//     flags & 0x000002 -> FUN_0025d1c0(1, 0xC, 0) and DAT_0058bf10 = 10
// - Returns 1 when the warp was requested, 0 otherwise. Scripts branch on this.
//
// Related
// - analyzed/ops/0x8C_request_map_change.c: the unconditional form, six
//   expressions, no mask, no return value.
// - FUN_0022b2c0: request_map_change(x, y, z, majorMap, minorMap, flags).
// - DAT_0058bf1c: entity slot 0 +0x6C, the flag word the mask selects against.
// - DAT_0058bebc: entity slot 0 +0x0C, collision flags.
//
// PS2 notes
// - Both warp opcodes read the *lead player's* state directly out of pool slot 0
//   rather than through a selected-entity pointer, which is consistent with
//   these being player-triggered transitions rather than generic entity events.
//
// Unresolved callees keep their original labels.

#include <stdint.h>

extern void FUN_0025c258(void *destination); // evaluate one expression
extern void FUN_0022b2c0(float x, float y, float z,
                         uint32_t majorMap, uint32_t minorMap, uint32_t flags);
extern void FUN_0025d610(void);
extern void FUN_0025d1c0(int, int, int);

extern int iGpffffb27c;       // non-zero while a map change is already pending
extern float fGpffff8cb4;     // script integer -> world float scale
extern uint32_t DAT_0058bf1c; // entity slot 0 +0x6C: flag word the mask tests
extern uint32_t DAT_0058bebc; // entity slot 0 +0x0C: collision flags
extern uint16_t DAT_0058bf10; // entity slot 0 +0x60: lead state

// NOTE: Original signature: undefined4 FUN_00260e30(void)
uint32_t opcode_0x8B_conditional_map_change(void)
{
  uint32_t mask = 0;
  uint32_t majorMap = 0;
  uint32_t minorMap = 0;
  uint32_t flags = 0;
  int32_t x = 0;
  int32_t y = 0;
  int32_t z = 0;

  FUN_0025c258(&mask);
  FUN_0025c258(&majorMap);
  FUN_0025c258(&minorMap);
  FUN_0025c258(&flags);
  FUN_0025c258(&x);
  FUN_0025c258(&y);
  FUN_0025c258(&z);

  if (iGpffffb27c != 0)
  {
    return 0;
  }
  if ((DAT_0058bf1c & mask) == 0)
  {
    return 0;
  }
  if ((flags & 0x10000) == 0 && ((DAT_0058bebc ^ 1) & 1) != 0)
  {
    return 0;
  }

  if ((flags & 0x200000) != 0)
  {
    FUN_0025d610();
  }
  if ((flags & 2) != 0)
  {
    FUN_0025d1c0(1, 0xC, 0);
    DAT_0058bf10 = 10;
  }

  FUN_0022b2c0((float)x / fGpffff8cb4,
               (float)y / fGpffff8cb4,
               (float)z / fGpffff8cb4,
               majorMap, minorMap, flags);
  return 1;
}

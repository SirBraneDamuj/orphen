// Opcode 0x8C — request_map_change (unconditional)
// Original: FUN_00260f78 (0x00260f78)
//
// CORRECTION
// - This file previously described 0x8C as "trigger_positional_audio_simple",
//   on the assumption that FUN_0022b2c0 was a spatial-audio call taking XYZ.
//   It is not. FUN_0022b2c0 is the map-change request: it stages a destination
//   position and destination map ids in globals and raises the pending bit that
//   the main loop (FUN_002239c8) watches to run the scene bootstrap
//   FUN_0022a418. See analyzed/map_bootstrap_sequence.c.
// - 0x8C is therefore a scripted warp: "go to map (major, minor), arriving at
//   (x, y, z)". The three values previously read as "audio params" are
//   majorMap, minorMap and flags.
//
// Behavior
// - Evaluates six expressions, in stream order:
//     1. majorMap    -> DAT_003551f4
//     2. minorMap    -> DAT_003551f0
//     3. flags       -> DAT_003551ec (OR 1 by the callee)
//     4. x  \
//     5. y   >  divided by fGpffff8cb8 before staging
//     6. z  /
// - Does nothing at all when iGpffffb27c != 0, i.e. when a map change is
//   already pending. Warps do not queue.
// - Before requesting, two flag bits act on the current scene:
//     flags & 0x200000 -> FUN_0025d610()
//     flags & 0x000002 -> FUN_0025d1c0(1, 0xC, 0) and DAT_0058bf10 = 10
//                         (entity slot 0 +0x60, the lead's state field)
// - Returns 0.
//
// Contrast with 0x8B (analyzed/ops/0x8B_conditional_map_change.c), which takes
// an extra leading mask expression and only fires when the lead's flags match --
// that is the trigger-volume form. 0x8C is the unconditional form.
//
// Related
// - FUN_0022b2c0: request_map_change(x, y, z, majorMap, minorMap, flags).
// - fGpffff8cb8: coordinate scale, script integers -> world floats.
// - iGpffffb27c: non-zero while a map change is already pending.
// - DAT_0058bf10: entity slot 0 (+0x60) state; slot 0 is the lead player.
//
// PS2 notes
// - The staged position is the *destination's* spawn point. A scene has no
//   inherent player start; it inherits one from whichever warp sent you there.
//   This is why booting cold into a scene has no spawn coordinates to read.
//
// Unresolved callees keep their original labels.

#include <stdint.h>

extern void FUN_0025c258(void *destination); // evaluate one expression
extern void FUN_0022b2c0(float x, float y, float z,
                         uint32_t majorMap, uint32_t minorMap, uint32_t flags);
extern void FUN_0025d610(void);
extern void FUN_0025d1c0(int, int, int);

extern int iGpffffb27c;       // non-zero while a map change is already pending
extern float fGpffff8cb8;     // script integer -> world float scale
extern uint16_t DAT_0058bf10; // entity slot 0 +0x60: lead state

// NOTE: Original signature: undefined8 FUN_00260f78(void)
uint64_t opcode_0x8C_request_map_change(void)
{
  uint32_t majorMap = 0;
  uint32_t minorMap = 0;
  uint32_t flags = 0;
  int32_t x = 0;
  int32_t y = 0;
  int32_t z = 0;

  FUN_0025c258(&majorMap);
  FUN_0025c258(&minorMap);
  FUN_0025c258(&flags);
  FUN_0025c258(&x);
  FUN_0025c258(&y);
  FUN_0025c258(&z);

  if (iGpffffb27c == 0)
  {
    if ((flags & 0x200000) != 0)
    {
      FUN_0025d610();
    }
    if ((flags & 2) != 0)
    {
      FUN_0025d1c0(1, 0xC, 0);
      DAT_0058bf10 = 10;
    }

    FUN_0022b2c0((float)x / fGpffff8cb8,
                 (float)y / fGpffff8cb8,
                 (float)z / fGpffff8cb8,
                 majorMap, minorMap, flags);
  }

  return 0;
}

/*
 * Opcode 0x71 — submit_distance_to_target
 * Original: FUN_00260080
 *
 * Summary:
 * - Reads one expression from the VM (index or sentinel 0x100).
 * - Uses FUN_0025d6c0 to set the current pool slot pointer (DAT_00355044):
 *     - If index != 0x100: selects &DAT_0058beb0[index * 0xEC].
 *     - If index == 0x100: keeps the current DAT_00355044 as-is (passed as fallback).
 * - Computes a distance-like metric via FUN_0023a418(DAT_00355044) from the selected object
 *   to a global target/origin (callers compare this to thresholds like 1.0/1.5).
 * - Scales the result by DAT_00352bf8 and submits it via FUN_0030bd20.
 *
 * Notes:
 * - This is the distance counterpart to 0x70 (which submits an angle via atan2f wrapper).
 * - FUN_0023a418 is declared to return float here based on widespread call-site usage,
 *   even though the raw decompile shows a void stub.
 *
 * Side effects:
 * - Writes DAT_00355044 via FUN_0025d6c0 (current selected pool object pointer).
 * - Writes into whatever system consumes FUN_0030bd20 (likely audio/param pipeline).
 */

#include <stdint.h>

// Original VM evaluator (FUN_0025c258): reads one or more values into out[...]
extern void bytecode_interpreter(uint32_t out[]); // FUN_0025c258

// Pool selection helper: sets DAT_00355044 based on index or leaves it unchanged (when index==0x100)
extern void FUN_0025d6c0(long index, unsigned int fallbackPtr);

// Distance-like metric from current object pointer to a global reference (see usage thresholds 1.0/1.5)
extern float FUN_0023a418(int objectPtr);

// Submit the computed/scaled parameter
extern void FUN_0030bd20(float value);

// Globals
extern unsigned int DAT_00355044; // current object pointer
extern float DAT_00352bf8;        // scale applied before submission

// Keep original name in comment for reference:
// void FUN_00260080(void)
void opcode_0x71_submit_distance_to_target(void)
{
  uint32_t tmp[4];

  // Read the index (or 0x100 sentinel) from the script expression
  bytecode_interpreter(tmp);

  // Select current object pointer: index into pool or keep current if sentinel
  FUN_0025d6c0((long)tmp[0], DAT_00355044);

  // Compute distance-like metric from the (possibly updated) current object
  float dist = FUN_0023a418((int)DAT_00355044);
  FUN_0030bd20(dist * DAT_00352bf8);
}

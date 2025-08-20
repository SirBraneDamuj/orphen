/*
 * Opcode 0x70 — submit_angle_to_target
 * Original: FUN_00260038
 *
 * Summary:
 * - Reads one expression from the VM (index or sentinel 0x100).
 * - Uses FUN_0025d6c0 to set the current pool slot pointer (DAT_00355044):
 *     - If index != 0x100: selects &DAT_0058beb0[index * 0xEC].
 *     - If index == 0x100: uses the direct pointer uGpffffb0d4.
 * - Computes an angle from the selected object’s XY to a global XY using FUN_0023a480,
 *   which calls FUN_00305408(DAT_0058bed4 - obj[Y], DAT_0058bed0 - obj[X]).
 *   FUN_00305408 resolves to atan2f(y, x) (see src/FUN_00305408.c), so the result is an angle.
 * - Scales the result by fGpffff8c84 and submits it via FUN_0030bd20.
 *
 * Notes:
 * - This opcode mirrors the parameter submission pattern used by 0x7F/0x80/0x92,
 *   but specifically computes an angle via atan2f before submission.
 * - FUN_0023a480 is declared here as returning float, matching call-site usage
 *   in FUN_00260038 (despite its raw decompile stub showing void).
 *
 * Side effects:
 * - Writes DAT_00355044 via FUN_0025d6c0 (current selected pool object pointer).
 * - Writes into whatever system consumes FUN_0030bd20 (likely audio/param pipeline).
 */

#include <stdint.h>

// Original VM evaluator (FUN_0025c258): reads one or more values into out[...]
extern void bytecode_interpreter(uint32_t out[]); // FUN_0025c258

// Pool selection helper: sets DAT_00355044 based on index or direct pointer (when index==0x100)
extern void FUN_0025d6c0(long index, unsigned int directPtr);

// Computes the angle (atan2f-based) from a global target to the object's XY.
// Raw decompile shows void, but call-sites use its return as float.
extern float FUN_0023a480(int objectPtr);

// Submit the computed/scaled parameter
extern void FUN_0030bd20(float value);

// Globals referenced by the original
extern unsigned int uGpffffb0d4; // used as direct pointer when index==0x100
extern float fGpffff8c84;        // scale applied before submission

// Keep original name in comment for reference:
// void FUN_00260038(void)
void opcode_0x70_submit_angle_to_target(void)
{
  uint32_t tmp[4];

  // Read the index (or 0x100 sentinel) from the script expression
  bytecode_interpreter(tmp);

  // Select current object pointer: index into pool or direct pointer
  FUN_0025d6c0((long)tmp[0], uGpffffb0d4);

  // Compute angle from global target to object, then scale and submit
  float angle = FUN_0023a480((int)uGpffffb0d4);
  FUN_0030bd20(angle * fGpffff8c84);
}

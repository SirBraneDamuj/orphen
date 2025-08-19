/*
 * Ensure descriptor is loaded/registered
 * Original: FUN_00266118
 *
 * Summary
 * - If descriptor->state5 < 'd' (ASCII 100), perform one-time registration:
 *   - FUN_00222498(); // enter critical/prepare context (exact semantics TBD)
 *   - If descriptor->flag6 == 0 and descriptor->field2 != 0:
 *       sizeA = 0x18; mode = 0x10; if (!(descriptor->flag4 & 8)) { sizeA = 0x0A; mode = 0x0E; }
 *       descriptor->field7 = FUN_00221d20(descriptor, sizeA, mode);
 *       descriptor->flag6 = 1;
 *   - descriptor->state5 = 1;
 * - No return value; updates descriptor in place.
 *
 * Notes
 * - Offsets/flags naming follows decomp layout: +2 (short), +4 (byte flags), +5 (state), +6 (flag), +7 (handle), etc.
 * - Keep FUN_ names for unresolved callees.
 */

#include <stdint.h>

extern void FUN_00222498(void);
extern unsigned char FUN_00221d20(void *descriptor, unsigned int sizeA, unsigned int mode);

// NOTE: Original signature: void FUN_00266118(undefined8 param_1)
void ensure_descriptor_loaded(void *descriptor)
{
  unsigned char *b = (unsigned char *)descriptor;
  // state at +5
  if (b[5] < 'd')
  {
    FUN_00222498();
    // if not yet assigned (+6 == 0) and short at +2 nonzero
    if (b[6] == 0 && *(short *)((char *)descriptor + 2) != 0)
    {
      unsigned int sizeA = 0x18;
      unsigned int mode = 0x10;
      if ((b[4] & 8) == 0)
      {
        sizeA = 0x0A;
        mode = 0x0E;
      }
      b[7] = FUN_00221d20(descriptor, sizeA, mode);
      b[6] = 1;
    }
    b[5] = 1;
  }
}

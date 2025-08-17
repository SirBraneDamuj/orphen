// Opcode 0x4F — process_pending_spawn_requests (analyzed)
// Original: FUN_0025e7c0
//
// Summary:
// - Iterates a pending spawn/config list at DAT_003556e8 with count DAT_003556e4 (stride 16 bytes per entry).
// - For each entry, selects a descriptor/index based on type bytes at +0x0D/+0x0E:
//     type==0:    base = *(int*)(DAT_00355208*8 + DAT_003551e8); id = (entry[+0x0E]-1) + 0x272
//     type==0x04: base = *(int*)(DAT_003551e8 + 0x78);           id = (entry[+0x0E]-1) + 0x373
//     type==0x05: base = *(int*)(DAT_003551e8 + 0x80);           id = (entry[+0x0E]-1) + 0x474
//   Uses per-entry index (0-based) with 0x2C stride into selected base to check/load and then resolve an entity pointer.
// - Ensures descriptor is loaded (FUN_002661a8) if not flagged, resolves instance via FUN_00265e28(id), and initializes:
//     * FUN_002662e0(entry[0], entry[1], entry[2], entity) — likely sets position/params
//     * entity[+0x2E*2] = FUN_00216690((int8)entry[3] * DAT_00352b90 + DAT_00352b94) — timing/phase
//     * entity[+0x4C*2] = loop index (i)
//     * If entry[+0x0F] is negative: set facing/flip and a cluster of flags; in debug mode (DAT_003555d3) perform an immediate update
// - Calls FUN_0025ba98(entity->something, outFlags) to derive auStack_b0/uStack_a4/uStack_a0/fStack_9c and applies:
//     * entity[+0x2A*2], [+0x8E*2] = uStack_a4; entity[+0x2C*2], [+0x90*2] = uStack_a0
//     * entity[+0x133] = FUN_0030bd20(fStack_9c / DAT_00352b98) — likely volume/attenuation
//     * entity[+0x95..+0x97], [+0x99] from cStack_aa/a9/a8/a7; set bit 0x100 if cStack_a7 != 0
//     * From auStack_b0[0] bits: 0x2000 -> set bit 0x10; 0x4000 -> either copy [0x50] to [0x57] or zero [3];
//       0x8000 -> clear a bit and write derived value via FUN_00227798 to [0x26]; 0x1000 -> set a bit in [2].
// - No VM operands consumed; returns 0.
//
// Notes:
// - This appears to be a batched spawner/initializer for effects or small entities sourced from a prebuilt list.
// - Keep raw FUN_/DAT_ names; we document addresses and behaviors but do not rename unresolved externals.

#include <stdint.h>

// Raw entry point we’re analyzing
extern unsigned long long FUN_0025e7c0(void);

// Wrapper with analyzed name
unsigned int opcode_0x4F_process_pending_spawn_requests(void)
{
  // Delegate to the raw implementation to preserve exact behavior.
  (void)FUN_0025e7c0();
  return 0;
}

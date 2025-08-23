// Analyzed re-expression of FUN_00218158
// Original signature: void FUN_00218158(int numerator, int denominator)
//
// Purpose:
//   Extended interpolation update used by opcode 0x44 path in advance_timed_interpolation.
//   Similar to FUN_00217f38 but with an additional curve/sample call (FUN_00266988) and a pair
//   of global copies at the end. Both functions consume a progress ratio = numerator/denominator
//   (float) to drive table/sample based transforms.
//
// Behavior summary (from src/FUN_00218158.c comparison):
//   ratio = (float)numerator / (float)denominator
//   FUN_00266ce8(ratio, 0x55fad8, 0x55f930)   // update first transform set
//   FUN_00266ce8(ratio, 0x55fce0, 0x55f940)   // update second transform set
//   FUN_00266988(ratio, 0x55f950, 0x355a88)   // extra update only present in this extended variant
//   FUN_00217d40(DAT_0055f930, DAT_0055f934, DAT_0055f938)  // apply first triple
//   FUN_00217d10(DAT_0055f940, DAT_0055f944, DAT_0055f948)  // apply second triple
//   DAT_0035564c = DAT_00355a88;                // copy pair (likely caching last curve output)
//   DAT_00355658 = DAT_00355a8c;
//
// Differences vs FUN_00217f38:
//   - Adds FUN_00266988 call (extra bank / curve sample) pointing to (0x55f950, 0x355a88)
//   - Copies two 32-bit globals (0x355a88/0x355a8c) into destination globals (0x35564c/0x355658)
//   - Otherwise identical call sequence/order for shared operations.
//
// Side effects:
//   - Mutates several global structs/buffers at addresses 0x55f930 / 0x55f940 and related fields.
//   - Updates final cached values DAT_0035564c / DAT_00355658.
//
// Naming notes:
//   - Retain original FUN_* names for callees until they are separately analyzed. This file gives
//     a semantic wrapper name (interpolation_update_extended) for internal cross-reference.
//
// Cross-reference:
//   - Invoked from analyzed/ops/0x44_advance_timed_interpolation.c when opcode == 0x44.
//   - Simpler variant (without FUN_00266988 & copies) is FUN_00217f38 (opcode 0x42 path).
//
// TODO (future analysis):
//   - Identify data types / structure layouts at 0x55fad8 / 0x55fce0 / 0x55f950.
//   - Determine semantic meaning of the extra sampled pair (cached into 0x35564c / 0x355658).
//
#include <stdint.h>

// Unanalyzed externs (preserve names)
extern void FUN_00266ce8(float ratio, uint32_t srcBase, uint32_t dstBase);
extern void FUN_00266988(float ratio, uint32_t srcBase, uint32_t dstBase);
extern void FUN_00217d40(int a, int b, int c);
extern void FUN_00217d10(int a, int b, int c);

// Globals touched (extern until mapped):
extern int DAT_0055f930;
extern int DAT_0055f934;
extern int DAT_0055f938;
extern int DAT_0055f940;
extern int DAT_0055f944;
extern int DAT_0055f948;
extern int DAT_00355a88;
extern int DAT_00355a8c; // source pair
extern int DAT_0035564c;
extern int DAT_00355658; // destination pair

void interpolation_update_extended(int numerator, int denominator)
{
  // Guard: avoid division by zero (not present in original, but safe in analyzed version)
  if (denominator == 0)
    return;
  float ratio = (float)numerator / (float)denominator;

  // Two base interpolation updates
  FUN_00266ce8(ratio, 0x55fad8, 0x55f930);
  FUN_00266ce8(ratio, 0x55fce0, 0x55f940);

  // Extended extra update
  FUN_00266988(ratio, 0x55f950, 0x355a88);

  // Apply updated triples
  FUN_00217d40(DAT_0055f930, DAT_0055f934, DAT_0055f938);
  FUN_00217d10(DAT_0055f940, DAT_0055f944, DAT_0055f948);

  // Cache sampled pair
  DAT_0035564c = DAT_00355a88;
  DAT_00355658 = DAT_00355a8c;
}

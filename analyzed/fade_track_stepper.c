// Fade track stepper — advances a global color/byte triplet blend over time
// Original: bool FUN_0025d480(int param_1) in src/FUN_0025d480.c
//
// Summary
//   Advances a time-accumulator for a small struct-of-arrays fade system and computes the
//   blended output bytes for three channels by linearly interpolating between two source
//   key states. Progress is tracked in 1/32nd units per step of the accumulator.
//
// Data layout (inferred from raw globals; stride = 0x14 bytes per track)
//   base +0x00: int32 accum32     — current accumulator in 1/32 units
//   base +0x04: int32 totalSteps  — total number of whole steps (must be > 0)
//   base +0x08: uint8 srcA[3]     — starting bytes (e.g., RGB)
//   base +0x0B: uint8 srcB[3]     — ending bytes (e.g., RGB)
//   base +0x0E: uint8 out[3]      — blended outputs written each update
//   (there are a few bytes of padding/unused within the 0x14-byte stride)
//
// Related functions
//   - Initializer: void FUN_0025d408(int track, uint32 colorA, uint32 colorB, int totalSteps)
//       * Sets accum32 to 0x20 (32), stores totalSteps, splits colorA/colorB into bytes,
//         and seeds out[] = srcA.
//   - Getter:     uint8 FUN_0025d590(int track, uint param) — returns out[param & 0xFF]
//   - Opcode:     0x9B (analyzed as opcode_0x9B_advance_fade_track) calls this stepper.
//
// Semantics
//   Let q = trunc_toward_zero(accum32 / 32). With totalSteps = N, compute weights:
//     wB = q, wA = N - q, denom = N
//   Then out[c] = ((srcA[c] * wA) + (srcB[c] * wB)) / denom, for c in {0,1,2}.
//   After writing out[], accum32 += DAT_003555bc (a global step increment).
//   Returns true when accum32 > (N << 5) (i.e., the fade has completed).
//   If N == 0, triggers an assert (FUN_0026bfc0(0x34cdc8)) and a trap.
//
// PS2-specific note
//   The manual rounding logic implements integer division by 32 with truncation toward zero
//   for negative accumulators; for non-negative values it behaves as a standard right shift.
//
// Keep the original FUN_* name in comments for traceability.

#include <stdint.h>
#include <stdbool.h>

// Raw globals (symbol addresses kept as-is; see globals.json for cross-refs)
extern int DAT_003555bc;           // global step increment per update
extern int DAT_00572078;           // base: accum32 array (int per track, stride 0x14)
extern int DAT_0057207c;           // base: totalSteps array (int per track, stride 0x14)
extern unsigned char DAT_00572080; // base: srcA[0]
extern unsigned char DAT_00572081; // base: srcA[1]
extern unsigned char DAT_00572082; // base: srcA[2]
extern unsigned char DAT_00572083; // base: srcB[0]
extern unsigned char DAT_00572084; // base: srcB[1]
extern unsigned char DAT_00572085; // base: srcB[2]
extern unsigned char DAT_00572086; // base: out[0]
extern unsigned char DAT_00572087; // base: out[1]
extern unsigned char DAT_00572088; // base: out[2]

// Assert/trap helper used by original code
extern void FUN_0026bfc0(uint32_t addr);

// Analyzed implementation with descriptive naming
// Original signature: bool FUN_0025d480(int param_1)
bool fade_track_stepper(int trackIndex)
{
  const int stride = 0x14; // bytes per track
  int offset = trackIndex * stride;

  // accum32 and totalSteps pointers for this track
  int *accum32 = (int *)((uintptr_t)&DAT_00572078 + offset);
  int *totalSteps = (int *)((uintptr_t)&DAT_0057207c + offset);

  int acc = *accum32;

  // q = trunc_toward_zero(acc / 32). Implementation matches original:
  int q = (acc < 0) ? ((acc + 31) >> 5) : (acc >> 5);

  if (*totalSteps == 0)
  {
    // Matches original behavior: assert first, then a trap shortly after.
    FUN_0026bfc0(0x34cdc8);
  }

  int N = *totalSteps;
  int wB = q;
  int wA = N - q;
  if (N == 0)
  {
    // The decompiled code issues a trap(7) here; we mirror by forcing a crash via divide-by-zero
    // if evaluated below, but keep the guard explicit for clarity.
    __asm volatile(""); // no-op barrier to keep structure; behavior documented above
  }

  // Compute blended outputs per channel
  unsigned char a0 = *(((unsigned char *)&DAT_00572080) + offset);
  unsigned char a1 = *(((unsigned char *)&DAT_00572081) + offset);
  unsigned char a2 = *(((unsigned char *)&DAT_00572082) + offset);
  unsigned char b0 = *(((unsigned char *)&DAT_00572083) + offset);
  unsigned char b1 = *(((unsigned char *)&DAT_00572084) + offset);
  unsigned char b2 = *(((unsigned char *)&DAT_00572085) + offset);

  unsigned char *out0 = ((unsigned char *)&DAT_00572086) + offset;
  unsigned char *out1 = ((unsigned char *)&DAT_00572087) + offset;
  unsigned char *out2 = ((unsigned char *)&DAT_00572088) + offset;

  // Integer blend with normalization by N
  *out0 = (unsigned char)(((int)a0 * wA + (int)b0 * wB) / N);
  *out1 = (unsigned char)(((int)a1 * wA + (int)b1 * wB) / N);
  *out2 = (unsigned char)(((int)a2 * wA + (int)b2 * wB) / N);

  // Advance accumulator by global step increment
  int newAcc = acc + DAT_003555bc;
  *accum32 = newAcc;

  // Completion condition: accum32 exceeds totalSteps<<5
  return (N << 5) < newAcc;
}

// Thin wrapper retaining the original name/signature for easy cross-referencing
// Original FUN_0025d480 exposed as:
bool FUN_0025d480(int param_1)
{
  return fade_track_stepper(param_1);
}

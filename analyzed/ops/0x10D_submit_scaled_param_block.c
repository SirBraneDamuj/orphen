// Extended opcode 0x10D — Submit scaled parameter block (calls FUN_0021e088)
// Original: FUN_002629c0
//
// Summary
// - Parses 9 arguments via the VM evaluator and forwards them to FUN_0021e088 with scaling:
//   * Four signed ints converted to floats and divided by fGpffff8d0c
//   * Two 32-bit values (pass-through)
//   * One 16-bit value (pass-through, lower 16 bits significant)
//   * Two 8-bit values (flags/modes), passed from the low byte of the evaluated slots
// - Returns 0.
//
// Observations
// - The first four values likely represent coordinates or dimensions normalized by a global scale
//   factor fGpffff8d0c. The callee FUN_0021e088 is not yet analyzed; semantics TBD.
// - Argument order (by evaluation sequence):
//     u32 a0, s32 x, u32 a2, u16 a3, s32 y, s32 z, s32 w, u8 b0, u8 b1
//   FUN_0021e088 receives: (x/f, y/f, z/f, w/f, a0, a2, a3, b0, b1).
//
// Keep original FUN_/global names for traceability until callees/globals are analyzed.

#include <stdint.h>

// VM expression evaluator (pushes a 32-bit result into the provided address)
extern void FUN_0025c258(void *out4);

// Callee (unanalyzed): likely applies the parameter block to some system (graphics/audio/ui)
extern void FUN_0021e088(float, float, float, float, uint32_t, uint32_t, uint16_t, uint8_t, uint8_t);

// Global scale used for normalizing the first four integer parameters
extern float fGpffff8d0c;

// NOTE: Original signature: undefined8 FUN_002629c0(void)
void opcode_0x10D_submit_scaled_param_block(void)
{
  uint32_t a0; // uStack_40
  int32_t x;   // iStack_3c
  uint32_t a2; // uStack_38
  uint16_t a3; // uStack_34 (lower 16 bits significant)
  int32_t y;   // iStack_30
  int32_t z;   // iStack_2c
  int32_t w;   // iStack_28
  uint8_t b0;  // auStack_24[0]
  uint8_t b1;  // low byte of auStack_20[0]

  // 32-bit temporaries for evaluator writes
  uint32_t t_a3, t_b0, t_b1;

  // Evaluate arguments in order, mirroring the raw function
  FUN_0025c258(&a0);
  FUN_0025c258(&x);
  FUN_0025c258(&a2);
  FUN_0025c258(&t_a3); // evaluator writes 32 bits; we use low 16 bits
  FUN_0025c258(&y);
  FUN_0025c258(&z);
  FUN_0025c258(&w);
  FUN_0025c258(&t_b0); // read into 32 bits; low byte used
  FUN_0025c258(&t_b1); // read into 32 bits; low byte used

  a3 = (uint16_t)t_a3;
  b0 = (uint8_t)t_b0;
  b1 = (uint8_t)t_b1;

  float xf = (float)x / fGpffff8d0c;
  float yf = (float)y / fGpffff8d0c;
  float zf = (float)z / fGpffff8d0c;
  float wf = (float)w / fGpffff8d0c;

  FUN_0021e088(xf, yf, zf, wf, a0, a2, a3, b0, b1);
}

// Opcode 0x65 — multi_call_feeder
// Original: FUN_0025f7d8 (src/FUN_0025f7d8.c)
//
// Summary
//   Allocates/initializes an effect or script-call object using four values pulled via nested
//   interpreter calls, then writes timing/position fields with global scaling. Returns success
//   if allocation succeeds.
//
// Inferred behavior
//   - Reads a 16-bit identifier from the bytecode stream (two bytes at DAT_00355cd0), then advances.
//   - Calls FUN_0025c1d0() to get a small index (uVar3); asserts if > 0xFFFF.
//   - Calls FUN_0025c258 four times to populate a 2x short pair (auStack_70[0]) and three ints
//     (iStack_6c, iStack_68, iStack_64), and a 4-int vector (aiStack_60).
//   - Allocates/looks up an object via FUN_00265e28(id) and, if successful:
//       * Sets type 0x38 and a flag bit 0x4000.
//       * Stores normalized durations/coords dividing by DAT_00352bdc.
//       * Computes a derived value via FUN_00227070 and a normalized scalar via FUN_00216690.
//       * Writes the small index (uVar3) into offset 0x98 (short) and the id into 0x1CE (short).
//
// PS2/engine context
//   - DAT_00355044: current working object pointer for scene/effect builder.
//   - DAT_00352bdc: global scale (frame/tick duration or pixel->unit scale).
//   - Likely creates a timed visual/audio effect with initial parameters and schedules it.
//
// Original signature
//   bool FUN_0025f7d8(void);
//
// Side effects
//   - Writes multiple fields on *DAT_00355044; sets global DAT_00355044 to the allocated object.
//   - Asserts via FUN_0026bfc0 on invalid inputs.
//
// Keep original name in comments for traceability.

#include <stdint.h>
#include <stdbool.h>

// Extern globals (retain raw names for now; use globals.json for cross-refs)
extern unsigned char *DAT_00355cd0;  // bytecode IP
extern unsigned short DAT_00355cd8;  // current opcode
extern float DAT_00352bdc;           // global scale
extern unsigned short *DAT_00355044; // current builder object (effect/scene node)

// Extern functions used
extern unsigned long FUN_0025c1d0(void);                  // small index fetch/compute
extern void bytecode_interpreter(void *out);              // analyzed helper (orig: FUN_0025c258)
extern long FUN_00265e28(int id);                         // allocate/lookup object by id
extern void FUN_0026bfc0(uint32_t addr);                  // assert/trap
extern uint32_t FUN_00227070(float a, float b, long ctx); // compute derived param
extern uint32_t FUN_00216690(float v);                    // normalize/convert scalar

// Analyzed wrapper preserving behavior
bool opcode_0x65_multi_call_feeder(void)
{
  // Read 16-bit id from stream (big-endian via two bytes)
  int id = ((uint32_t)DAT_00355cd0[0] << 16) | ((uint32_t)DAT_00355cd0[1] << 24);
  // Match decompiled arithmetic: (b0 + b1*0x100) * 0x10000 then >>16 later
  int packed = (((int)DAT_00355cd0[0]) + ((int)DAT_00355cd0[1] << 8)) << 16;
  DAT_00355cd0 += 2;

  unsigned long small_index = FUN_0025c1d0();
  if (small_index > 0xFFFFUL)
  {
    FUN_0026bfc0(0x34ced8); // assert: out of range
  }

  // Gather arguments
  unsigned short auStack_70[2];
  int iStack_6c, iStack_68, iStack_64;
  int aiStack_60[4];

  bytecode_interpreter(auStack_70);
  bytecode_interpreter((void *)((uintptr_t)auStack_70 | 4));
  bytecode_interpreter((void *)((uintptr_t)auStack_70 | 8));
  bytecode_interpreter((void *)((uintptr_t)auStack_70 | 12));
  bytecode_interpreter(aiStack_60);

  long obj = FUN_00265e28(packed >> 16);
  float scale = DAT_00352bdc;
  unsigned short *pu = (unsigned short *)obj;
  DAT_00355044 = pu;

  if (obj != 0)
  {
    float fB = (float)iStack_68 / scale;
    pu[0] = 0x38;               // type
    pu[1] = pu[1] | 0x4000;     // flag
    *(float *)(pu + 0x12) = fB; // field
    pu[0x50] = auStack_70[0];   // short copy
    *(float *)(pu + 0x10) = (float)iStack_6c / scale;
    *(float *)(pu + 0x14) = (float)iStack_64 / scale;

    uint32_t d = FUN_00227070((float)iStack_6c / scale, fB, obj);
    *(uint32_t *)(DAT_00355044 + 0x26) = d;

    uint32_t norm = FUN_00216690((float)aiStack_60[0] / scale);
    unsigned short *cur = DAT_00355044;
    cur[0x98] = (unsigned short)small_index;
    pu[0xE7] = (unsigned short)((uint32_t)packed >> 16);
    *(uint32_t *)(cur + 0x2E) = norm;
  }

  return obj != 0;
}

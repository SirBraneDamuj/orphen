// Opcode 0x80 — submit_param_from_model_axis (analyzed)
// Original: FUN_00260880
//
// Summary:
// - Evaluates one VM expression: modelIndex
// - Reads an immediate byte: axis (0..2)
// - Bounds checks axis and modelIndex; on failure calls FUN_0026bfc0 with format strings at 0x34cfe8/0x34d000
// - Reads a float from per-model data at base (iGpffffb770 + modelIndex*0x74):
//     offset = (sGpffffbd68 == 0x7f ? 0x3c : 0x48) + axis*4
// - Multiplies by a scale constant (sGpffffbd68==0x7f ? fGpffff8ca0 : fGpffff8ca4)
// - Submits the resulting value via FUN_0030bd20 (shared audio/param submit path)
//
// Notes:
// - pbGpffffbd60 is the VM byte-stream pointer. This opcode mixes an evaluated operand with a raw immediate.
// - Offsets 0x3c/0x48 and axis 0..2 strongly suggest selecting a component triplet from two fields in a model struct.
// - Keeps original FUN_/DAT_ names for traceability; do not rename unresolved externs here.

#include <stdint.h>

// VM entry (analyzed name)
extern void bytecode_interpreter(void *result_out); // orig FUN_0025c258

// VM/script globals (raw symbols)
extern unsigned char *pbGpffffbd60; // VM instruction pointer (byte stream)
extern short sGpffffbd68;           // mode/selector influencing field/scale
extern int iGpffffb76c;             // model count / upper bound
extern int iGpffffb770;             // base pointer to model array
extern float fGpffff8ca0;           // scale A
extern float fGpffff8ca4;           // scale B

// Utilities (raw symbols)
extern void FUN_0030bd20(float value);       // param submit (shared path with 0x92)
extern void FUN_0026bfc0(uint32_t fmt_addr); // debug/assert print

// Original signature: void FUN_00260880(void)
unsigned int opcode_0x80_submit_param_from_model_axis(void)
{
  int modelIndex = 0;
  bytecode_interpreter(&modelIndex);

  unsigned char axis = *pbGpffffbd60; // immediate byte
  pbGpffffbd60++;

  if (axis > 2)
  {
    // "axis out of range" message at 0x34cfe8
    FUN_0026bfc0(0x34cfe8);
  }
  if (modelIndex >= iGpffffb76c)
  {
    // "model index out of range" message at 0x34d000
    FUN_0026bfc0(0x34d000);
  }

  // Select field block and scale based on sGpffffbd68
  const int perModelStride = 0x74;
  const int fieldBase = (sGpffffbd68 == 0x7f) ? 0x3c : 0x48;
  const float scale = (sGpffffbd68 == 0x7f) ? fGpffff8ca0 : fGpffff8ca4;

  uintptr_t base = (uintptr_t)iGpffffb770 + (uintptr_t)modelIndex * (uintptr_t)perModelStride;
  float value = *(float *)(base + (uintptr_t)fieldBase + (uintptr_t)axis * 4u);

  FUN_0030bd20(value * scale);
  return 0;
}

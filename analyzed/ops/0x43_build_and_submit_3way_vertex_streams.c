// Opcode 0x43 — build_and_submit_3way_vertex_streams
// Original handler: FUN_0025de08 (src/FUN_0025de08.c)
// Signature: undefined8 FUN_0025de08(void)
//
// Summary
//   This opcode reads three VM expressions that each resolve to a work-mem index,
//   then, for each of three vertex streams, it:
//     - Sets the VM work-mem base (iGpffffbd60) to (work_base + expr[i] + 4)
//     - Repeatedly reads scalars via FUN_0025c258 into a local buffer
//     - Normalizes each read by dividing by fGpffff8c00 (a global scale)
//     - Writes the floats into one of three contiguous output buffers:
//         apuStack_c0[0] = &DAT_01849a00
//         apuStack_c0[1] = &DAT_01849ac0
//         apuStack_c0[2] = &DAT_01849b40
//   The element counts per stream are derived from the first three VM results:
//     stream0_count = *(int *)(expr0 + work_base) * 3
//     stream1_count = *(int *)(expr1 + work_base) << 1 (times 2)
//     stream2_count = *(int *)(expr2 + work_base) * 3
//   After filling the buffers, it submits them to the renderer:
//     FUN_00217e18(0);
//     FUN_00217fe8(apuStack_c0[0], apuStack_c0[1], stream0_count/3,
//                  apuStack_c0[2], stream2_count/3);
//   Finally, it clears uGpffffbd78 (likely a render/GP state busy flag).
//
// PS2-specific notes
//   - The three buffers look like tightly packed float arrays, probably positions (xyz),
//     normals/tangents (?), and UVs (two floats), based on the multipliers (×3, ×2, ×3)
//     and the final counts passed as triplet counts to FUN_00217fe8.
//   - iGpffffbd60 is the interpreter's transient work-mem base pointer used by FUN_0025c258.
//   - fGpffff8c00 is a global normalization constant (screen or world scale).
//
// Contract
//   Inputs:
//     - Three VM expressions yielding work-mem indices (expr0, expr1, expr2)
//     - Work memory base iGpffffb0e8, VM fetcher FUN_0025c258
//   Outputs:
//     - Writes to DAT_01849a00 / ac0 / b40 float buffers
//     - Calls FUN_00217fe8 to submit the geometry
//     - Sets uGpffffbd78 = 0
//   Error modes:
//     - None observed; counts of zero skip loops
//
// Keep original FUN_* name in comments for traceability.

#include <stdint.h>

// VM and globals
extern int iGpffffb0e8;           // work memory base (script data segment)
extern int iGpffffbd60;           // VM scratch base (set before reads)
extern float fGpffff8c00;         // normalization constant
extern uint32_t uGpffffbd78;      // render/GP busy flag (cleared at end)

// Raw VM scalar fetch (original bytecode interpreter helper)
extern void FUN_0025c258(void *out);

// Renderer submitters
extern void FUN_00217e18(int);
extern void FUN_00217fe8(float *pos, float *uv, int triCount, float *norm, int triCount2);

// Output buffers discovered in FUN_0025de08
extern float DAT_01849a00[]; // stream 0 (xyz..., count*3)
extern float DAT_01849ac0[]; // stream 1 (uv...,  count*2)
extern float DAT_01849b40[]; // stream 2 (xyz..., count*3)

// Original signature: undefined8 FUN_0025de08(void)
unsigned long opcode_0x43_build_and_submit_3way_vertex_streams(void)
{
  int expr[6];
  int saved_vm_base = iGpffffbd60;

  // Read three expressions; the original calls FUN_0025c258 three times with distinct destinations
  // aiStack_e0, (aiStack_e0|8), and aiStack_e0+4 patterns correspond to separate locals.
  FUN_0025c258(expr);           // expr0
  FUN_0025c258((void *)((uintptr_t)expr | 8)); // expr1 (matching raw code's odd stack addressing)
  FUN_0025c258(expr + 4);       // expr2

  // Compute element counts from work-mem values
  int base = iGpffffb0e8;
  int count0 = *(int *)(expr[0] + base) * 3;   // xyz per vertex
  int count1 = *(int *)(expr[2] + base) << 1;  // uv per vertex (×2)
  int count2 = *(int *)(expr[4] + base) * 3;   // normals or second xyz

  float *streams[3];
  streams[0] = DAT_01849a00;
  streams[1] = DAT_01849ac0;
  streams[2] = DAT_01849b40;

  // For each stream, set VM base to (base + index + 4) and read that many scalars,
  // normalizing by fGpffff8c00 into the output buffer.
  int offsets[3] = { 0, 2, 4 };   // indices into expr[] used by raw code
  int counts[3]  = { count0, count1, count2 };

  for (int s = 0; s < 3; ++s)
  {
    int idx = expr[offsets[s]];
    int n   = counts[s];
    iGpffffbd60 = idx + base + 4; // source stream pointer for FUN_0025c258

    float *dst = streams[s];
    for (int i = 0; i < n; ++i)
    {
      int scalar;
      FUN_0025c258(&scalar);
      dst[i] = (float)scalar / fGpffff8c00;
    }
  }

  // Restore VM base and submit
  iGpffffbd60 = saved_vm_base;
  FUN_00217e18(0);
  // Pass tri counts (divide xyz streams by 3)
  FUN_00217fe8(DAT_01849a00, DAT_01849ac0, count0 / 3,
               DAT_01849b40, count2 / 3);
  uGpffffbd78 = 0;
  return 0;
}

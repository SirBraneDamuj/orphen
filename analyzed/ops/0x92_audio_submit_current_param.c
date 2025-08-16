// Opcode 0x92 — audio_submit_current_param (analyzed)
// Original: FUN_00261258
//
// Summary:
// - Evaluates one expression (index) via the main VM and uses it to address a 3-float
//   per-index parameter block at DAT_00571de0 (stride 0x0C):
//     +0x00: current value, +0x04: target value, +0x08: step per tick
// - Multiplies the current value by global scale DAT_00352c30 and passes the result to FUN_0030bd20.
// - Side effect: drives the audio engine (likely sets/plays a sound or channel parameter for this index).
//
// Cluster context (neighbor opcodes around 0x91–0x93):
// - 0x91 (orig FUN_002611b8): ramp current toward target for DAT_00571de0[idx]; generic param tween over that table.
// - 0x92 (this): submit/apply the current value (scaled by DAT_00352c30) to the audio system via FUN_0030bd20; commonly
//   paired with 0x91 when tweening audio parameters.
// - 0x93 (orig FUN_002612a0/002612e0 pair used nearby): helpers for coarse timing/param submit; 0x612e0 calls FUN_0022dcf0 with a scaled float and a u16.
//
// Doors case study:
// - Subproc 4739 (0x1283) includes "... 0x92 0C 07 0B ...", passing index 7. At runtime this triggers the door SFX.
//   The index selects DAT_00571de0[7], and FUN_0030bd20 likely enqueues/updates the SFX parameter or playback.
//
// PS2 notes:
// - FUN_0030bd20 is widely used for float→engine conversions; here it’s invoked for side effects (return value unused),
//   consistent with submitting an audio command or parameter to a mixer/voice.
//
// Keep unresolved externs by their original labels for traceability.

#include <stdint.h>

typedef unsigned int uint;

// VM entry (analyzed name)
extern void bytecode_interpreter(void *result_out); // orig FUN_0025c258

// Audio param table: 3 floats per index: current(+0), target(+4), step(+8)
extern unsigned char DAT_00571de0[];

// Global scale applied to the current value before submit
extern float DAT_00352c30;

// Audio submit primitive (side effects inside; return value ignored here)
extern int FUN_0030bd20(float value);

// Original signature: void FUN_00261258(void)
void opcode_0x92_audio_submit_current_param(void)
{
  // Evaluate index expression via VM
  uint idx;
  bytecode_interpreter(&idx);

  // Load current value from DAT_00571de0[idx * 0x0C]
  float *base = (float *)(DAT_00571de0 + (idx * 0x0C));
  float current = base[0]; // +0x00

  // Apply global scale and submit to audio engine
  (void)FUN_0030bd20(current * DAT_00352c30);
}

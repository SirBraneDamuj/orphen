// Opcode 0x90 — audio_init_param_ramp (analyzed)
// Original: FUN_00261100
//
// Summary:
// - Evaluates 4 expressions (index, target, current, step) via the main VM.
// - Divides the three float parameters by DAT_00352c2c (normalization scale).
// - Writes them to DAT_00571de0[index * 0x0C] parameter block:
//     +0x00: current value
//     +0x04: target value
//     +0x08: step per tick
// - If target < current, negates the step (ensures ramping moves toward target).
// - Returns 0.
//
// Cluster context (opcodes 0x90-0x92 work together):
// - 0x90 (this): Initialize ramping parameters for an audio channel/parameter.
// - 0x91 (FUN_002611b8): Advance current toward target by step * delta_time.
// - 0x92 (FUN_00261258): Submit current value (scaled) to audio engine.
//
// Typical usage pattern:
//   0x90 <index> <target> <current> <step>  # Initialize ramp
//   [loop:]
//   0x91 <index>                             # Step toward target
//   0x92 <index>                             # Apply/submit to audio
//   [conditional loop back if not reached target]
//
// PS2 notes:
// - DAT_00571de0 is an array of audio parameter states (stride 0x0C = 3 floats).
// - DAT_00352c2c is a global normalization divisor for input values.
// - Step negation ensures movement toward target regardless of initial ordering.
//
// Keep unresolved externs by their original labels for traceability.

#include <stdint.h>

typedef unsigned int uint;

// VM entry (analyzed name)
extern void bytecode_interpreter(void *result_out); // orig FUN_0025c258

// Audio param table: 3 floats per index: current(+0), target(+4), step(+8)
extern unsigned char DAT_00571de0[];

// Normalization divisor applied to input parameters
extern float DAT_00352c2c;

// Original signature: undefined8 FUN_00261100(void)
uint64_t opcode_0x90_audio_init_param_ramp(void)
{
  // Evaluate 4 parameters via VM
  int params[4];
  bytecode_interpreter(&params[0]); // index
  bytecode_interpreter(&params[1]); // target
  bytecode_interpreter(&params[2]); // current
  bytecode_interpreter(&params[3]); // step

  int index = params[0];

  // Normalize the three float parameters
  float target = (float)params[1] / DAT_00352c2c;
  float current = (float)params[3] / DAT_00352c2c;
  float step = (float)params[2] / DAT_00352c2c;

  // Calculate offset into parameter array (stride 0x0C = 12 bytes = 3 floats)
  int offset = index * 0x0C;

  // Write to DAT_00571de0[index] block
  *(float *)(DAT_00571de0 + offset + 0x04) = target;  // +0x04: target value
  *(float *)(DAT_00571de0 + offset + 0x00) = current; // +0x00: current value
  *(float *)(DAT_00571de0 + offset + 0x08) = step;    // +0x08: step per tick

  // If target < current, negate step to ensure ramping moves toward target
  // (e.g., if current=100 target=50 step=5, flip to step=-5)
  if (target < current)
  {
    *(float *)(DAT_00571de0 + offset + 0x08) = -step;
  }

  return 0;
}

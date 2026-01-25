// Extended opcode 0x103 — audio_trigger_with_param (analyzed)
// Original: FUN_00262488
//
// Summary:
// - Evaluates 2 expressions: sound_id and raw_param.
// - Normalizes raw_param by dividing by DAT_00352c64 (scale factor).
// - Calls FUN_0021b480(normalized_param, sound_id) to trigger audio.
// - Commonly paired with opcode 0x125 in scripts.
// - Returns 0.
//
// Typical usage:
//   FF 03 <sound_id_expr> <param_expr>  # Trigger sound with parameter
//
// Context:
// - Part of audio control system alongside opcodes 0x90-0x92 (parameter ramping).
// - FUN_0021b480 wraps FUN_0021b4b8 with default values (0, 0, 5, 5, entity_pool_base).
// - DAT_00354cac set to 1 after call (audio busy/pending flag).
//
// PS2 notes:
// - param likely controls volume, pitch, pan, or other audio property.
// - DAT_00352c64 is normalization scale (maps script values to audio system range).
// - Entity pool base (0x58beb0) passed to audio system for spatial audio binding.
//
// Keep unresolved externs by their original labels for traceability.

#include <stdint.h>

typedef unsigned int uint;

// VM entry (analyzed name)
extern void bytecode_interpreter(void *result_out); // orig FUN_0025c258

// Audio param normalization scale
extern float DAT_00352c64;

// Audio trigger function (wraps FUN_0021b4b8 with defaults)
// Signature: FUN_0021b480(float param, uint sound_id)
extern void FUN_0021b480(float param, uint sound_id);

// Original signature: undefined8 FUN_00262488(void)
uint64_t opcode_0x103_audio_trigger_with_param(void)
{
  uint params[2];

  // Evaluate sound_id and raw parameter
  bytecode_interpreter(&params[0]); // sound_id
  bytecode_interpreter(&params[1]); // raw_param

  uint sound_id = params[0];
  int raw_param = (int)params[1];

  // Normalize parameter by dividing by scale factor
  float normalized_param = (float)raw_param / DAT_00352c64;

  // Trigger audio with normalized parameter
  FUN_0021b480(normalized_param, sound_id);

  return 0;
}

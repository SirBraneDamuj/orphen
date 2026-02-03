// Opcode 0x94 — set_audio_position_normalized
// Original: FUN_002612e0
//
// Summary:
// - Reads two parameters from VM
// - Normalizes first parameter by DAT_00352c34
// - Calls audio positioning function with normalized value and second param
// - Returns 0
//
// Parameters:
// - param0: Position value (normalized before use)
// - param1: Additional parameter (u16, passed directly)
//
// Side effects:
// - Calls FUN_0022dcf0(normalized_position, param1)
// - Likely sets audio position/pan/distance parameter
//
// Normalization:
// Converts integer position to float by dividing by DAT_00352c34 scale factor.
//
// Related opcodes:
// - 0x8A-0x8C: Audio trigger opcodes with positioning
// - 0x8D-0x8E: Audio mode initialization
// - 0xDE: set_audio_channel_parameter
//
// PS2-specific notes:
// - Audio system parameter update
// - FUN_0022dcf0 likely SPU2 or 3D audio positioning
// - Normalized float suggests range like -1.0 to 1.0 or 0.0 to 1.0

#include <stdint.h>

// VM evaluator (analyzed name for FUN_0025c258)
extern void bytecode_interpreter(void *result_out);

// Audio positioning function
extern void FUN_0022dcf0(float normalized_position, uint16_t param);

// Normalization scale factor
extern float DAT_00352c34;

// Original signature: undefined8 FUN_002612e0(void)
uint64_t opcode_0x94_set_audio_position_normalized(void)
{
  int32_t position;
  uint16_t param1;

  // Read first parameter (position value)
  bytecode_interpreter(&position);

  // Read second parameter (stored at offset +4)
  bytecode_interpreter((uint32_t)&position | 4);

  // Normalize position and call audio function
  FUN_0022dcf0((float)position / DAT_00352c34, param1);

  return 0;
}

// Original signature preserved for cross-reference
// undefined8 FUN_002612e0(void)

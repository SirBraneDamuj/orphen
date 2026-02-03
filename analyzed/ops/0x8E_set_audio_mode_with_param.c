// Opcode 0x8E — set_audio_mode_with_param
// Original: FUN_002610a8
//
// Summary:
// - Reads one parameter from VM
// - Copies 12 bytes from entity audio data to defaults
// - Stores parameter to global
// - Sets audio mode flag to 0x20001
//
// Parameters:
// - param0: Audio parameter value (stored to DAT_003551f8)
//
// Memory operations:
// - Source: 0x58bed0 (entity audio data)
// - Destination: 0x31e668 (default audio parameters)
// - Size: 12 bytes (0x0C)
//
// Side effects:
// - Copies audio data from entity to defaults (reverse of 0x8D)
// - Sets DAT_003551f8 = param0
// - Sets DAT_003551ec = 0x20001 (different mode than 0x8D)
//
// PS2-specific notes:
// - Audio mode switching (0x20001 vs 0x40001 in 0x8D)
// - Saves current entity audio state as new defaults
// - Likely used for audio context switching
// - Parameter may control audio bank or preset

#include <stdint.h>

// VM evaluator (analyzed name for FUN_0025c258)
extern void bytecode_interpreter(void *result_out);

// Memory copy function
extern void FUN_00267da0(uint32_t dest, uint32_t src, uint32_t size);

// Audio mode flag and parameter
extern uint32_t DAT_003551ec;
extern uint32_t DAT_003551f8;

// Original signature: undefined8 FUN_002610a8(void)
uint64_t opcode_0x8e_set_audio_mode_with_param(void)
{
  uint32_t param;

  // Read parameter from VM
  bytecode_interpreter(&param);

  // Copy 12 bytes from entity audio to defaults
  // Source: 0x58bed0 (entity audio)
  // Dest: 0x31e668 (defaults)
  FUN_00267da0(0x31e668, 0x58bed0, 0x0C);

  // Store parameter
  DAT_003551f8 = param;

  // Set audio mode to 0x20001 (different from 0x8D's 0x40001)
  DAT_003551ec = 0x20001;

  return 0;
}

// Original signature preserved for cross-reference
// undefined8 FUN_002610a8(void)

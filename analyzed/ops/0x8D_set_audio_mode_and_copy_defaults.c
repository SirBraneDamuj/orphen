// Opcode 0x8D — set_audio_mode_and_copy_defaults
// Original: FUN_00261068
//
// Summary:
// - Sets audio mode flag to 0x40001
// - Copies 12 bytes (0x0C) of default audio data
// - No parameters read from VM
//
// Memory operations:
// - Source: 0x31e668 (default audio parameters)
// - Destination: 0x325340 (active audio state)
// - Size: 12 bytes (0x0C)
//
// Side effects:
// - Sets DAT_003551ec = 0x40001 (audio mode flag)
// - Copies audio defaults via FUN_00267da0
//
// PS2-specific notes:
// - Audio system initialization/reset
// - 0x40001 mode likely enables specific audio features
// - Default parameters restored from ROM/data section
// - FUN_00267da0 is memory copy function (memcpy-like)

#include <stdint.h>

// Memory copy function
extern void FUN_00267da0(uint32_t dest, uint32_t src, uint32_t size);

// Audio mode flag
extern uint32_t DAT_003551ec;

// Original signature: undefined8 FUN_00261068(void)
uint64_t opcode_0x8d_set_audio_mode_and_copy_defaults(void)
{
  // Set audio mode to 0x40001
  DAT_003551ec = 0x40001;

  // Copy 12 bytes of default audio data
  // Source: 0x31e668 (defaults)
  // Dest: 0x325340 (active state)
  FUN_00267da0(0x325340, 0x31e668, 0x0C);

  return 0;
}

// Original signature preserved for cross-reference
// undefined8 FUN_00261068(void)

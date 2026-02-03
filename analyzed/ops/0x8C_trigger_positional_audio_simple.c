// Opcode 0x8C — trigger_positional_audio_simple
// Original: FUN_00260f78
//
// Summary:
// - Reads 6 parameters from VM (3 base params + 3 coordinates)
// - Simplified version of 0x8B with no mask check or return value
// - Always executes if system not busy (iGpffffb27c == 0)
// - Normalizes coordinates by fGpffff8cb8 (different scale than 0x8B)
//
// Parameters:
// - param0: Audio parameter 1
// - param1: Audio parameter 2
// - param2: Flags (bits control behavior)
// - param3: X coordinate (normalized)
// - param4: Y coordinate (normalized)
// - param5: Z coordinate (normalized)
//
// Flag bits (param2):
// - 0x200000: Call FUN_0025d610 (fade/transition trigger)
// - 0x02: Dispatch event 0xC with buffer=1, set DAT_0058bf10=10
//
// Differences from 0x8B:
// - No mask check (always triggers if system ready)
// - No audio enable check (DAT_0058bebc)
// - No force flag (0x10000)
// - Different normalization scale (fGpffff8cb8 vs fGpffff8cb4)
// - Returns 0 (void-like), not success flag
//
// Side effects:
// - Calls FUN_0022b2c0 with normalized 3D coordinates
// - May call FUN_0025d610 (transition trigger)
// - May dispatch event via FUN_0025d1c0
// - Sets DAT_0058bf10 to 10 if flag 0x02 set
//
// PS2-specific notes:
// - Simplified 3D audio trigger without gating
// - Different coordinate scale suggests different unit system
// - Used for unconditional audio effects

#include <stdint.h>

// VM evaluator (analyzed name for FUN_0025c258)
extern void bytecode_interpreter(void *result_out);

// 3D audio function with coordinates
extern void FUN_0022b2c0(float x, float y, float z, uint32_t param1, uint32_t param2, uint32_t flags);

// Event dispatcher
extern void FUN_0025d1c0(int buffer_select, uint16_t event_id, int param);

// Transition/fade trigger
extern void FUN_0025d610(void);

// Globals
extern int iGpffffb27c;       // System state/busy flag
extern uint16_t DAT_0058bf10; // Audio state counter
extern float fGpffff8cb8;     // Coordinate normalization scale

// Original signature: undefined8 FUN_00260f78(void)
uint64_t opcode_0x8c_trigger_positional_audio_simple(void)
{
  uint32_t param1;
  uint32_t param2;
  uint32_t flags;
  int coordX;
  int coordY;
  int coordZ;

  // Read 6 parameters from VM
  bytecode_interpreter(&param1);
  bytecode_interpreter(&param2);
  bytecode_interpreter(&flags);
  bytecode_interpreter(&coordX);
  bytecode_interpreter(&coordY);
  bytecode_interpreter(&coordZ);

  // Execute if system not busy (no mask or enable checks)
  if (iGpffffb27c == 0)
  {
    // Handle transition trigger
    if ((flags & 0x200000) != 0)
    {
      FUN_0025d610();
    }

    // Handle event dispatch
    if ((flags & 0x02) != 0)
    {
      FUN_0025d1c0(1, 0x0C, 0);
      DAT_0058bf10 = 10;
    }

    // Call 3D audio function with normalized coordinates
    FUN_0022b2c0(
        (float)coordX / fGpffff8cb8,
        (float)coordY / fGpffff8cb8,
        (float)coordZ / fGpffff8cb8,
        param1, param2, flags);
  }

  return 0;
}

// Original signature preserved for cross-reference
// undefined8 FUN_00260f78(void)

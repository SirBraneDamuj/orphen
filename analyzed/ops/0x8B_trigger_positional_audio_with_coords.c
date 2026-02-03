// Opcode 0x8B — trigger_positional_audio_with_coords
// Original: FUN_00260e30
//
// Summary:
// - Reads 7 parameters from VM (4 base params + 3 coordinates)
// - Similar to 0x8A but includes 3D position coordinates
// - Normalizes coordinates by fGpffff8cb4 before calling audio function
// - Returns 1 if triggered, 0 otherwise
//
// Parameters:
// - param0: Mask value (checked against DAT_0058bf1c)
// - param1: Audio parameter 1
// - param2: Audio parameter 2
// - param3: Flags (bits control behavior)
// - param4: X coordinate (normalized)
// - param5: Y coordinate (normalized)
// - param6: Z coordinate (normalized)
//
// Flag bits (param3):
// - 0x10000: Force execution bypass
// - 0x200000: Call FUN_0025d610 (fade/transition trigger)
// - 0x02: Dispatch event 0xC with buffer=1, set DAT_0058bf10=10
//
// Conditions:
// - Same gating as 0x8A (system state, mask check, audio enable)
//
// Side effects:
// - Calls FUN_0022b2c0 with normalized 3D coordinates
// - May call FUN_0025d610 (transition trigger)
// - May dispatch event via FUN_0025d1c0
// - Sets DAT_0058bf10 to 10 if flag 0x02 set
//
// PS2-specific notes:
// - 3D positional audio trigger
// - Coordinates normalized by fGpffff8cb4
// - FUN_0022b2c0 likely handles 3D spatial audio
// - Similar gating to 0x8A but with position data

#include <stdint.h>
#include <stdbool.h>

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
extern uint32_t DAT_0058bf1c; // Entity audio permission flags
extern uint32_t DAT_0058bebc; // Audio enable flag
extern uint16_t DAT_0058bf10; // Audio state counter
extern float fGpffff8cb4;     // Coordinate normalization scale

// Original signature: undefined4 FUN_00260e30(void)
uint32_t opcode_0x8b_trigger_positional_audio_with_coords(void)
{
  uint32_t mask;
  uint32_t param1;
  uint32_t param2;
  uint32_t flags;
  int coordX;
  int coordY;
  int coordZ;
  uint32_t result;

  // Read 7 parameters from VM
  bytecode_interpreter(&mask);
  bytecode_interpreter(&param1);
  bytecode_interpreter(&param2);
  bytecode_interpreter(&flags);
  bytecode_interpreter(&coordX);
  bytecode_interpreter(&coordY);
  bytecode_interpreter(&coordZ);

  result = 0;

  // Check system state
  if (iGpffffb27c == 0)
  {
    // Check mask against entity audio flags
    if ((DAT_0058bf1c & mask) == 0)
    {
      result = 0;
    }
    else
    {
      // Execute if forced (bit 0x10000) or audio enabled
      if (((flags & 0x10000) != 0) || (((DAT_0058bebc ^ 1) & 1) == 0))
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
            (float)coordX / fGpffff8cb4,
            (float)coordY / fGpffff8cb4,
            (float)coordZ / fGpffff8cb4,
            param1, param2, flags);
        result = 1;
      }
    }
  }

  return result;
}

// Original signature preserved for cross-reference
// undefined4 FUN_00260e30(void)

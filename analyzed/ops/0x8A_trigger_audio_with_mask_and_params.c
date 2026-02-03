// Opcode 0x8A — trigger_audio_with_mask_and_params
// Original: FUN_00260d30
//
// Summary:
// - Reads 5 parameters from VM
// - Checks system state (iGpffffb27c) and entity flags (DAT_0058bf1c)
// - Conditionally sets flags, triggers events, and calls audio function
// - Returns 1 if triggered, 0 otherwise
//
// Parameters:
// - param0: Mask value (checked against DAT_0058bf1c)
// - param1: Audio parameter 1
// - param2: Audio parameter 2
// - param3: Flags (bits control behavior)
// - param4: Audio ID or additional parameter
//
// Flag bits (param3):
// - 0x10000: Force execution bypass (trigger even if DAT_0058bebc check fails)
// - 0x200000: Call FUN_0025d610 (fade/transition trigger)
// - 0x02: Dispatch event 0xC with buffer=1, set DAT_0058bf10=10
// - 0x10: Set bit in flags before call
//
// Conditions:
// - Skips if iGpffffb27c != 0 (system busy/disabled)
// - Skips if (DAT_0058bf1c & param0) == 0 (mask check)
// - Executes if bit 0x10000 set OR DAT_0058bebc == 0
//
// Side effects:
// - Calls FUN_0022b298 with normalized/flagged parameters
// - May call FUN_0025d610 (transition trigger)
// - May dispatch event via FUN_0025d1c0
// - Sets DAT_0058bf10 to 10 if flag 0x02 set
//
// PS2-specific notes:
// - Audio trigger with conditional gating
// - Entity flags at 0x58bf1c control audio permissions
// - DAT_0058bebc appears to be audio enable/disable flag
// - Complex flag system for audio control

#include <stdint.h>
#include <stdbool.h>

// VM evaluator (analyzed name for FUN_0025c258)
extern void bytecode_interpreter(void *result_out);

// Audio/effect function
extern void FUN_0022b298(uint32_t param1, uint32_t param2, uint32_t flags, uint32_t mask, uint32_t audio_id);

// Event dispatcher
extern void FUN_0025d1c0(int buffer_select, uint16_t event_id, int param);

// Transition/fade trigger
extern void FUN_0025d610(void);

// Globals
extern int iGpffffb27c;       // System state/busy flag
extern uint32_t DAT_0058bf1c; // Entity audio permission flags
extern uint32_t DAT_0058bebc; // Audio enable flag
extern uint16_t DAT_0058bf10; // Audio state counter

// Original signature: undefined4 FUN_00260d30(void)
uint32_t opcode_0x8a_trigger_audio_with_mask_and_params(void)
{
  uint32_t mask;
  uint32_t param1;
  uint32_t param2;
  uint32_t flags;
  uint32_t audioId;
  uint32_t result;

  // Read 5 parameters from VM
  bytecode_interpreter(&mask);
  bytecode_interpreter(&param1);
  bytecode_interpreter(&param2);
  bytecode_interpreter(&flags);
  bytecode_interpreter(&audioId);

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
        // Set bit 0x10 in flags
        flags |= 0x10;

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

        // Call audio function
        FUN_0022b298(param1, param2, flags, mask, audioId);
        result = 1;
      }
    }
  }

  return result;
}

// Original signature preserved for cross-reference
// undefined4 FUN_00260d30(void)

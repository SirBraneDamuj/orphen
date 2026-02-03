// Opcode 0x8F — get_frame_delta
// Original: UndefinedFunction_002610f8 / LAB_002610f8
//
// Summary:
// - Returns current frame time delta value
// - No parameters read from VM
// - Used for frame-rate independent calculations
//
// Return value:
// - uGpffffb64c: Frame time delta (varies with frame rate)
//
// Frame-rate independence:
// This global is used throughout the codebase in the pattern:
//   value * uGpffffb64c * 0.03125
// to normalize timing calculations across different frame rates.
//
// Related opcodes:
// - 0x6B: step_global_parameter_toward_target (uses delta * 0.03125)
// - 0x81/0x82: update_entity_angle_offset_parameter (uses delta * 0.03125)
// - 0x91: param_ramp_current_toward_target (uses delta / 32)
//
// PS2-specific notes:
// - Frame delta varies based on PAL (50Hz) vs NTSC (60Hz)
// - Allows scripts to perform custom frame-independent interpolation
// - Typical usage: multiply rate by this value for smooth animation

#include <stdint.h>

// Frame time delta (updated each frame)
extern uint32_t uGpffffb64c;

// Original signature: undefined4 UndefinedFunction_002610f8(void)
uint32_t opcode_0x8f_get_frame_delta(void)
{
  return uGpffffb64c;
}

// Original signature preserved for cross-reference
// undefined4 UndefinedFunction_002610f8(void)

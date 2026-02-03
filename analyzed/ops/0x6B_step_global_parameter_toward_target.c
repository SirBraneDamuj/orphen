// Opcode 0x6B — step_global_parameter_toward_target
// Original: LAB_0025fc28 (not recognized as function by Ghidra)
//
// Summary:
// - Frame-rate independent interpolation stepper for global parameter fGpffffb6e8
// - Steps current value (fGpffffb6e8) toward target (fGpffffbd7c) by rate (fGpffffbd80)
// - Returns 1 when target reached, 0 while still interpolating
// - Works in conjunction with opcode 0x6A which sets up the parameters
//
// NO script parameters - operates purely on globals set by previous opcodes
//
// Algorithm:
// 1. Determine direction based on step rate sign (fGpffffbd80)
// 2. Check if target reached:
//    - If rate > 0 (moving up): check if current <= target
//    - If rate <= 0 (moving down): check if target <= current
// 3. If target NOT reached:
//    - current += rate * frame_time * 0.03125
//    - return 0 (interpolation in progress)
// 4. If target reached:
//    - return 1 (interpolation complete)
//
// Global Variables Used:
// - fGpffffbd7c: Target value (set by opcode 0x6A param0, transformed)
// - fGpffffbd80: Step rate (set by opcode 0x6A param1, can be positive or negative)
// - fGpffffb6e8: Current value (initialized to 1.0 by opcode 0x6A, stepped by this opcode)
// - iGpffffb64c: Frame delta time (game engine timer, frame-rate independence)
//
// Frame-Rate Independence:
// - Step amount = rate * frame_delta * (1/32)
// - iGpffffb64c appears to be frame time in some unit (ticks/cycles?)
// - 0.03125 = 1/32, likely normalizes frame time to seconds or standard units
// - This ensures smooth interpolation regardless of frame rate
//
// Typical Usage Pattern (with opcode 0x6A):
// 1. Opcode 0x6A: Set target (fGpffffbd7c), rate (fGpffffbd80), current=1.0 (fGpffffb6e8)
// 2. Loop: Call opcode 0x6B each frame
//    - Returns 0 while interpolating
//    - Returns 1 when target reached
// 3. Use fGpffffb6e8 as interpolated parameter for rendering/transforms
//
// Example Script Logic:
// ```
// 0x6A <target=0.5> <rate=-0.05> <unused>  // Set up interpolation from 1.0 → 0.5
// loop:
//   0x6B                                    // Step toward target
//   if result == 0 goto loop                // Continue if not done
// // fGpffffb6e8 now equals 0.5 (target reached)
// ```
//
// Use Cases:
// - Smooth camera transitions (distance, FOV)
// - Fade effects (alpha, brightness)
// - Animation blending (weights, scales)
// - Audio volume ramping
// - Any time-based parameter interpolation
//
// Direction Logic:
// - Positive rate: Moving from lower to higher value (0.0 → 1.0)
//   - Check: current <= target (haven't exceeded)
// - Negative rate: Moving from higher to lower value (1.0 → 0.0)
//   - Check: target <= current (haven't gone below)
// - This prevents overshoot in both directions
//
// PS2-specific notes:
// - Frame time (iGpffffb64c) likely comes from VSync counter or GS timer
// - The 1/32 scale factor suggests frame time is in units of 32 per standard unit
// - Smooth interpolation critical for 60fps (NTSC) vs 50fps (PAL) consistency
//
// Relationship to other opcodes:
// - Opcode 0x6A: Sets up interpolation parameters (target, rate, initial=1.0)
// - Opcode 0x6B: Steps interpolation each frame (this opcode)
// - Opcode 0x90-0x92: Similar audio parameter ramping system (different globals)
// - Opcode 0x9B: advance_fade_track - another interpolation system
//
// Why Ghidra didn't recognize as function:
// - Likely no direct calls to this address from other functions
// - Only accessed via function pointer table (PTR_LAB_0031e228[0x6B - 0x32])
// - Ghidra's function detection heuristics missed the indirect reference
// - Common issue with dispatch table-based architectures
//
// Returns:
// - 0: Interpolation in progress, target not yet reached
// - 1: Target reached, interpolation complete
//
// Original signature: undefined4 UndefinedFunction_0025fc28(void)

#include <stdint.h>
#include <stdbool.h>

// Global interpolation parameters (set by opcode 0x6A)
extern float fGpffffbd7c; // Target value
extern float fGpffffbd80; // Step rate (positive = increasing, negative = decreasing)
extern float fGpffffb6e8; // Current value (stepped by this function)

// Frame delta time (for frame-rate independence)
extern int32_t iGpffffb64c; // Frame time in engine ticks

// Analyzed implementation
uint32_t opcode_0x6b_step_global_parameter_toward_target(void)
{
  float target = fGpffffbd7c;
  float rate = fGpffffbd80;
  float current = fGpffffb6e8;
  bool target_reached;

  // Determine if target reached based on direction
  if (rate > 0.0f)
  {
    // Moving upward: check if current hasn't exceeded target
    target_reached = (current <= target);
  }
  else
  {
    // Moving downward: check if current hasn't gone below target
    target_reached = (target <= current);
  }

  // If target not yet reached, step toward it
  if (!target_reached)
  {
    // Frame-rate independent step:
    // current += rate * frame_time * (1/32)
    // The 0.03125 (1/32) likely normalizes frame_time units
    fGpffffb6e8 = current + (rate * (float)iGpffffb64c * 0.03125f);
    return 0; // Still interpolating
  }

  // Target reached
  return 1; // Interpolation complete
}

// Original signature wrapper
uint32_t LAB_0025fc28(void)
{
  return opcode_0x6b_step_global_parameter_toward_target();
}

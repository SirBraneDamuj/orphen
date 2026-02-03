// analyzed/ops/0xDE_set_audio_channel_parameter.c
// Original: FUN_00264f50
// Opcode: 0xDE
// Handler: Set audio channel parameter (volume/pitch/pan)

// Behavior:
// - Evaluates two expressions: raw_value (int) and channel_index.
// - Normalizes raw_value by dividing by fGpffff8d5c scale factor.
// - Calls FUN_0023bbd8(normalized_value, channel_index) to update audio channel state.
// - Returns 0 (standard opcode completion).

// Related:
// - FUN_0023bbd8: Audio channel parameter setter (checks cGpffffb656 guard flag).
// - Audio state: DAT_00571b50 array (4 slots * 12 bytes stride).
// - FUN_00229640: Validates channel index range (0x1C499FF < idx < 0x1D49A00).
// - FUN_00216868: Random channel selector (fallback when all slots occupied).

// PS2 Architecture:
// - Audio channel parameter system with 4 simultaneous tracks.
// - Parameters clamped to [0x3C, 0x100] range after normalization.
// - Guard flag cGpffffb656 can disable audio updates globally.

#include <stdint.h>

// External declarations
typedef void (*bytecode_evaluator_t)(void *);
extern bytecode_evaluator_t FUN_0025c258;                                 // Bytecode expression evaluator
extern void FUN_0023bbd8(float normalized_value, uint64_t channel_index); // Audio channel parameter setter
extern float fGpffff8d5c;                                                 // Audio parameter normalization scale factor

// Audio channel constants
extern int32_t DAT_00571b50[12]; // Audio channel state array (4 slots * 3 ints each)
extern char cGpffffb656;         // Audio update guard flag (0=enabled, !=0=disabled)

uint64_t opcode_0xde_set_audio_channel_parameter(void)
{
  uint32_t raw_value;
  int32_t channel_index;

  // Evaluate two expressions: raw parameter value and channel index
  FUN_0025c258(&raw_value);
  FUN_0025c258(&channel_index);

  // Normalize parameter value and set audio channel
  // FUN_0023bbd8 will:
  // 1. Check cGpffffb656 guard flag (early return if set)
  // 2. Convert to range via: (10.0 - normalized) * fGpffff86dc
  // 3. Clamp result to [0x3C, 0x100]
  // 4. Find free slot in DAT_00571b50 or use random slot
  // 5. Validate channel_index via FUN_00229640 (must be in [0x1C499FF, 0x1D49A00))
  // 6. Store channel state: slot[0]=channel_ptr, slot[1]=initial_value<<5
  FUN_0023bbd8((float)channel_index / fGpffff8d5c, raw_value);

  return 0;
}

/*
 * Function Call Hierarchy:
 *
 * opcode_0xde_set_audio_channel_parameter()
 *   ├─> FUN_0025c258(&raw_value)          [Eval expr: parameter value]
 *   ├─> FUN_0025c258(&channel_index)      [Eval expr: channel index]
 *   └─> FUN_0023bbd8(normalized, channel) [Set audio channel parameter]
 *         ├─> Check cGpffffb656 guard flag
 *         ├─> FUN_0030bd20((10.0 - normalized) * fGpffff86dc) [Convert to internal range]
 *         ├─> Clamp result to [0x3C, 0x100]
 *         ├─> Find free slot in DAT_00571b50 (4 slots)
 *         ├─> FUN_00216868() if all slots occupied [Random channel selector]
 *         ├─> FUN_00229640(channel_index) [Validate channel index]
 *         └─> Store state: (&DAT_00571b50)[slot*3] = channel_ptr
 *                          (&DAT_00571b54)[slot*3] = value<<5
 *
 * Audio Channel State Layout (DAT_00571b50):
 * - 4 slots, 12 bytes (3 ints) per slot
 * - slot[0]: Channel pointer (from FUN_00229640 validation)
 * - slot[1]: Scaled parameter value (initial_value << 5)
 * - slot[2]: Unknown/unused (not set by this function)
 *
 * Normalization Pipeline:
 * 1. Input: raw integer value, channel index
 * 2. Normalize: channel_index / fGpffff8d5c -> float in [0.0, 10.0] range
 * 3. Invert: (10.0 - normalized) * fGpffff86dc
 * 4. Clamp: max(0x3C, min(0x100, result))
 * 5. Scale: final_value << 5 (multiply by 32 for fixed-point)
 *
 * Guard Flag (cGpffffb656):
 * - When set (!=0), all audio channel updates are blocked
 * - Typical use: disable audio during cutscenes or loading
 *
 * Channel Index Validation:
 * - Valid range: (0x1C499FF, 0x1D49A00) = [29948416, 30711296)
 * - Range size: ~762K entries (likely audio sample/voice table)
 * - Invalid channels ignored silently
 *
 * Usage Patterns:
 * - Set volume: normalized value 0.0-10.0 (higher = quieter after inversion)
 * - Set pitch: similar range but affects playback rate
 * - Set pan: stereo positioning (L-R balance)
 * - Typical sequence: load audio -> 0xDE (set params) -> play command
 *
 * Related Opcodes:
 * - 0x90-0x92: audio_param_ramp family (gradual parameter changes)
 * - 0x93-0x94: Likely audio playback control
 * - 0xDF: initialize_camera_entity (battle logo, calls FUN_0025d5b8)
 *
 * Example Script Usage:
 * ```
 * 0xDE 0x80 0x1C50000   # Set channel 0x1C50000 param to 0x80/scale
 * ```
 */

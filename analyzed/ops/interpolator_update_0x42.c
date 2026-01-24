// Opcode 0x42 Interpolator Update Function (analyzed)
// Original: FUN_00217f38
//
// Summary:
// - Called during opcode 0x42 execution to update two sets of interpolated parameters.
// - Takes the current step (param_1) and total duration (param_2) to compute a normalized
//   progress ratio (step/duration), then passes this ratio to two instances of FUN_00266ce8
//   which performs the actual interpolation on different data structures.
// - Finally applies the interpolated results via FUN_00217d40 and FUN_00217d10.
//
// Inputs:
// - param_1 (int): Current step in the interpolation sequence (from iGpffffbd78 >> 5)
// - param_2 (int): Total duration of the interpolation (typically from the opcode parameter)
//
// Data Structures (unanalyzed addresses):
// - 0x55fad8: Source data structure #1 (input to interpolation)
// - 0x55f930: Destination data structure #1 (interpolated output)
// - 0x55fce0: Source data structure #2 (input to interpolation)
// - 0x55f940: Destination data structure #2 (interpolated output)
//
// Side Effects:
// - Calls FUN_00266ce8 twice to interpolate two separate data structures based on progress ratio.
// - Calls FUN_00217d40 with three consecutive floats from 0x55f930 (likely a 3D vector).
// - Calls FUN_00217d10 with three consecutive floats from 0x55f940 (likely another 3D vector).
// - These likely update camera position, entity position, or transformation matrices.
//
// Notes:
// - Progress ratio is computed as (float)step / (float)duration, range [0.0, 1.0].
// - The two data structure pairs (0x55fad8→0x55f930 and 0x55fce0→0x55f940) suggest this
//   interpolates two independent 3-component values (possibly position and rotation,
//   or two different positions for camera/target).
// - FUN_00217d40 and FUN_00217d10 likely apply the interpolated values to the rendering
//   pipeline or entity state (unanalyzed).

#include <stdint.h>

// Interpolation function - applies progress ratio to transform source→dest
extern void FUN_00266ce8(float progress_ratio, short *source_data, float *dest_data);

// Application functions - apply interpolated values (3D vectors)
extern void FUN_00217d40(float x, float y, float z);
extern void FUN_00217d10(float x, float y, float z);

// Global data structures at fixed addresses
extern short DAT_0055fad8[]; // Source data structure #1
extern float DAT_0055f930;   // Interpolated output #1 component X
extern float DAT_0055f934;   // Interpolated output #1 component Y
extern float DAT_0055f938;   // Interpolated output #1 component Z

extern short DAT_0055fce0[]; // Source data structure #2
extern float DAT_0055f940;   // Interpolated output #2 component X
extern float DAT_0055f944;   // Interpolated output #2 component Y
extern float DAT_0055f948;   // Interpolated output #2 component Z

// Original signature: void FUN_00217f38(int param_1, int param_2)
void interpolator_update_0x42(int step, int duration)
{
  // Compute normalized progress [0.0, 1.0]
  float progress = (float)step / (float)duration;

  // Interpolate two data structures based on progress
  FUN_00266ce8(progress, (short *)0x55fad8, (float *)0x55f930);
  FUN_00266ce8(progress, (short *)0x55fce0, (float *)0x55f940);

  // Apply interpolated 3D vector #1 (possibly position or camera target)
  FUN_00217d40(DAT_0055f930, DAT_0055f934, DAT_0055f938);

  // Apply interpolated 3D vector #2 (possibly rotation or camera position)
  FUN_00217d10(DAT_0055f940, DAT_0055f944, DAT_0055f948);
}

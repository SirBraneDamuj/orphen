// Opcode 0x44 Interpolator Update Function (analyzed)
// Original: FUN_00218158
//
// Summary:
// - Called during opcode 0x44 execution to update three sets of interpolated parameters.
// - Similar to opcode 0x42's updater (FUN_00217f38), but with an additional interpolation
//   step via FUN_00266988 and extra global variable updates at the end.
// - Computes normalized progress (step/duration), interpolates three data structures,
//   applies the results, and copies values between global state variables.
//
// Inputs:
// - param_1 (int): Current step in the interpolation sequence (from iGpffffbd78 >> 5)
// - param_2 (int): Total duration of the interpolation
//
// Data Structures (unanalyzed addresses):
// - 0x55fad8: Source data structure #1
// - 0x55f930: Destination data structure #1 (3 floats)
// - 0x55fce0: Source data structure #2
// - 0x55f940: Destination data structure #2 (3 floats)
// - 0x55f950: Source data structure #3 (unique to 0x44)
// - 0x355a88: Destination data structure #3 (unique to 0x44)
//
// Side Effects:
// - Calls FUN_00266ce8 twice (same as 0x42) for first two data structure pairs.
// - Calls FUN_00266988 once (unique to 0x44) for third data structure pair.
// - Calls FUN_00217d40 and FUN_00217d10 to apply interpolated 3D vectors.
// - Copies DAT_00355a88 → DAT_0035564c and DAT_00355a8c → DAT_00355658.
//
// Difference from 0x42:
// - Additional interpolation via FUN_00266988 (0x55f950 → 0x355a88).
// - Final two global variable assignments (0x355a88 → 0x35564c, 0x355a8c → 0x355658).
// - These extra operations suggest 0x44 interpolates more complex state (e.g., camera
//   position + rotation + FOV, or entity transform + additional parameters).
//
// Notes:
// - The copy operations at the end (DAT_00355a88 → DAT_0035564c) suggest a double-buffering
//   or state propagation pattern where interpolated values are committed to active state.
// - FUN_00266988 is structurally similar to FUN_00266ce8 (based on code similarity), likely
//   another interpolation routine with slightly different parameter count or behavior.

#include <stdint.h>

// Interpolation functions
extern void FUN_00266ce8(float progress_ratio, short *source_data, float *dest_data);
extern void FUN_00266988(float progress_ratio, short *source_data, float *dest_data);

// Application functions - apply interpolated values (3D vectors)
extern void FUN_00217d40(float x, float y, float z);
extern void FUN_00217d10(float x, float y, float z);

// Global data structures (same as 0x42)
extern float DAT_0055f930, DAT_0055f934, DAT_0055f938;
extern float DAT_0055f940, DAT_0055f944, DAT_0055f948;

// Additional data structures (unique to 0x44)
extern float DAT_00355a88; // Interpolated output #3 (copied to DAT_0035564c)
extern float DAT_00355a8c; // Interpolated output #3+4 (copied to DAT_00355658)
extern float DAT_0035564c; // Active state variable #1
extern float DAT_00355658; // Active state variable #2

// Original signature: void FUN_00218158(int param_1, int param_2)
void interpolator_update_0x44(int step, int duration)
{
  // Compute normalized progress [0.0, 1.0]
  float progress = (float)step / (float)duration;

  // Interpolate first two data structures (same as 0x42)
  FUN_00266ce8(progress, (short *)0x55fad8, (float *)0x55f930);
  FUN_00266ce8(progress, (short *)0x55fce0, (float *)0x55f940);

  // Interpolate third data structure (unique to 0x44)
  FUN_00266988(progress, (short *)0x55f950, (float *)0x355a88);

  // Apply interpolated 3D vectors (same as 0x42)
  FUN_00217d40(DAT_0055f930, DAT_0055f934, DAT_0055f938);
  FUN_00217d10(DAT_0055f940, DAT_0055f944, DAT_0055f948);

  // Commit interpolated values to active state (unique to 0x44)
  DAT_0035564c = DAT_00355a88;
  DAT_00355658 = DAT_00355a8c;
}

/**
 * Debug coordinate output function - outputs 3D coordinates from a structure
 *
 * This function extracts 3D floating-point coordinates from a data structure,
 * converts them to fixed-point values scaled by 1000, formats them as
 * "(%d, %d, %d)\n", and outputs the formatted string to the debug system.
 *
 * The function performs:
 * 1. Reads float values from offsets 0x20, 0x24, 0x28 in the input structure
 * 2. Scales each coordinate by 1000.0 (likely converting from world units to display units)
 * 3. Converts to fixed-point integers using float_to_fixed_point
 * 4. Formats using sprintf_variadic with format string "(%d, %d, %d)\n"
 * 5. Outputs the formatted string via debug_output_formatter
 *
 * This is commonly used for debugging 3D object positions, camera coordinates,
 * or other spatial data in the game engine.
 *
 * Original function: FUN_00269fa8
 * Address: 0x00269fa8
 *
 * @param coordinate_struct Pointer to structure containing float coordinates at offsets 0x20, 0x24, 0x28
 */
void debug_output_coordinates_from_struct(int coordinate_struct)
{
  int x_coord_fixed;
  int y_coord_fixed;
  int z_coord_fixed;
  char format_buffer[256];

  // Extract coordinates from structure and convert to fixed-point scaled by 1000
  x_coord_fixed = float_to_fixed_point(*(float *)(coordinate_struct + 0x20) * 1000.0);
  y_coord_fixed = float_to_fixed_point(*(float *)(coordinate_struct + 0x24) * 1000.0);
  z_coord_fixed = float_to_fixed_point(*(float *)(coordinate_struct + 0x28) * 1000.0);

  // Format coordinates as "(%d, %d, %d)\n"
  sprintf_variadic(format_buffer, 0x34d838, x_coord_fixed, y_coord_fixed, z_coord_fixed);

  // Output formatted string to debug system
  debug_output_formatter(format_buffer);

  return;
}

// Function prototypes for referenced functions:

/**
 * Float to fixed-point converter (already analyzed)
 * Original: FUN_0030bd20
 */
extern int float_to_fixed_point(float value);

/**
 * String formatting function with variadic arguments (already analyzed)
 * Original: FUN_0030c1d8
 */
extern void sprintf_variadic(char *buffer, int format_string_addr, int arg1, int arg2, int arg3);

/**
 * Debug output formatter (already analyzed)
 * Original: FUN_002681c0
 */
extern void debug_output_formatter(char *formatted_string);

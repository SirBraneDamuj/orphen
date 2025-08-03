/**
 * @file get_map_id_from_debug_selection.c
 * @brief Translates the two-part map selection from the debug menu into a single map ID.
 * @original_name FUN_001f75a8
 *
 * The debug menu allows selecting a map using two values: a major number (e.g., "MP08")
 * and a minor number (e.g., "16"). This function implements the logic to combine them
 * into the final map ID used by the file system.
 *
 * @param major_map_number The first map number selected in the debug menu (0-29).
 * @param minor_map_number The second map number selected (0-31).
 * @return The calculated map ID for the lookup table in MAP.BIN.
 */
int get_map_id_from_debug_selection(int major_map_number, int minor_map_number)
{
  unsigned int uVar1;

  uVar1 = 0;
  // The major number is used as a high-order part of the ID.
  // It's shifted left by 5, which is equivalent to multiplying by 32.
  if (major_map_number != 0)
  {
    uVar1 = (unsigned int)major_map_number << 5;
  }

  // The minor number is added to the result to get the final ID.
  // Formula: map_id = (major * 32) + minor
  return uVar1 + minor_map_number;
}

/*
 * Initialize Mini-Map Data Arrays - FUN_0022de88
 *
 * Initializes mini-map data structures by filling data arrays with default values.
 * This function clears/resets mini-map data buffers to prepare them for new
 * mini-map rendering operations.
 *
 * The function iterates through mini-map data entries and sets each 16-entry
 * block to 0xFFFF (likely indicating empty/uninitialized map cells).
 *
 * Array structure appears to be:
 * - Each entry is 0x20 bytes (32 bytes)
 * - Each entry contains 16 undefined2 values (16 * 2 = 32 bytes)
 * - Values are set to 0xFFFF (empty/default state)
 *
 * Original function: FUN_0022de88
 */

#include "orphen_globals.h"

// Mini-map globals not yet in orphen_globals.h
extern int iGpffffb714;    // Number of mini-map data entries
extern void *puGpffffbc74; // Mini-map data buffer pointer

/*
 * Initializes mini-map data arrays with default values
 *
 * Clears all mini-map data entries by setting them to 0xFFFF.
 * Each data entry is 32 bytes containing 16 two-byte values.
 *
 * The loop structure suggests a grid-based mini-map where each
 * cell can contain multiple data points (possibly terrain, objects, etc.)
 */
void initialize_minimap_data_arrays(void)
{
  int entry_offset;
  unsigned short *data_ptr;
  int value_index;
  int entry_count;

  entry_count = 0;

  if (0 < iGpffffb714)
  {
    entry_offset = 0;

    do
    {
      entry_count = entry_count + 1;

      // Set pointer to end of 16-value block (working backwards)
      // Each entry is 0x20 bytes, starting at offset 0x1e (30 decimal)
      data_ptr = (unsigned short *)((char *)puGpffffbc74 + entry_offset + 0x1e);

      // Initialize 16 values in this entry to 0xFFFF (empty state)
      value_index = 0xf; // 15 values (0-15)
      do
      {
        *data_ptr = 0xffff; // Set to empty/uninitialized
        value_index = value_index - 1;
        data_ptr = data_ptr - 1; // Move to previous value
      } while (-1 < value_index);

      // Move to next entry (32 bytes per entry)
      entry_offset = entry_count * 0x20;

    } while (entry_count < iGpffffb714);
  }
}

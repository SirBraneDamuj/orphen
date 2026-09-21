/*
 * Clear the vertex->primitive adjacency table - FUN_0022de88
 *
 * First pass of the slope map (see docs/minimap_disp_is_a_slope_map.md).
 * Fills one 0x20-byte row per collision-mesh vertex with 0xFFFF, giving each
 * vertex 16 empty owner slots. FUN_0022def0 then fills them in.
 *
 * iGpffffb714 (DAT_00355684) is the PSM2 vertex count, not a mini-map cell
 * count; puGpffffbc74 is the table base, which mode 0 parks at 0x01849A00.
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

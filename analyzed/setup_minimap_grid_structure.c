/*
 * Setup Mini-Map Grid Structure - FUN_0022def0
 *
 * Sets up the mini-map grid structure by copying reference data and building
 * lookup tables for mini-map rendering. This function appears to establish
 * the relationship between map grid coordinates and mini-map display data.
 *
 * The function processes a grid layout where:
 * - Each row processes 4 columns of data (2 bytes each = 8 bytes per row)
 * - Source data comes from iGpffffb740 + row_offset + 8
 * - Target data goes to mini-map buffer (iGpffffbc78)
 * - Also builds lookup tables in the main mini-map data buffer
 *
 * Grid structure suggests this is building coordinate mapping for translating
 * game world coordinates to mini-map display positions.
 *
 * Original function: FUN_0022def0
 */

#include "orphen_globals.h"

// Mini-map globals not yet in orphen_globals.h
extern int iGpffffb718;          // Mini-map grid height/rows
extern int iGpffffb740;          // Source grid data buffer
extern unsigned int uGpffffbc78; // Mini-map coordinate buffer
extern void *puGpffffbc74;       // Mini-map data buffer pointer

/*
 * Sets up mini-map grid coordinate system and lookup tables
 *
 * Processes a grid layout to establish coordinate mapping between
 * game world positions and mini-map display coordinates.
 *
 * For each grid position:
 * 1. Copies coordinate data from source buffer to mini-map buffer
 * 2. Finds available slot in mini-map data array
 * 3. Stores row index in the available slot for lookup
 *
 * Grid processing:
 * - Processes iGpffffb718 rows
 * - Each row has 4 columns (8 bytes total per row)
 * - Source data stride is 0x78 bytes per row
 */
void setup_minimap_grid_structure(void)
{
  short coordinate_value;
  int row_data_offset;
  int column_offset;
  short *source_coord_ptr;
  short *lookup_slot_ptr;
  int slot_index;
  int row_index;
  int column_index;
  int next_row;

  if (0 < iGpffffb718)
  {
    row_data_offset = 0;
    row_index = 0;

    do
    {
      column_index = 0;
      next_row = row_index + 1;
      column_offset = 0;

      do
      {
        column_index = column_index + 1;

        // Get pointer to source coordinate data
        // Source data at: base + row_offset + 8 + column_offset
        source_coord_ptr = (short *)(row_data_offset + iGpffffb740 + 8 + column_offset);

        slot_index = 0;

        // Copy coordinate to mini-map buffer
        // Target: mini-map buffer + (row * 8) + column_offset
        *(short *)(column_offset + row_index * 8 + uGpffffbc78) = *source_coord_ptr;

        coordinate_value = *source_coord_ptr;

        // Find available slot in mini-map data array for this coordinate
        while (true)
        {
          // Calculate pointer to lookup slot
          // Each coordinate entry is 0x20 bytes, each slot is 2 bytes
          lookup_slot_ptr = (short *)(slot_index * 2 + coordinate_value * 0x20 + (long)puGpffffbc74);

          // Check if slot is available (value >= 0 means occupied)
          if (*lookup_slot_ptr < 0)
            break;

          slot_index = slot_index + 1;

          // Maximum 16 slots per coordinate entry
          if (0xf < slot_index)
            goto next_column;

          coordinate_value = *source_coord_ptr;
        }

        // Store row index in available slot
        *lookup_slot_ptr = (short)row_index;

      next_column:
        // Move to next column (2 bytes per column)
        column_offset = column_index * 2;

      } while (column_index < 4); // Process 4 columns per row

      // Move to next row (0x78 bytes per row in source data)
      row_data_offset = next_row * 0x78;
      row_index = next_row;

    } while (next_row < iGpffffb718);
  }
}

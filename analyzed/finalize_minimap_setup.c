/*
 * Finalize Mini-Map Setup - FUN_0022dfb0
 *
 * Finalizes mini-map setup by processing coordinate pairs and calculating
 * directional/distance relationships between map points. This function appears
 * to build navigation or pathfinding data for the mini-map display.
 *
 * The function processes coordinate pairs within each grid row, calculates
 * angles and distances between points, and stores valid connections in the
 * mini-map data buffer for rendering.
 *
 * Key operations:
 * - Processes adjacent coordinate pairs in each grid row
 * - Calculates angles between coordinate pairs (in degrees)
 * - Applies distance/validity checks
 * - Stores valid connections in output buffer
 *
 * Original function: FUN_0022dfb0
 */

#include "orphen_globals.h"

// Forward declarations for functions not yet analyzed
extern long FUN_0022e2a0(short *coord_ptr, short row_index);                               // find_coordinate_index
extern long FUN_0022e1c8(short *coord_ptr, short row_index, long coord_index, int offset); // validate_coordinate_pair
extern void *FUN_0022e340(short *coord_ptr, long index);                                   // get_coordinate_data
extern float FUN_0022e438(void *coord1, void *coord2);                                     // calculate_angle_between_coordinates
extern short FUN_0030bd20(float value);                                                    // float_to_fixed_point

// Mini-map globals not yet in orphen_globals.h
extern int iGpffffb718;          // Mini-map grid height/rows
extern int iGpffffb740;          // Source grid data buffer
extern int iGpffffb73c;          // Additional map data buffer
extern unsigned int uGpffffbc7c; // Mini-map output buffer
extern short sGpffffbc80;        // Output buffer write index
extern float fGpffff8584;        // Angle scaling constant

/*
 * Finalizes mini-map setup by processing coordinate relationships
 *
 * For each row in the mini-map grid:
 * 1. Processes coordinate pairs within the row (up to 4 coordinates)
 * 2. Finds coordinate indices and validates coordinate pairs
 * 3. Calculates angles between valid coordinate pairs
 * 4. Applies filtering based on angle and map properties
 * 5. Stores valid connections in output buffer
 *
 * The output appears to be connection/path data for mini-map rendering,
 * possibly representing walkable paths or visual connections between
 * map locations.
 */
void finalize_minimap_setup(void)
{
  short start_coordinate;
  bool is_sequential;
  short current_coordinate;
  long coord_index;
  long pair_validation;
  void *coord1_data;
  void *coord2_data;
  int angle_degrees;
  int coordinate_offset;
  int row_index;
  int next_row;
  int output_buffer_index;
  short previous_coordinate;
  short row_index_short;
  int max_offset;
  float angle_radians;
  short temp_start_coord;
  short temp_current_coord;

  row_index = 0;

  if (0 < iGpffffb718)
  {
    do
    {
      next_row = row_index + 1;
      previous_coordinate = -1;
      coordinate_offset = 1;
      row_index_short = (short)row_index;

      // Get starting coordinate for this row
      start_coordinate = *(short *)(row_index * 0x78 + iGpffffb740 + 8);
      is_sequential = true;
      temp_start_coord = start_coordinate;

      do
      {
        temp_current_coord = start_coordinate;

        if (is_sequential)
        {
          // Get next coordinate in the row
          temp_current_coord = *(short *)(row_index * 0x78 + iGpffffb740 + coordinate_offset * 2 + 8);
        }

        // Check if this coordinate is the same as previous
        is_sequential = temp_current_coord == previous_coordinate;
        previous_coordinate = temp_current_coord;

        if (is_sequential)
        {
          // Reset if sequential, use original starting coordinate
          previous_coordinate = -1;
          temp_current_coord = start_coordinate;
        }

        // Find index for starting coordinate
        coord_index = FUN_0022e2a0(&temp_start_coord, row_index_short);

        // Validate coordinate pair relationship
        pair_validation = FUN_0022e1c8(&temp_start_coord, row_index_short, coord_index, coordinate_offset);

        if (pair_validation != 0)
        {
          angle_degrees = 999; // Default invalid angle

          if (-1 < coord_index)
          {
            // Calculate angle between coordinate pair
            coord1_data = FUN_0022e340(&temp_start_coord, row_index_short);
            coord2_data = FUN_0022e340(&temp_start_coord, coord_index);
            angle_radians = (float)FUN_0022e438(coord1_data, coord2_data);

            // Convert angle to degrees and scale
            current_coordinate = FUN_0030bd20((angle_radians * 360.0) / fGpffff8584);
            angle_degrees = (int)current_coordinate;
          }

          // Apply special angle adjustment for certain map properties
          if ((*(unsigned int *)((int)coord_index * 0x80 + iGpffffb73c + 0x70) & 2) != 0)
          {
            angle_degrees = (angle_degrees + -10) * 0x10000 >> 0x10;
          }

          // Only store connections with valid angles (> 0x31 = 49 degrees)
          if (0x31 < angle_degrees)
          {
            // Store connection data in output buffer
            output_buffer_index = sGpffffbc80 * 8 + uGpffffbc7c;

            *(short *)(output_buffer_index) = temp_start_coord;         // Start coordinate
            *(short *)(output_buffer_index + 2) = temp_current_coord;   // End coordinate
            *(short *)(output_buffer_index + 4) = (short)angle_degrees; // Connection angle

            sGpffffbc80 = sGpffffbc80 + 1; // Increment output index
          }
        }

        // Break if we've hit an invalid coordinate
        if (previous_coordinate < 0)
          break;

        coordinate_offset = coordinate_offset + 1;
        is_sequential = coordinate_offset < 4;
        temp_start_coord = temp_current_coord;

      } while (coordinate_offset < 5); // Process up to 4 coordinate pairs per row

      row_index = next_row;

    } while (next_row < iGpffffb718);
  }
}

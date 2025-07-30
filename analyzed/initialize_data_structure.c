#include <stdint.h>

// PS2 type definitions (matching Ghidra output)
typedef unsigned char undefined;
typedef unsigned char undefined1;
typedef unsigned short undefined2;
typedef unsigned int undefined4;
typedef unsigned short ushort;
typedef unsigned int uint;

// Forward declaration for referenced function
extern void setup_graphics_data(undefined4 coordinate_data, char graphics_mode); // Setup graphics coordinates (FUN_0025d0e0)

/**
 * Data structure initialization function - sets up configuration based on mode
 *
 * This function initializes one of two data structures based on the first parameter,
 * then configures various fields and calls another function with calculated parameters.
 *
 * The function appears to be setting up some kind of system configuration or
 * operation parameters, possibly related to graphics, audio, or data processing.
 *
 * Original function: FUN_0025d1c0
 * Address: 0x0025d1c0
 *
 * @param mode_selector Selects which data structure to use (0 or non-zero)
 * @param config_value Configuration value stored in structure
 * @param base_value Base value for calculations
 */
void initialize_data_structure(long mode_selector, ushort config_value, int base_value)
{
  ushort *structure_ptr;

  if (mode_selector == 0)
  {
    // Mode 0: Use first data structure
    data_structure_1 = 0x1fe0; // Initialize with specific value
    structure_ptr = &data_structure_1;
  }
  else
  {
    // Mode 1+: Use second data structure
    data_structure_2 = 0; // Initialize with zero
    structure_ptr = &data_structure_2;
  }

  // Configure structure fields:
  structure_ptr[5] = 0xa0;                  // Field at offset +10 = 0xa0 (160)
  structure_ptr[1] = config_value;          // Field at offset +2 = config_value
  *(int *)(structure_ptr + 2) = base_value; // Field at offset +4 = base_value (as int)

  // Calculate complex parameter and call related function:
  // Takes the first field, shifts it, multiplies by 0x1000000, adds base_value
  int calculated_param = base_value + ((int)((uint)*structure_ptr << 0x10) >> 0x15) * 0x1000000;
  setup_graphics_data(calculated_param, 1);

  // Clear field at offset +8
  structure_ptr[4] = 0;
  return;
}

// Global variables for data structures:

/**
 * First data structure (mode 0)
 * Original: DAT_00571dc0
 */
extern ushort data_structure_1;

/**
 * Second data structure (mode 1+)
 * Original: DAT_00571dd0
 */
extern ushort data_structure_2;

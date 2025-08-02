/*
 * Scene Processing with Work Flags - FUN_0025b778
 *
 * Main scene processing function that respects the SCEN WORK DISP debug flags.
 * This function processes scene elements and uses the scene work flags (0-127)
 * to control which elements are processed or displayed.
 *
 * When scene work flags are enabled via the debug menu, this function will
 * output debug information about which scene elements are being processed.
 *
 * The function processes:
 * - Scene objects from DAT_00355cf4 array (up to 62 objects)
 * - Additional scene data processing
 * - Scene work flag checking and debug output
 *
 * For each enabled scene work flag (0-127), it outputs debug information
 * including the flag index and associated scene data.
 *
 * Original function: FUN_0025b778
 */

#include "orphen_globals.h"

// Forward declarations for functions not yet analyzed
extern void scene_command_interpreter(int scene_data_ptr); // FUN_0025bc68 - Bytecode interpreter for NPC actions/scripts
extern void FUN_0025ce30(void);                            // scene_preprocessing
extern void FUN_0025cfb8(void);                            // scene_postprocessing
extern void FUN_002681c0(int format_addr, ...);            // debug_output_formatter

// Scene processing globals (not yet in orphen_globals.h)
extern int DAT_0035503c;             // Scene processing state
extern int DAT_00355058;             // Scene data pointer
extern int DAT_00355cf4;             // Scene objects array pointer
extern int DAT_00355cf8;             // Current scene object index
extern unsigned char DAT_003555dd;   // Debug output flags
extern void *DAT_00355044;           // Scene buffer pointer 1
extern void *DAT_00355048;           // Scene buffer pointer 2
extern int DAT_00355060;             // Scene work data array
extern unsigned int DAT_0031e770[4]; // Scene work flags array (128 bits)

/*
 * Processes scene elements with scene work flag checking
 *
 * Main scene processing loop that:
 * 1. Processes scene objects from the scene objects array
 * 2. Handles special scene buffer processing
 * 3. Checks scene work flags and outputs debug info for enabled flags
 *
 * The scene work flags (0-127) control which scene elements get
 * debug output. When a flag is enabled via SCEN WORK DISP menu,
 * this function will print debug information about the corresponding
 * scene element including its index and associated data.
 */
void process_scene_with_work_flags(void)
{
  unsigned int bit_mask;
  int scene_data_index;
  int object_index;
  int scene_object_ptr;
  int bit_index;
  unsigned int flag_value;
  unsigned int *flag_array_ptr;
  int scene_work_index;

  // Initialize scene processing state
  DAT_0035503c = 0;

  // Process main scene data
  scene_command_interpreter(*(int *)(DAT_00355058 + 8) + DAT_00355058);

  // Run scene preprocessing
  FUN_0025ce30();

  // Process scene objects array (up to 62 objects)
  object_index = 0;
  if (DAT_00355cf4 != 0)
  {
    do
    {
      scene_object_ptr = *(int *)(object_index * 4 + DAT_00355cf4);
      if (scene_object_ptr != 0)
      {
        // Debug output for scene object processing (Subproc debug info)
        // Format: "Subproc:%3d [%5d]\n"
        // First value: object_index (0-61)
        // Second value: *(scene_object_ptr + -4) - likely current script instruction pointer or action state ID
        if ((DAT_003555dd & 0x80) != 0)
        {
          FUN_002681c0(0x34ca60, object_index, *(unsigned int *)(scene_object_ptr + -4));
        }

        // Set current object index and process
        DAT_00355cf8 = object_index;
        scene_command_interpreter(*(unsigned int *)(object_index * 4 + DAT_00355cf4));
      }
      object_index = object_index + 1;
    } while (object_index < 0x3e); // Process up to 62 objects

    // Reset current object index
    DAT_00355cf8 = -1;

    // Handle special scene buffer processing
    if (*(int *)(DAT_00355cf4 + 0x100) != 0)
    {
      DAT_00355044 = (void *)0x0058beb0;
      DAT_00355048 = (void *)0x0058beb0;
      scene_command_interpreter(0); // Special buffer processing with null command sequence
    }
  }

  // Run scene postprocessing
  FUN_0025cfb8();

  // Check scene work flags and output debug information
  flag_array_ptr = DAT_0031e770;
  object_index = 0;

  do
  {
    scene_data_index = object_index << 7; // object_index * 128
    bit_mask = 1;
    bit_index = 0;
    scene_work_index = object_index + 1;

    // Check each bit in current flag array element (32 bits)
    do
    {
      int global_flag_index = object_index * 0x20 + bit_index; // Global flag index (0-127)
      bit_index = bit_index + 1;
      flag_value = *flag_array_ptr & bit_mask;
      bit_mask = bit_mask << 1;

      // If this scene work flag is enabled, output debug info
      if (flag_value != 0)
      {
        FUN_002681c0(0x34ca78, global_flag_index,
                     *(unsigned int *)(scene_data_index + DAT_00355060),
                     *(unsigned int *)(scene_data_index + DAT_00355060));
      }

      scene_data_index = scene_data_index + 4; // Move to next data element
    } while (bit_index < 0x20); // Process 32 bits per array element

    flag_array_ptr = flag_array_ptr + 1; // Move to next flag array element
    object_index = scene_work_index;
  } while (scene_work_index < 4); // Process all 4 flag array elements (128 total flags)
}

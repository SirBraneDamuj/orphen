/*
 * Mini-Map Display Controller - FUN_0022dd60
 *
 * Controls the mini-map display system based on the debug menu MINI_MAP_DISP option.
 * This function handles initialization of mini-map buffers and rendering when the
 * mini-map debug option is enabled.
 *
 * The function operates in two modes:
 * - Mode 0: Initialize mini-map display system (called when option is enabled)
 * - Mode 1: Update/render mini-map display (called during game loop)
 *
 * When initializing (mode 0), it sets up memory buffers, initializes display
 * structures, and prepares the mini-map rendering system.
 *
 * Original function: FUN_0022dd60
 */

#include "orphen_globals.h"

// Forward declarations for analyzed functions
extern void initialize_minimap_data_arrays(void);     // FUN_0022de88
extern void setup_minimap_grid_structure(void);       // FUN_0022def0
extern void finalize_minimap_setup(void);             // FUN_0022dfb0
extern void FUN_0022e7b0(int addr);                   // process_minimap_data
extern void FUN_0022e638(void);                       // update_minimap_display
extern void FUN_0022e7b8(void);                       // render_minimap_elements
extern void FUN_0022e528(void);                       // minimap_post_processing
extern void FUN_0020bc78(int src_addr, int dst_addr); // copy_minimap_buffer

// Debug logging function (already identified)
extern void FUN_002681c0(int format_addr, ...); // debug_print_formatted

// Mini-map system globals (not yet in orphen_globals.h)
extern unsigned int uGpffffb7bc; // Memory pointer/offset for mini-map data
extern unsigned int uGpffffbc78; // Mini-map buffer start address
extern unsigned int uGpffffbc7c; // Mini-map buffer end address
extern void *puGpffffbc74;       // Mini-map data pointer
extern unsigned int uGpffffbc80; // Mini-map state flag
extern unsigned int uGpffffbc82; // Mini-map counter/index
extern int iGpffffb718;          // Mini-map dimension/size parameter
extern int DAT_0031c210;         // Mini-map display parameter
extern int DAT_0031c214;         // Mini-map display parameter
extern float DAT_0031c21c;       // Mini-map coordinate/scale value
extern int DAT_0031c218;         // Mini-map display parameter
extern float fGpffffb6d4;        // Game coordinate value
extern float fGpffff8580;        // Reference coordinate value

/*
 * Controls mini-map display system initialization and updates
 *
 * Parameters:
 *   mode - Operation mode:
 *          0: Initialize mini-map display system
 *          1: Update and render mini-map display
 *
 * Mode 0 (Initialize):
 * - Sets up memory buffers for mini-map data
 * - Initializes display structures and parameters
 * - Calls helper functions to prepare mini-map system
 *
 * Mode 1 (Update/Render):
 * - Updates mini-map coordinate calculations
 * - Processes and renders mini-map elements
 * - Copies display buffers for final output
 */
void minimap_display_controller(int mode)
{
  int saved_memory_ptr;

  saved_memory_ptr = uGpffffb7bc;

  if (mode == 0)
  {
    // Initialize mini-map display system

    // Set up memory buffer alignment (align to 4-byte boundary)
    uGpffffbc78 = uGpffffb7bc + 3 & 0xfffffffc;
    uGpffffb7bc = uGpffffbc78 + iGpffffb718 * 8;

    // Initialize mini-map data pointer to base address
    puGpffffbc74 = (void *)0x01849a00; // Mini-map data base address

    // Reset mini-map state
    uGpffffbc80 = 0; // Clear state flags

    // Initialize mini-map data structures
    initialize_minimap_data_arrays(); // Initialize mini-map data arrays
    setup_minimap_grid_structure();   // Setup mini-map grid/layout

    // Reset counters and display parameters
    uGpffffbc82 = 0;  // Reset mini-map counter
    DAT_0031c210 = 0; // Reset display parameter

    // Align memory pointer and set buffer end
    uGpffffb7bc = uGpffffb7bc + 3 & 0xfffffffc;
    DAT_0031c214 = 0;          // Reset display parameter
    DAT_0031c21c = 0.0;        // Reset coordinate value
    DAT_0031c218 = 0;          // Reset display parameter
    uGpffffbc7c = uGpffffb7bc; // Set buffer end address

    // Finalize mini-map setup
    finalize_minimap_setup();
  }
  else if (mode == 1)
  {
    // Update and render mini-map display

    // Debug log mini-map update
    FUN_002681c0(0x34c1c8, 0x32); // Print debug message

    // Update mini-map coordinate calculation
    DAT_0031c21c = -(fGpffffb6d4 - fGpffff8580);

    // Process and render mini-map elements
    FUN_0022e7b0(0x31c210); // Process mini-map data
    FUN_0022e638();         // Update mini-map display
    FUN_0022e7b8();         // Render mini-map elements
    FUN_0022e528();         // Post-processing

    // Copy final mini-map buffer for display
    FUN_0020bc78(0x58bd40, 0x58bc80);

    return;
  }

  // Restore original memory pointer
  uGpffffb7bc = saved_memory_ptr;
}

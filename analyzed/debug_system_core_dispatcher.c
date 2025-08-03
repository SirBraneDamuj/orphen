/*
 * Debug System Core Dispatcher - FUN_00304bf0
 *
 * Central dispatcher for the debug system that handles debug command execution
 * based on parameter patterns. This function is called through the semaphore-
 * protected wrapper (FUN_00204c50) and processes debug commands from various
 * debug menu options including MAP SELECT.
 *
 * Command Processing:
 * - param_1: Debug category (0 = MAP SELECT category)
 * - param_2: Debug command code (0x4034/0x4032 for MAP SELECT operations)
 * - Dispatches to actual debug function executors via FUN_002f4b10
 *
 * Parameter Routing:
 * - 0x6240: DAT_01d4f948 = *DAT_01d4f944 (read operation)
 * - 0x8100: piGpffffba14 = DAT_01d4f944 (set pointer)
 * - 0x8200: piGpffffba18 = DAT_01d4f944 (set pointer)
 * - 0x8300-0x8600: Various global pointer assignments
 * - 0x7600: Special text processing with address calculation
 * - 0x6000-0x6FFF: Debug operations (MAP SELECT uses this range)
 * - 0x7000-0x7FFF: Advanced debug operations
 *
 * MAP SELECT Execution Path:
 * comprehensive_debug_menu_handler (FUN_00269140)
 *   → FUN_00205f98 (debug function state manager)
 *   → FUN_00204c50 (semaphore-protected wrapper)
 *   → debug_system_core_dispatcher (FUN_00304bf0) ← THIS FUNCTION
 *   → FUN_002f4b10 (resource-based debug executor)
 *   → Map loading system (FUN_00223268 → FUN_00221b48 → MAP.BIN lookup)
 *
 * Original function: FUN_00304bf0
 * Referenced by: 40+ functions in globals.json (major debug system hub)
 */

#include "orphen_globals.h"

// Forward declarations for debug system functions
extern void FUN_0030c0c0(int addr);                                           // Text processing function
extern void FUN_002f4b10(int format_addr, uint param2, bool param3, int arg4, // Debug function executor
                         int arg5, int arg6, int arg7, uint arg8);

// Debug system global variables (not yet in orphen_globals.h)
extern uint uGpffffba00;  // Default value for param_1 == 0 case
extern int *piGpffffba04; // Global pointer 1 (0x8300)
extern int *piGpffffba08; // Global pointer 2 (0x8400)
extern int *piGpffffba0c; // Global pointer 3 (0x8500)
extern int *piGpffffba14; // Global pointer 4 (0x8100)
extern int *piGpffffba10; // Global pointer 5 (0x8600)
extern int *piGpffffba18; // Global pointer 6 (0x8200)
extern int *DAT_01d4f940; // Debug data structure base
extern int *DAT_01d4f944; // Debug data array access
extern int DAT_01d4f948;  // Debug read/write value (0x6240)

/**
 * Central debug system dispatcher
 *
 * This function routes debug commands from the debug menu system to specific
 * debug implementations based on parameter patterns. MAP SELECT operations
 * use the 0x6000-0x6FFF range to trigger map loading functionality.
 *
 * For MAP SELECT:
 * - Category 0 (param_1 = 0) indicates map-related debug operations
 * - Command codes 0x4034/0x4032 route through the 0x6000 range handler
 * - Eventually connects to map loading system via FUN_00223268 (type 2 = MAP.BIN)
 */
void debug_system_core_dispatcher(long param_1, uint param_2, undefined8 param_3,
                                  undefined8 param_4, undefined8 param_5, undefined8 param_6,
                                  undefined8 param_7, undefined8 param_8)
{
  bool is_param1_zero;
  int calculated_offset;
  undefined8 *stack_ptr;
  undefined4 *data_ptr;
  undefined4 stack_value;
  undefined8 uStack_30;
  undefined8 uStack_28;
  undefined8 uStack_20;
  undefined8 uStack_18;
  undefined8 uStack_10;
  undefined8 uStack_8;

  // Set up debug data structure access
  data_ptr = &DAT_01d4f944;
  calculated_offset = 5;
  DAT_01d4f940 = &DAT_01d4f940;
  stack_ptr = &uStack_30;

  // Copy parameter stack to debug data structure
  do
  {
    calculated_offset = calculated_offset - 1;
    *data_ptr = *(undefined4 *)stack_ptr;
    data_ptr = data_ptr + 1;
    stack_ptr = stack_ptr + 1;
  } while (-1 < calculated_offset);

  is_param1_zero = param_1 == 0;
  stack_value = 0;

  // Use default value if param_1 is 0 (MAP SELECT category)
  if (is_param1_zero)
  {
    stack_value = uGpffffba00;
  }

  // Handle specific debug command codes
  if ((int)param_2 < 0x8100)
  {
    // Read operations (0x6240)
    if (param_2 == 0x6240)
    {
      DAT_01d4f948 = *DAT_01d4f944; // Read current debug value
    }
  }
  else
  {
    // Pointer assignment operations (0x8100-0x8600)
    if (param_2 == 0x8300)
    {
      piGpffffba04 = DAT_01d4f944;
      return;
    }
    if (param_2 == 0x8400)
    {
      piGpffffba08 = DAT_01d4f944;
      return;
    }
    if (param_2 == 0x8500)
    {
      piGpffffba0c = DAT_01d4f944;
      return;
    }
    if (param_2 == 0x8100)
    {
      piGpffffba14 = DAT_01d4f944;
    }
    else if (param_2 == 0x8600)
    {
      piGpffffba10 = DAT_01d4f944;
    }
    else if (param_2 == 0x8200)
    {
      piGpffffba18 = DAT_01d4f944;
    }
  }

  // Preserve parameter stack for debug function calls
  uStack_30 = param_3;
  uStack_28 = param_4;
  uStack_20 = param_5;
  uStack_18 = param_6;
  uStack_10 = param_7;
  uStack_8 = param_8;

  // Handle special text processing command
  if (param_2 == 0x7600)
  {
    FUN_0030c0c0(0x3510b8);                // Process text at address
    calculated_offset = DAT_01d4f948 << 6; // Calculate offset (multiply by 64)
    param_2 = 0x7600;
  }
  else
  {
    // Route debug commands based on parameter range
    if ((param_2 & 0xf000) == 0x6000)
    {
      // MAP SELECT and similar debug operations (0x6000-0x6FFF)
      // This is where MAP SELECT commands (0x4034/0x4032) are processed
      FUN_002f4b10(0x34acf0, param_2, is_param1_zero, 0x1d4f940, 0x40,
                   DAT_01d4f944, 0x40, stack_value);
      return;
    }

    if ((param_2 & 0xf000) != 0x7000)
    {
      // Standard debug operations (not 0x7000-0x7FFF)
      FUN_002f4b10(0x34acf0, param_2, is_param1_zero, 0x1d4f940, 0x40,
                   0x1d4f940, 0x10, stack_value);
      return;
    }

    // Advanced debug operations (0x7000-0x7FFF)
    calculated_offset = 0x40;
  }

  // Execute debug function with calculated parameters
  FUN_002f4b10(0x34acf0, param_2, is_param1_zero, DAT_01d4f944,
               calculated_offset, 0, 0, stack_value);
  return;
}

/*
 * ANALYSIS SUMMARY: MAP SELECT Execution Chain
 *
 * 1. User selects MAP SELECT from comprehensive debug menu
 * 2. comprehensive_debug_menu_handler calls FUN_00205f98(0, 0)
 * 3. FUN_00205f98 calls FUN_00204c50(0x4034, debug_data) or FUN_00204c50(0x4032, debug_data)
 * 4. FUN_00204c50 provides semaphore protection around debug_system_core_dispatcher call
 * 5. debug_system_core_dispatcher routes 0x4034/0x4032 through 0x6000 range handler
 * 6. FUN_002f4b10 executes the actual map loading functionality
 * 7. Eventually connects to FUN_00223268 (main file loader) with type 2 (MAP.BIN)
 * 8. FUN_00221b48 performs MAP.BIN lookup table access to load specific maps
 *
 * The MAP SELECT option provides developers with direct access to the game's
 * map loading system, allowing them to test and debug individual maps from
 * the MAP.BIN archive without going through normal game progression.
 */

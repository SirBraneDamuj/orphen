/**
 * System Function Dispatcher
 *
 * This function dispatches to different system initialization/processing functions
 * based on an index stored in a global variable. It uses a function pointer array
 * to call the appropriate system function for the current game state or mode.
 *
 * The dispatch pattern:
 * - DAT_00354d2c: Index/state value that determines which function to call
 * - PTR_FUN_00318a88: Base address of function pointer array
 * - Uses the index to select and call the appropriate function from the array
 *
 * Analysis shows PTR_FUN_00318a88 points to FUN_00224218, which appears to be
 * a system initialization function that calls multiple subsystem initializers.
 *
 * This dispatcher is likely part of a state machine system that manages
 * different game modes or initialization phases.
 *
 * Original function: FUN_002241e0
 * Address: 0x002241e0
 */

#include "orphen_globals.h"

// Forward declarations
extern void (**system_function_table)(void); // PTR_FUN_00318a88 - Function pointer array
extern int system_function_index;            // DAT_00354d2c - Index into function array

/**
 * Dispatch to system function based on current index
 *
 * Calls the appropriate system function from the function pointer array
 * using the current system function index as the selector.
 */
void dispatch_system_function(void)
{
  // Call function from array using current index
  system_function_table[system_function_index]();

  return;
}

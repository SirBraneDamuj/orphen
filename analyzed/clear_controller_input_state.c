/*
 * Clear Controller Input State - FUN_002686a0
 *
 * Clears all controller input state variables and calls system input clearing function.
 * This function resets the controller input tracking globals to ensure clean state
 * after menu operations or when switching between input contexts.
 *
 * The function zeroes out multiple controller-related global variables that track
 * button states, input history, and processing flags across both controllers.
 *
 * Original function: FUN_002686a0
 */

#include "orphen_globals.h"

// Forward declarations for functions not yet analyzed
extern void FUN_0023bae8(void); // system_clear_input - clears system-level input state

/*
 * Clears all controller input state variables
 *
 * Resets controller input tracking globals to zero:
 * - Controller button state flags
 * - Input processing state variables
 * - Input history/timing variables
 * - Cross-controller input coordination flags
 *
 * Also calls system-level input clearing function to ensure
 * complete input state reset.
 */
void clear_controller_input_state(void)
{
  // Clear controller input state variables
  DAT_003555f6 = 0; // Controller 2 input state (includes Start, Circle buttons)
  DAT_003555e4 = 0; // Input processing state
  DAT_003555f4 = 0; // Controller 1 input state (includes D-pad, X, Triangle buttons)
  DAT_003555f8 = 0; // Input timing/history variable
  DAT_003555fa = 0; // Input coordination variable
  DAT_003555f0 = 0; // Input state variable
  DAT_003555ec = 0; // Input processing variable
  DAT_003555e8 = 0; // Input state variable

  // Clear system-level input state
  FUN_0023bae8();
}

#include <stdint.h>

// PS2 type definitions (matching Ghidra output)
typedef unsigned char undefined;
typedef unsigned char undefined1;
typedef unsigned short undefined2;
typedef unsigned int undefined4;
typedef unsigned int uint;

// Forward declaration for input history clear function
extern void clear_input_history_buffer(void);

/**
 * Refresh display and reset input state
 *
 * This function is called to refresh the display and reset various controller
 * input state variables. It's typically called after making changes to game
 * state (like toggling flags) to ensure the display is updated and input
 * state is properly reset.
 *
 * The function clears:
 * - Controller input states
 * - Analog stick values
 * - Input processing flags
 * - Input history buffer
 *
 * Original function: FUN_002686a0
 * Address: 0x002686a0
 */
void refresh_display(void)
{
  // Reset controller input states
  controller2_buttons_current = 0; // DAT_003555f6 - Controller 2 button state
  right_stick_y_processed = 0;     // DAT_003555e4 - Right analog stick Y
  controller1_buttons_current = 0; // DAT_003555f4 - Controller 1 button state
  controller1_buttons_mapped = 0;  // DAT_003555f8 - Controller 1 mapped buttons
  controller2_buttons_mapped = 0;  // DAT_003555fa - Controller 2 mapped buttons
  left_stick_x_processed = 0;      // DAT_003555f0 - Left analog stick X
  left_stick_y_processed = 0;      // DAT_003555ec - Left analog stick Y
  right_stick_x_processed = 0;     // DAT_003555e8 - Right analog stick X

  // Clear input history buffer
  clear_input_history_buffer();
  return;
}

// Global variable references:

/**
 * Controller 2 current button state
 * Original: DAT_003555f6
 */
extern uint controller2_buttons_current;

/**
 * Right analog stick Y processed value
 * Original: DAT_003555e4
 */
extern float right_stick_y_processed;

/**
 * Controller 1 current button state
 * Original: DAT_003555f4
 */
extern uint controller1_buttons_current;

/**
 * Controller 1 mapped button state
 * Original: DAT_003555f8
 */
extern uint controller1_buttons_mapped;

/**
 * Controller 2 mapped button state
 * Original: DAT_003555fa
 */
extern uint controller2_buttons_mapped;

/**
 * Left analog stick X processed value
 * Original: DAT_003555f0
 */
extern uint left_stick_x_processed;

/**
 * Left analog stick Y processed value
 * Original: DAT_003555ec
 */
extern uint left_stick_y_processed;

/**
 * Right analog stick X processed value
 * Original: DAT_003555e8
 */
extern uint right_stick_x_processed;

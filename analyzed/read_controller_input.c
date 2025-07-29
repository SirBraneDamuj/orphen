#include <stdint.h>

// PS2 type definitions (matching Ghidra output)
typedef unsigned char undefined;
typedef unsigned char undefined1;
typedef unsigned short undefined2;
typedef unsigned int undefined4;
typedef unsigned short ushort;
typedef short sshort;

// Forward declaration for controller input processing
extern void process_controller_input(long enable_history_logging);

/**
 * Controller input reader with repeat handling
 *
 * This function reads controller input and handles button repeat timing.
 * It's commonly used in menus and UI systems where button repeat is needed
 * for navigation (like holding a direction to scroll through options).
 *
 * Original function: FUN_0023b9f8
 * Address: 0x0023b9f8
 *
 * @param button_mask Bitmask of buttons to check for input
 * @param enable_sticky_input If non-zero, enables sticky/toggle input behavior
 * @return 1 if button input detected (with repeat handling), 0 otherwise
 */
undefined4 read_controller_input(ushort button_mask, long enable_sticky_input)
{
  int loop_counter;

  // Update input timer with delta increment
  input_repeat_timer = input_repeat_timer + timer_delta_increment;

  // Cap timer at maximum value (512 units)
  if (0x200 < input_repeat_timer)
  {
    input_repeat_timer = 0x200;
  }

  // Only process input if minimum time has elapsed (31+ units)
  if (0x1f < input_repeat_timer)
  {
    // Process controller input (no history logging)
    process_controller_input(0);

    // Handle sticky input mode
    if (enable_sticky_input != 0)
    {
      // OR the current input with previous sticky state, filtered by button mask
      sticky_input_state = sticky_input_state | filtered_input_state & button_mask;
    }

    // Check if any requested buttons are currently pressed
    if ((sticky_input_state & button_mask) == 0)
    {
      // No buttons pressed - reset repeat counter
      button_repeat_counter = 0;
    }
    else if (0 < input_repeat_timer)
    {
      // Button is held - handle repeat timing
      while (true)
      {
        button_repeat_counter = button_repeat_counter + 1;
        loop_counter = (int)button_repeat_counter;

        // Check for immediate response (first press) or repeat intervals
        if ((loop_counter == 1) ||
            ((0xc < loop_counter && ((loop_counter - 0xdU & 3) == 0))))
        {
          // First press OR repeat interval (every 4th frame after 13 frames)
          break;
        }

        // Decrement timer for next iteration
        input_repeat_timer = input_repeat_timer + -0x20;
        if (input_repeat_timer < 1)
        {
          // Timer expired - no input
          return 0;
        }
      }

      // Input detected - reset timer for next cycle
      input_repeat_timer = 0;
      return 1;
    }
  }

  // No input detected
  return 0;
}

// Global variables for input timing system:

/**
 * Input repeat timer - tracks timing for button repeat functionality
 * Original: iGpffffaf00
 */
extern int input_repeat_timer;

/**
 * Timer delta increment per frame
 * Original: iGpffffb64c
 */
extern int timer_delta_increment;

/**
 * Sticky input state - maintains button state across frames
 * Original: uGpffffb684
 */
extern ushort sticky_input_state;

/**
 * Filtered input state - current input filtered by some criteria
 * Original: uGpffffb68e
 */
extern ushort filtered_input_state;

/**
 * Button repeat counter - tracks how long a button has been held
 * Original: sGpffffaefe
 */
extern sshort button_repeat_counter;

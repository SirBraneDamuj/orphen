#include <stdint.h>

// PS2 type definitions (matching Ghidra output)
typedef unsigned char undefined;
typedef unsigned char undefined1;
typedef unsigned short undefined2;
typedef unsigned int undefined4;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned char byte;

// Forward declarations for referenced functions
extern void controller_state_change_handler(void);                                                  // FUN_0023af50
extern void process_analog_stick_input(byte x_input, byte y_input, uint *x_result, uint *y_result); // FUN_0023b3f0
extern ushort calculate_analog_magnitude(uint x_value, uint y_value);                               // FUN_0023b4e8
extern uint convert_analog_to_float(uint input_value);                                              // FUN_0023b358
extern float interpolate_analog_value(float current, uint target, float delta);                     // FUN_0023a320

/**
 * Controller input processing and state management function
 *
 * This function is the main controller input processor that handles:
 * - Controller state change detection
 * - Digital button input processing
 * - Analog stick input processing and smoothing
 * - Input history buffer management
 * - Controller port switching logic
 *
 * Original function: FUN_0023b5d8
 * Address: 0x0023b5d8
 *
 * @param enable_history_logging If non-zero, enables input history logging
 */
void process_controller_input(long enable_history_logging)
{
  ushort previous_buttons;
  uint controller2_input;
  int controller_port_offset;
  ushort current_buttons;
  uint controller1_input;
  undefined4 analog_converted;
  float analog_interpolated;

  // Store previous button state
  previous_buttons = current_controller_buttons;

  // Check if controller configuration has changed
  if (current_controller_config != previous_controller_config)
  {
    previous_controller_config = current_controller_config;
    controller_state_change_handler();
  }

  // Process controller 1 input
  if (controller1_connected == '\0')
  {
    // Controller 1 is connected - read inverted input (active low)
    controller1_input = ~(uint)CONCAT11(controller1_input_high, controller1_input_low) & 0xffff;
  }
  else
  {
    // Controller 1 disconnected
    controller1_input = 0;
  }

  // Process controller 2 input if dual controller mode is enabled
  if (dual_controller_mode != 0)
  {
    if (controller2_connected == '\0')
    {
      // Controller 2 is connected - read inverted input (active low)
      controller2_input = ~(uint)CONCAT11(controller2_input_high, controller2_input_low) & 0xffff;
    }
    else
    {
      // Controller 2 disconnected
      controller2_input = 0;
    }

    // Handle controller switching logic
    if (controller1_input == 0)
    {
      if (controller2_input != 0)
      {
        // Switch to controller 2
        active_controller_port = '\x01';
        controller1_input = controller2_input;
      }
    }
    else
    {
      // Stay with controller 1
      active_controller_port = '\0';
    }
  }

  // Calculate controller data offset based on active port
  controller_port_offset = active_controller_port * 0x20;

  // Check if this is an analog controller with right stick at center (type 7)
  if ((byte)controller_data_array[controller_port_offset + 1] >> 4 == 7)
  {
    // Process analog stick input
    analog_stick_active = -1;

    // Process left analog stick
    process_analog_stick_input(
        controller_data_array[controller_port_offset + 4],
        controller_data_array[controller_port_offset + 5],
        &left_stick_x_processed,
        &left_stick_y_processed);

    // Process right analog stick
    process_analog_stick_input(
        controller_data_array[active_controller_port * 0x20 + 6],
        controller_data_array[active_controller_port * 0x20 + 7],
        &right_stick_x_processed,
        &right_stick_y_processed);

    // Calculate combined analog magnitude
    current_controller_buttons = calculate_analog_magnitude(right_stick_x_processed, right_stick_y_processed);
  }
  else
  {
    // Digital controller or analog sticks not active
    analog_stick_active = '\0';
    controller1_input = 0;
  }

  current_buttons = (ushort)controller1_input;

  // Process analog stick smoothing and interpolation
  if ('\0' < analog_stick_active)
  {
    if ((controller1_input & 0xf000) == 0)
    {
      // No analog input - reset values
      right_stick_x_processed = 0;
      right_stick_y_processed = 0.0;
    }
    else if ((previous_analog_input & 0xf000) == 0)
    {
      // First analog input - initialize
      right_stick_y_processed = (float)convert_analog_to_float(controller1_input);
      right_stick_x_processed = 0x43000000; // Float constant
    }
    else
    {
      // Smooth analog input transition
      analog_converted = convert_analog_to_float(controller1_input);
      analog_interpolated = (float)interpolate_analog_value(
          right_stick_y_processed,
          analog_converted,
          (float)analog_sensitivity * delta_time);
      right_stick_y_processed = right_stick_y_processed + analog_interpolated;
      right_stick_x_processed = 0x43000000; // Float constant
    }

    // Reset left stick values
    left_stick_x_processed = 0;
    left_stick_y_processed = 0;
    previous_analog_input = current_buttons;
  }

  // Update controller state
  controller_state_flags = 0;
  controller1_buttons_current = current_buttons;

  // Handle input history logging if enabled
  if (enable_history_logging != 0)
  {
    // Calculate new button presses (current & ~previous)
    controller2_buttons_current = (ushort)(controller1_input & ~(uint)previous_input_mask);

    // Apply input mapping table
    controller2_input = (uint)input_mapping_table[controller1_input & 0xff] | controller1_input & 0xff00;
    controller1_buttons_mapped = (undefined2)controller2_input;

    // Update input history ring buffer
    input_history_index = input_history_index + 1 & 0x3f; // Wrap at 64 entries

    // Store mapped input for controller 2
    controller2_buttons_mapped = (ushort)input_mapping_table[controller1_input & ~(uint)previous_input_mask & 0xff] |
                                 controller2_buttons_current & 0xff00;

    // Update history counters
    input_history_count = input_history_count + 1;

    // Calculate button state changes
    button_state_changes = current_controller_buttons & ~previous_buttons;

    // Reset state tracking
    button_repeat_counter = 0;
    previous_input_mask = current_buttons;

    // Store combined input in history buffer (32-bit: high 16 = mapped1, low 16 = mapped2)
    input_history_buffer[input_history_index] = controller2_input << 0x10 | (uint)controller2_buttons_mapped;

    // Reset input processing flags
    input_processing_flags = 0;

    // Limit history count to maximum
    if (0x40 < input_history_count)
    {
      input_history_count = 0x40; // Cap at 64 entries
    }
  }
  return;
}

// Global variables for controller input system:

/**
 * Current controller button state
 * Original: DAT_003555fe
 */
extern ushort current_controller_buttons;

/**
 * Current controller configuration
 * Original: DAT_003555ac
 */
extern uint current_controller_config;

/**
 * Previous controller configuration
 * Original: DAT_00354e68
 */
extern uint previous_controller_config;

/**
 * Controller 1 connection status (0=connected, non-zero=disconnected)
 * Original: DAT_00571a00
 */
extern char controller1_connected;

/**
 * Controller 1 input data (high byte)
 * Original: DAT_00571a02
 */
extern byte controller1_input_high;

/**
 * Controller 1 input data (low byte)
 * Original: DAT_00571a03
 */
extern byte controller1_input_low;

/**
 * Dual controller mode flag
 * Original: DAT_00354e50
 */
extern int dual_controller_mode;

/**
 * Controller 2 connection status
 * Original: DAT_00571a20
 */
extern char controller2_connected;

/**
 * Controller 2 input data (high byte)
 * Original: DAT_00571a22
 */
extern byte controller2_input_high;

/**
 * Controller 2 input data (low byte)
 * Original: DAT_00571a23
 */
extern byte controller2_input_low;

/**
 * Active controller port (0=controller1, 1=controller2)
 * Original: DAT_003555df
 */
extern char active_controller_port;

/**
 * Controller data array (contains raw controller data)
 * Original: DAT_00571a01
 */
extern byte controller_data_array[];

/**
 * Analog stick active flag (-1=active, 0=inactive)
 * Original: DAT_003555e0
 */
extern char analog_stick_active;

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

/**
 * Right analog stick Y processed value
 * Original: DAT_003555e4
 */
extern float right_stick_y_processed;

/**
 * Previous analog input state
 * Original: DAT_00354e6c
 */
extern ushort previous_analog_input;

/**
 * Analog sensitivity setting
 * Original: DAT_003555bc
 */
extern float analog_sensitivity;

/**
 * Delta time for analog interpolation
 * Original: DAT_00352648
 */
extern float delta_time;

/**
 * Controller state flags
 * Original: DAT_00354e52
 */
extern int controller_state_flags;

/**
 * Controller 1 current button state
 * Original: DAT_003555f4
 */
extern ushort controller1_buttons_current;

/**
 * Controller 2 current button state
 * Original: DAT_003555f6
 */
extern ushort controller2_buttons_current;

/**
 * Controller 1 mapped button state
 * Original: DAT_003555f8
 */
extern undefined2 controller1_buttons_mapped;

/**
 * Input history ring buffer index
 * Original: DAT_00355614
 */
extern int input_history_index;

/**
 * Controller 2 mapped button state
 * Original: DAT_003555fa
 */
extern ushort controller2_buttons_mapped;

/**
 * Input history entry count
 * Original: DAT_00355618
 */
extern int input_history_count;

/**
 * Button state changes (new presses)
 * Original: DAT_00355600
 */
extern ushort button_state_changes;

/**
 * Button repeat counter
 * Original: DAT_00354e54
 */
extern int button_repeat_counter;

/**
 * Previous input mask for change detection
 * Original: DAT_00354e56
 */
extern ushort previous_input_mask;

/**
 * Input mapping table for button remapping
 * Original: DAT_00571a50
 */
extern byte input_mapping_table[256];

/**
 * Input history ring buffer (64 entries)
 * Original: DAT_00342a70
 */
extern uint input_history_buffer[64];

/**
 * Input processing flags
 * Original: DAT_003555fc
 */
extern int input_processing_flags;

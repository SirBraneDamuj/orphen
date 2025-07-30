#include "orphen_globals.h"

// Forward declarations for referenced functions
extern void clear_memory_buffer(void);                                                                // Clear 256-byte buffer (FUN_0025bc30)
extern void initialize_data_structure(long mode_selector, ushort config_value, int base_value);       // Initialize system config (FUN_0025d1c0)
extern int get_flag_state(int flag_id);                                                               // Get flag state (FUN_00266368)
extern bool is_system_busy(void);                                                                     // Check if system is busy/blocked (FUN_00237c60)
extern void initialize_menu_system(void);                                                             // Initialize menu system (FUN_00231a98)
extern void FUN_00213ef0(void);                                                                       // Unknown function
extern int get_text_resource(int text_index);                                                         // Get text resource by index (FUN_0025b9e8)
extern short calculate_text_width(char *text_string, int scale);                                      // Calculate text width (FUN_00238e68)
extern void FUN_00238608(int x, int y, undefined8 param3, unsigned int color, int width, int height); // Unknown function
extern long FUN_00206218(int index);                                                                  // Unknown function - check entity status?
extern long FUN_00206238(int index);                                                                  // Unknown function - check entity state?
extern undefined4 FUN_002061f8(int index);                                                            // Unknown function - get entity data?
extern void FUN_00206260(int index, int param2, int param3);                                          // Unknown function - set entity data?
extern void FUN_002063c8(int index, int param2);                                                      // Unknown function
extern void play_menu_back_sound(void);                                                               // Play menu back/cancel sound effect (FUN_002256b0)
extern void FUN_002241d8(void);                                                                       // Unknown function

/**
 * Game system manager function - handles state transitions and entity management
 *
 * This function appears to be a complex game system manager that handles:
 * - State machine logic with multiple states
 * - Flag-based conditional execution
 * - Entity/object management (2 entities in a loop)
 * - Controller input processing
 * - Timer/counter decrements
 * - Text/UI rendering in certain states
 *
 * The function uses two specific flags:
 * - Flag 1288 (0x508): Disables system if set
 * - Flag 1298 (0x512): Triggers special action when set
 *
 * Original function: FUN_00224ff0
 * Address: 0x00224ff0
 *
 * @return 0 if system should exit/sleep, 1 if system should continue running
 */
undefined4 game_system_manager(void)
{
  int entity_index;
  undefined4 entity_data;
  long flag_result;
  undefined8 text_resource;
  int *entity_ptr;
  undefined4 *entity_data_ptr;

  // Early exit conditions - system disabled states
  if (system_disable_flag != 0)
  {
    return 0;
  }
  if (system_state == '\x02') // State 2 - system inactive
  {
    return 0;
  }

  // Handle state 1 with timer countdown
  if ((system_state == '\x01') && (countdown_timer = countdown_timer - timer_delta, countdown_timer < 1))
  {
    clear_memory_buffer();
    initialize_data_structure(1, 0xc, 0);
    mode_state = 2;
    system_state = 2;
    return 0;
  }

  // Check flag 1288 - exit if this flag is set
  flag_result = get_flag_state(0x508); // Flag 1288
  if (flag_result != 0)
  {
    return 0; // System disabled by flag
  }

  // Additional system state checks
  if (system_busy_flag != 0)
  {
    return 0;
  }
  if (mode_state != 0)
  {
    return 0;
  }
  if ((0 < scene_loading_counter) && (system_state == '\0'))
  {
    return 0;
  }

  // State-dependent logic branches
  if (primary_mode == '\0')
  {
    // Primary mode - check for invalid states
    if (game_mode == 0)
    {
      return 0;
    }
    if (game_mode == 0xc) // Mode 12
    {
      return 0;
    }
    if (game_mode == 0xd) // Mode 13
    {
      return 0;
    }
  }
  else if (secondary_mode == 0x1f) // Mode 31
  {
    return 0;
  }

  // Additional blocking condition
  if (blocking_flag != '\0')
  {
    return 0;
  }

  // Handle text rendering in state 1
  if (system_state == '\x01')
  {
    text_resource = get_text_resource(0x26);                                              // Get text resource
    entity_index = calculate_text_width((char *)text_resource, 0x20);                     // Calculate text width
    FUN_00238608(-entity_index / 2, 0x10, text_resource, 0xffffffff80808080, 0x20, 0x20); // Render centered text
  }

  // Main state machine logic
  if (main_state == 2)
  {
    // State 2: Active processing mode
    if ((controller_input & 0x800) == 0) // Button not pressed
    {
      return 0;
    }

    // Handle toggle input
    if (((secondary_input & 0x80) != 0) && (toggle_flag != '\0'))
    {
      boolean_toggle = boolean_toggle ^ 1; // XOR toggle
    }

    // Process entities in active state
    entity_index = 0;
    entity_ptr = (int *)&entity_array_start;
    do
    {
      if (-1 < *entity_ptr) // Entity is valid
      {
        FUN_002063c8(entity_index, 0x19); // Process entity
      }
      entity_index = entity_index + 1;
      entity_ptr = entity_ptr + 1;
    } while (entity_index < 2);

    counter_value = 0;
  }
  else
  {
    // Non-state-2 processing
    if ((controller_input & 0x840) == 0) // Buttons 0x800 and 0x40 not pressed
    {
      // Additional state checks
      if (primary_mode != '\0')
      {
        return 0;
      }
      if (system_busy_flag != 0)
      {
        return 0;
      }
      if (scene_data != 0)
      {
        return 0;
      }

      // Check if system is busy with critical operations
      flag_result = is_system_busy();
      if (flag_result != 0)
      {
        return 0;
      }
      if (main_state == 2)
      {
        return 0;
      }

      // Handle specific button combinations
      if ((controller_input & 0x5000) != 0) // Buttons 0x5000
      {
        main_state = 4;
        initialize_menu_system();
        return 0;
      }
      if ((controller_input & 0x8000) == 0) // Button 0x8000 not pressed
      {
        return 0;
      }

      // Check flag 1298 and handle special case
      flag_result = get_flag_state(0x512); // Flag 1298
      if (flag_result != 0)
      {
        FUN_00213ef0();
        global_flags = global_flags | 1;
        main_state = 0xc; // State 12
        return 0;
      }
      return 0;
    }

    // Handle state transition from state 1
    if (system_state == '\x01')
    {
      initialize_data_structure(1, 0xc, 0);
      system_state = 2;
      fade_value = 0xff;
      mode_state = 2;
      FUN_002241d8();
      return 0;
    }

    // Initialize entity processing
    entity_index = 0;
    if ((controller_input & 0x800) != 0) // Button 0x800 pressed
    {
      entity_data_ptr = (undefined4 *)&entity_array_start;
      do
      {
        // Check entity status
        flag_result = FUN_00206218(entity_index);
        if (flag_result < 1)
        {
          *entity_data_ptr = 0xffffffff; // Mark entity as invalid
        }
        else
        {
          // Check entity state
          flag_result = FUN_00206238(entity_index);
          if (flag_result == 0)
          {
            // Entity is ready - get data and set parameters
            entity_data = FUN_002061f8(entity_index);
            *entity_data_ptr = entity_data;
            FUN_00206260(entity_index, 0x19, 500); // Set entity parameters
          }
          else
          {
            *entity_data_ptr = 0xffffffff; // Mark entity as invalid
          }
        }
        entity_index = entity_index + 1;
        entity_data_ptr = entity_data_ptr + 1;
      } while (entity_index < 2);

      main_state = 1;

      // Determine next state based on conditions
      if ((condition_flag1 == '\0') && (main_state = 1, condition_flag2 == '\0'))
      {
        main_state = 2;
      }

      play_menu_back_sound();
      return 1;
    }
  }
  return 1;
}

// Global variables for this system:

/**
 * System disable flag
 * Original: sGpffffb0ec
 */
extern sshort system_disable_flag;

/**
 * System state (0=inactive, 1=countdown, 2=active)
 * Original: cGpffffb656
 */
extern cchar system_state;

/**
 * Countdown timer value
 * Original: iGpffffb65c
 */
extern int countdown_timer;

/**
 * Timer delta for countdown
 * Original: iGpffffb64c
 */
extern int timer_delta;

/**
 * Mode state value
 * Original: iGpffffb27c
 */
extern int mode_state;

/**
 * System busy flag
 * Original: iGpffffb0e4
 */
extern int system_busy_flag;

/**
 * Scene loading counter
 * Original: DAT_0058c7e8
 */
extern int scene_loading_counter;

/**
 * Primary mode flag
 * Original: cGpffffb663
 */
extern cchar primary_mode;

/**
 * Game mode value
 * Original: iGpffffb284
 */
extern int game_mode;

/**
 * Secondary mode value
 * Original: iGpffffb288
 */
extern int secondary_mode;

/**
 * Blocking flag
 * Original: cGpffffb6d0
 */
extern cchar blocking_flag;

/**
 * Main state machine value
 * Original: iGpffffadbc
 */
extern int main_state;

/**
 * Controller input state
 * Original: uGpffffb686
 */
extern ushort controller_input;

/**
 * Secondary input state
 * Original: uGpffffb668
 */
extern ushort secondary_input;

/**
 * Toggle flag
 * Original: cGpffffb66a
 */
extern cchar toggle_flag;

/**
 * Boolean toggle state
 * Original: bGpffffb66c
 */
extern byte boolean_toggle;

/**
 * Entity array start address
 * Original: gp0xffffbc58
 */
extern int entity_array_start;

/**
 * Counter value
 * Original: uGpffffaf26
 */
extern ushort counter_value;

/**
 * Scene data pointer/flag
 * Original: DAT_0058bf10
 */
extern int scene_data;

/**
 * Global flags bitfield
 * Original: DAT_0058cba0
 */
extern int global_flags;

/**
 * Fade value
 * Original: uGpffffb662
 */
extern ushort fade_value;

/**
 * Condition flag 1
 * Original: cGpffffb657
 */
extern cchar condition_flag1;

/**
 * Condition flag 2
 * Original: cGpffffb663
 */
extern cchar condition_flag2;

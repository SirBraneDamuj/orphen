#include "orphen_globals.h"

// PS2 type definitions for this function
typedef unsigned long ulong;

// Forward declarations for referenced functions
extern void FUN_00206048(int sound_slot, undefined2 param2);                                   // Sound slot management
extern void FUN_00204d10(int command, undefined param2, undefined2 param3, undefined2 param4); // Low-level sound command

/**
 * Update sound parameters with change detection and slot management
 *
 * This function manages sound parameter updates with intelligent change detection:
 * 1. Tracks time since last update to avoid excessive changes (minimum 4 frame gap)
 * 2. Compares new parameters against current values to detect changes
 * 3. Updates sound slot data structures with new parameters
 * 4. Triggers low-level sound system commands when parameters change
 * 5. Manages sound slot activation and configuration
 *
 * The function uses several data arrays:
 * - frame_timestamps: Tracks last update time per sound (DAT_00314bb0)
 * - sound_parameters: Stores current sound parameter data (DAT_003567xx range)
 * - sound_configs: Extended sound configuration data
 *
 * Original function: FUN_00206128
 * Address: 0x00206128
 *
 * @param sound_id Sound identifier (0-based, gets +3 offset for internal slot ID)
 * @param param2 Sound parameter 2 (volume/pan/etc)
 * @param param3 Sound parameter 3 (volume/pan/etc)
 */
void update_sound_parameters(int sound_id, ulong param2, ulong param3)
{
  undefined2 current_config;
  int time_delta;
  int slot_id;

  // Calculate internal slot ID (sound_id + 3)
  slot_id = sound_id + 3;

  // Check time since last update to prevent excessive changes
  time_delta = current_frame_time - frame_timestamps[sound_id];
  if (time_delta < 0)
  {
    time_delta = -time_delta;
  }

  // Only update if enough time has passed (minimum 4 frames)
  if (3 < time_delta)
  {
    int param_offset = slot_id * 0x2c; // Calculate parameter block offset (44 bytes per slot)

    // Update timestamp for this sound
    frame_timestamps[sound_id] = current_frame_time;

    // Check if parameters have changed
    if ((byte)sound_param2_array[param_offset] == param2)
    {
      if ((byte)sound_param3_array[param_offset] == param3)
      {
        return; // No change in parameters, exit early
      }
      current_config = sound_config_array[param_offset];
    }
    else
    {
      current_config = sound_config_array[param_offset];
    }

    // Update parameter values
    sound_param2_array[param_offset] = (char)param2;
    sound_param3_array[param_offset] = (char)param3;

    // Notify sound slot manager of parameter change
    FUN_00206048(slot_id, current_config);

    // If sound slot is active (type > 1), send low-level command
    if ('\x01' < (char)sound_type_array[param_offset])
    {
      FUN_00204d10(0x4043,                             // Sound command type
                   sound_name_array[slot_id * 0xb],    // Sound name/identifier
                   sound_config1_array[param_offset],  // Config parameter 1
                   sound_config2_array[param_offset]); // Config parameter 2
    }
  }
  return;
}

// Global variables for sound parameter management:

/**
 * Current frame time for change detection
 * Original: DAT_003555b4
 */
extern int current_frame_time;

/**
 * Frame timestamps for each sound slot (tracks last update time)
 * Original: DAT_00314bb0 (array indexed by sound_id * 4)
 */
extern int frame_timestamps[];

/**
 * Sound parameter 2 array (volume/pan/etc)
 * Original: DAT_003567b6 (array indexed by slot_id * 0x2c + offset)
 */
extern char sound_param2_array[];

/**
 * Sound parameter 3 array (volume/pan/etc)
 * Original: DAT_003567b7 (array indexed by slot_id * 0x2c + offset)
 */
extern char sound_param3_array[];

/**
 * Sound configuration array
 * Original: DAT_003567b4 (array indexed by slot_id * 0x2c + offset)
 */
extern undefined2 sound_config_array[];

/**
 * Sound type array (indicates if sound slot is active)
 * Original: DAT_003567b0 (array indexed by slot_id * 0x2c + offset)
 */
extern char sound_type_array[];

/**
 * Sound name/identifier array
 * Original: DAT_003567d8 (array indexed by slot_id * 0xb)
 */
extern undefined sound_name_array[];

/**
 * Sound configuration parameter 1 array
 * Original: DAT_003567b8 (array indexed by slot_id * 0x2c + offset)
 */
extern undefined2 sound_config1_array[];

/**
 * Sound configuration parameter 2 array
 * Original: DAT_003567ba (array indexed by slot_id * 0x2c + offset)
 */
extern undefined2 sound_config2_array[];

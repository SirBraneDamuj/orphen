#include "orphen_globals.h"

// Forward declarations for referenced functions
extern void FUN_00206048(int sound_slot, undefined2 param2); // Sound slot management
extern void FUN_00204d10(int command, ...);                  // Low-level sound command (variadic)

/**
 * Calculate sound envelope fade with distance-based attenuation
 *
 * This function performs envelope calculations for sound effects based on distance
 * and applies fade-in/fade-out effects with configurable attenuation curves:
 * 1. Validates sound slot is active (type >= 2)
 * 2. Triggers sound command for active fade states (type 3-4)
 * 3. Calculates volume difference using current vs target levels
 * 4. Applies distance-based attenuation multipliers
 * 5. Sets fade state and duration in sound slot data
 * 6. Updates sound slot management system
 *
 * The envelope calculation uses a normalized formula:
 * volume_delta = (current_level * (param6 + param7)) / 2000 - (target_level * (param6 + param7)) / 2000
 *
 * Distance attenuation multipliers:
 * - Very close (param2 <= 2): 4x multiplier
 * - Close (param2 <= 4): 3x multiplier
 * - Medium (param2 <= 8): 2x multiplier
 * - Far (param2 > 8): 1x multiplier
 * - Very far (param2 > 13): 0.5x multiplier
 *
 * Original function: FUN_00206260
 * Address: 0x00206260
 *
 * @param sound_id Sound identifier (0-based, gets +3 offset for internal slot ID)
 * @param distance Distance factor for attenuation calculation
 * @param target_level Target volume/envelope level
 */
void calculate_sound_envelope_fade(int sound_id, long distance, undefined8 target_level)
{
  int volume_delta;
  int fade_duration;
  int slot_id;
  int param_offset;

  // Calculate internal slot ID (sound_id + 3)
  slot_id = sound_id + 3;
  param_offset = slot_id * 0x2c; // Calculate parameter block offset (44 bytes per slot)

  // Check if sound slot is active (type must be >= 2)
  if ((char)sound_type_array[param_offset] < '\x02')
  {
    return;
  }

  // If sound is in active fade state (type 3-4), send low-level sound command
  if ((byte)sound_type_array[param_offset] - 3 < 2)
  {
    FUN_00204d10(0x4043,                                               // Sound command type for envelope
                 sound_name_array[slot_id * 0xb],                      // Sound name/identifier
                 *(undefined2 *)(sound_config1_array + param_offset),  // Config parameter 1
                 *(undefined2 *)(sound_config2_array + param_offset)); // Config parameter 2
  }

  // Calculate volume difference between current and target levels
  // Formula: (current_level * combined_params) / 2000 - (target_level * combined_params) / 2000
  volume_delta = (int)((int)*(short *)(sound_level_array + param_offset) *
                       ((uint)(byte)sound_param6_array[param_offset] +
                        (uint)(byte)sound_param7_array[param_offset])) /
                     2000 -
                 (int)((int)target_level *
                       ((uint)(byte)sound_param6_array[param_offset] +
                        (uint)(byte)sound_param7_array[param_offset])) /
                     2000;

  // Only apply fade if volume needs to decrease (positive delta)
  if (0 < volume_delta)
  {
    // Distance-based attenuation multiplier
    fade_duration = 4; // Default far distance multiplier
    if (distance < 8)
    {
      if (distance < 4)
      {
        if (1 < distance)
        {
          fade_duration = 3; // Close distance
        }
        // else: very close distance keeps default 4x
      }
      else
      {
        fade_duration = 2; // Medium distance
      }
    }
    else
    {
      fade_duration = 1; // Far distance
    }

    // Apply distance multiplier to volume delta
    fade_duration = fade_duration * volume_delta;

    // Very far distance gets additional 50% reduction
    if (0xd < distance)
    {
      fade_duration = fade_duration / 2;
    }

    // Set sound slot to fade state (type 3)
    sound_type_array[param_offset] = 3;

    // Set fade duration with base offset of 10 frames
    *(short *)(sound_fade_duration_array + param_offset) = (short)fade_duration + 10;

    // Trigger sound command for new fade state
    FUN_00204d10(0x4037,                           // Sound command type for fade start
                 sound_name_array[slot_id * 0xb]); // Sound name/identifier
  }

  // Update sound slot management system
  FUN_00206048(slot_id, target_level);
  return;
}

// Global sound parameter arrays (44-byte structures per slot):

/**
 * Sound type/state array - tracks current sound state
 * Values: 0=inactive, 1=loading, 2=playing, 3=fading, 4=special_fade
 * Original: DAT_003567b0 (array indexed by slot_id * 0x2c + offset)
 */
extern char sound_type_array[];

/**
 * Sound fade duration array - stores fade time in frames
 * Original: DAT_003567b2 (array indexed by slot_id * 0x2c + offset)
 */
extern char sound_fade_duration_array[];

/**
 * Sound level array - current volume/envelope level
 * Original: DAT_003567b4 (array indexed by slot_id * 0x2c + offset)
 */
extern char sound_level_array[];

/**
 * Sound parameter 6 array - envelope parameter component 1
 * Original: DAT_003567b6 (array indexed by slot_id * 0x2c + offset)
 */
extern char sound_param6_array[];

/**
 * Sound parameter 7 array - envelope parameter component 2
 * Original: DAT_003567b7 (array indexed by slot_id * 0x2c + offset)
 */
extern char sound_param7_array[];

/**
 * Sound config parameter 1 array
 * Original: DAT_003567b8 (array indexed by slot_id * 0x2c + offset)
 */
extern char sound_config1_array[];

/**
 * Sound config parameter 2 array
 * Original: DAT_003567ba (array indexed by slot_id * 0x2c + offset)
 */
extern char sound_config2_array[];

/**
 * Sound name array - stores sound identifiers/names
 * Original: DAT_003567d8 (array indexed by slot_id * 0xb)
 */
extern char sound_name_array[];

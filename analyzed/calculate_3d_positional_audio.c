#include "orphen_globals.h"
#include <math.h>

// Forward declarations for referenced functions
extern int FUN_0030bd20(float value);                                                               // Float to int conversion/clamping
extern undefined4 FUN_00305408(float y, float x);                                                   // Calculate angle (likely atan2)
extern float FUN_002166e8(int param1, undefined4 angle);                                            // Angle processing
extern float FUN_00305130(float angle);                                                             // Trigonometric function (likely cos)
extern void update_sound_parameters(int sound_id, unsigned long left_vol, unsigned long right_vol); // Sound parameter update (FUN_00206128)
extern void FUN_002057c8(long audio_id, long left_vol, long right_vol);                             // Direct audio function

/**
 * Calculate 3D positional audio with stereo panning and distance attenuation
 *
 * This function implements a sophisticated 3D audio system that:
 * 1. Transforms world coordinates relative to listener position
 * 2. Calculates distance-based volume attenuation (14.0 unit max range)
 * 3. Applies stereo panning based on horizontal angle from listener
 * 4. Uses trigonometric calculations for realistic audio positioning
 * 5. Supports both positive and negative audio IDs for different sound types
 *
 * The audio calculation uses:
 * - Distance attenuation: Volume decreases as distance increases (max 14.0 units)
 * - Stereo panning: Left/right channel balance based on angle
 * - Close-range boost: Special handling for sounds within 3.0 units
 * - Volume scaling: All calculations scaled by input volume percentage
 *
 * Original function: FUN_00267a80
 * Address: 0x00267a80
 *
 * @param world_x X coordinate in world space
 * @param world_y Y coordinate in world space
 * @param world_z Z coordinate in world space
 * @param audio_id Audio identifier (negative values use different sound function)
 * @param volume_percent Volume percentage (0-100, negative uses default distance)
 */
void calculate_3d_positional_audio(float world_x, float world_y, float world_z, long audio_id, long volume_percent)
{
  int base_volume;
  int close_range_boost;
  int left_volume;
  int right_volume;
  float distance_3d;
  undefined4 angle;
  float stereo_factor;

  // Transform coordinates relative to listener position
  world_x = world_x - listener_position_x;
  world_y = world_y - listener_position_y;

  if (volume_percent < 0)
  {
    // Negative volume - use default settings
    volume_percent = 100;
    distance_3d = default_audio_distance;
  }
  else
  {
    // Calculate 3D distance from listener
    distance_3d = SQRT(world_x * world_x + world_y * world_y +
                       (world_z - listener_position_z) * (world_z - listener_position_z));
  }

  // Only process audio if within audible range (14.0 units)
  if (distance_3d < 14.0)
  {
    // Calculate base volume based on distance attenuation
    base_volume = FUN_0030bd20(((14.0 - distance_3d) * 128.0) / 14.0);
    base_volume = (base_volume * (int)volume_percent) / 100;

    // Calculate horizontal distance for close-range boost
    distance_3d = SQRT(world_x * world_x + world_y * world_y);
    close_range_boost = FUN_0030bd20(((3.0 - distance_3d) * 100.0) / 3.0);

    // Apply close-range boost limits
    if (close_range_boost < 0)
    {
      close_range_boost = 0;
    }
    else if (base_volume < close_range_boost)
    {
      close_range_boost = base_volume;
    }

    // Calculate stereo panning if outside minimum distance threshold
    if (min_stereo_distance < distance_3d)
    {
      // Calculate angle from listener to sound source
      angle = FUN_00305408(world_y, world_x);
      distance_3d = (float)FUN_002166e8(listener_orientation, angle);
      stereo_factor = (float)FUN_00305130(distance_3d + distance_3d); // cos(2 * angle)

      // Calculate stereo offset based on angle
      left_volume = FUN_0030bd20((stereo_factor - 1.0) * 40.0);
      if (0.0 < distance_3d)
      {
        left_volume = -left_volume; // Invert for right side
      }
    }
    else
    {
      left_volume = 0; // No stereo separation for very close sounds
    }

    // Calculate final left and right channel volumes
    right_volume = (left_volume + 0x6e) * base_volume; // Right = (offset + 110) * volume
    base_volume = (0x6e - left_volume) * base_volume;  // Left = (110 - offset) * volume

    // Apply volume scaling and clamping
    left_volume = right_volume + 0x7f;
    if (-1 < right_volume)
    {
      left_volume = right_volume;
    }
    left_volume = left_volume >> 7; // Divide by 128

    right_volume = base_volume + 0x7f;
    if (-1 < base_volume)
    {
      right_volume = base_volume;
    }
    right_volume = right_volume >> 7; // Divide by 128

    // Apply close-range boost as minimum volume
    base_volume = close_range_boost;
    if ((close_range_boost <= left_volume) && (base_volume = left_volume, 0x7f < left_volume))
    {
      base_volume = 0x7f; // Clamp to maximum (127)
    }

    if ((close_range_boost <= right_volume) && (close_range_boost = right_volume, 0x7f < right_volume))
    {
      close_range_boost = 0x7f; // Clamp to maximum (127)
    }

    // Send audio to appropriate sound system if audio is enabled
    if (audio_enabled_flag != '\0')
    {
      if (audio_id < 0)
      {
        // Negative audio ID - use sound parameter update
        update_sound_parameters(-(int)audio_id, base_volume, close_range_boost);
        return;
      }
      // Positive audio ID - use direct audio function
      FUN_002057c8(audio_id, base_volume, close_range_boost);
      return;
    }
  }
  return;
}

// Global variables for 3D audio system:

/**
 * Listener X position in world coordinates
 * Original: DAT_0058c0a8
 */
extern float listener_position_x;

/**
 * Listener Y position in world coordinates
 * Original: DAT_0058c0ac
 */
extern float listener_position_y;

/**
 * Listener Z position in world coordinates
 * Original: DAT_0058c0b0
 */
extern float listener_position_z;

/**
 * Default audio distance for special cases
 * Original: fGpffff8d9c
 */
extern float default_audio_distance;

/**
 * Minimum distance for stereo separation
 * Sounds closer than this play in mono (center)
 * Original: fGpffff8da0
 */
extern float min_stereo_distance;

/**
 * Listener orientation angle for stereo calculations
 * Original: uGpffffb6d4
 */
extern uint listener_orientation;

/**
 * Audio system enabled flag
 * When non-zero, enables audio processing
 * Original: cGpffffb664
 */
extern cchar audio_enabled_flag;

#include "orphen_globals.h"

// Forward declarations for referenced functions
extern void calculate_3d_positional_audio(float x, float y, float z, long audio_id, long volume); // 3D audio calculation (FUN_00267a80)
extern void update_sound_parameters(int sound_id, unsigned long param2, unsigned long param3);    // Sound parameter update (FUN_00206128)
extern void FUN_002057c8(long param1, long param2, long param3);                                  // Unknown audio function

/**
 * Trigger positional audio with spatial calculations
 *
 * This function serves as an audio dispatcher that handles different types of audio playback:
 * 1. If audio_data is provided, extracts 3D coordinates and calls 3D positional audio
 * 2. If no audio_data but audio is enabled, calls direct sound functions
 * 3. Handles both positive and negative audio IDs differently
 *
 * The function checks a global audio enable flag (audio_enabled_flag) before processing.
 * When audio_data is provided, it reads coordinates from offsets 0x20, 0x24, 0x28.
 *
 * Original function: FUN_00267d38
 * Address: 0x00267d38
 *
 * @param audio_id Audio identifier (negative values trigger different sound function)
 * @param audio_data Pointer to audio data structure containing 3D coordinates, or 0 for direct audio
 */
void trigger_positional_audio(long audio_id, long audio_data)
{
  int audio_data_ptr;

  if (audio_data != 0)
  {
    // Audio data provided - extract 3D coordinates and calculate positional audio
    audio_data_ptr = (int)audio_data;
    calculate_3d_positional_audio(*(undefined4 *)(audio_data_ptr + 0x20), // X coordinate
                                  *(undefined4 *)(audio_data_ptr + 0x24), // Y coordinate
                                  *(undefined4 *)(audio_data_ptr + 0x28), // Z coordinate
                                  audio_id,                               // Audio ID
                                  100);                                   // Default volume (100%)
    return;
  }

  // No audio data - check if audio system is enabled
  if (audio_enabled_flag != '\0')
  {
    if (audio_id < 0)
    {
      // Negative audio ID - use sound parameter update with maximum values
      update_sound_parameters(-(int)audio_id, 0x7f, 0x7f);
      return;
    }
    // Positive audio ID - use direct audio function with maximum values
    FUN_002057c8(audio_id, 0x7f, 0x7f);
    return;
  }
  return;
}

// Global variables for audio system:

/**
 * Audio system enabled flag
 * When non-zero, enables audio processing
 * Original: cGpffffb664
 */
extern cchar audio_enabled_flag;

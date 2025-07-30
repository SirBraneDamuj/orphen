#include "orphen_globals.h"

// Forward declarations
extern void trigger_positional_audio(long audio_id, long audio_data);

/**
 * Activate menu with audio feedback
 *
 * This function activates a menu system and plays an audio cue.
 * It calls the audio trigger function with ID 4 and no additional data,
 * which likely plays a menu selection or activation sound effect.
 *
 * Original function: FUN_00237ad8
 * Address: 0x00237ad8
 */
void activate_menu_with_audio(void)
{
  trigger_positional_audio(4, 0);
  return;
}

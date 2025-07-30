#include "orphen_globals.h"

// Forward declarations for referenced functions
extern void trigger_positional_audio(long audio_id, long audio_data); // Trigger positional audio with spatial calculations (FUN_00267d38)

/**
 * Play menu navigation sound effect
 *
 * This function plays a standard menu navigation sound (audio ID 1) without
 * positional audio data. It's a simple wrapper around the audio system
 * specifically for menu sound feedback.
 *
 * Used in menu systems to provide audio feedback when:
 * - Navigating through menu items with D-pad
 * - Selecting menu items with action buttons
 * - Any menu interaction that requires audio confirmation
 *
 * Original function: FUN_002256c0
 * Address: 0x002256c0
 */
void play_menu_sound(void)
{
  // Play menu sound effect (audio ID 1) with no positional data
  trigger_positional_audio(1, 0);
  return;
}

#include "orphen_globals.h"

// Forward declarations for referenced functions
extern void trigger_positional_audio(long audio_id, long audio_data); // Trigger positional audio with spatial calculations (FUN_00267d38)

/**
 * Play menu back/cancel sound effect
 *
 * This function plays a menu back/cancel sound (audio ID 2) without
 * positional audio data. It's a simple wrapper around the audio system
 * specifically for menu back/cancel operations.
 *
 * Used in menu systems to provide audio feedback when:
 * - Backing out of menu selections
 * - Canceling menu operations
 * - Transitioning to previous menu states
 * - General system state transitions
 *
 * This uses audio ID 2, which is different from the standard menu
 * navigation sound (audio ID 1), indicating it serves a different
 * purpose in the audio feedback system.
 *
 * Original function: FUN_002256b0
 * Address: 0x002256b0
 */
void play_menu_back_sound(void)
{
  // Play menu back/cancel sound effect (audio ID 2) with no positional data
  trigger_positional_audio(2, 0);
  return;
}

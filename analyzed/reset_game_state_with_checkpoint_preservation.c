/*
 * Game State Reset with Mode Preservation
 *
 * This function performs a comprehensive reset of the game state while preserving
 * the current game mode. It's called during save/load operations and major state
 * transitions where the game needs to reset systems but remember what mode to
 * return to (field exploration, dialog, etc.).
 *
 * Key operations:
 * 1. Preserves game mode state byte (0x0C=field, 0x00=dialog, etc.)
 * 2. Saves critical flags (0x50c-0x50f) before clearing flag system
 * 3. Clears entire game flags array (18,424 flags)
 * 4. Restores only the critical preserved flags
 * 5. Reinitializes menu and system state structures
 * 6. Restores the game mode state byte
 *
 * Original function: FUN_002294d0
 * Original address: 0x002294d0
 */

#include "orphen_globals.h"

// External function declarations for functions not yet analyzed
extern long get_flag_state(int flag_id);                            // FUN_00266368 - already analyzed
extern void memset_zero(void *ptr, int size);                       // FUN_00267e78 - already analyzed
extern void set_flag_state(uint flag_id);                           // FUN_002663a0 - just analyzed
extern long FUN_00229888(int param_1);                              // Menu/system initialization helper
extern void FUN_0025bae8(int param_1, long param_2, void *param_3); // Menu structure setup
extern void FUN_00251dc0(int param_1);                              // System state initialization
extern void FUN_002294b8(void);                                     // Related system function

// External global variables not yet in orphen_globals.h
extern void *DAT_00343688; // Menu/system structure array base
extern char DAT_00343692;  // System state flag
extern char DAT_003555c7;  // Conditional initialization trigger
extern char DAT_003437f4;  // System state flag 1
extern char DAT_003437f5;  // System state flag 2
extern char DAT_003437f6;  // System state flag 3
extern int DAT_00355638;   // System state counter

void reset_game_state_with_mode_preservation(void)
{
  unsigned char saved_game_mode;
  long flag_value;
  long menu_init_data;
  uint critical_flags_mask;
  void *menu_structure_ptr;
  int loop_counter;

  // Save the current game mode state (0x0C=field, 0x00=dialog, etc.)
  // This byte tracks the current interaction mode and must be preserved
  saved_game_mode = g_game_mode_state;

  // Build bitmask of critical SFLG system flags (0x50c-0x50f) that must survive the reset
  // These are Story/System flags (1292-1295) that control essential system state
  critical_flags_mask = 0;
  loop_counter = 0;
  do
  {
    flag_value = get_flag_state(loop_counter + 0x50c);
    loop_counter = loop_counter + 1;
    critical_flags_mask = (critical_flags_mask & 0x7f) << 1 | (uint)(flag_value != 0);
  } while (loop_counter < 4);

  // Clear the entire game flags array (18,424 flags = 2,304 bytes)
  // This is a complete reset of all game state flags
  memset_zero(game_flags_array, 0x900);

  // Restore the critical SFLG system flags that were preserved
  loop_counter = 3;
  do
  {
    if ((critical_flags_mask & 1) != 0)
    {
      set_flag_state(loop_counter + 0x50c);
    }
    loop_counter = loop_counter - 1;
    critical_flags_mask = critical_flags_mask >> 1;
  } while (-1 < loop_counter);

  // Initialize menu/system structures (7 entries, each 0x28 bytes)
  menu_structure_ptr = &DAT_00343688;
  loop_counter = 0;

  // Restore the game mode state before proceeding with initialization
  g_game_mode_state = saved_game_mode;

  do
  {
    menu_init_data = FUN_00229888(loop_counter);
    loop_counter = loop_counter + 1;
    FUN_0025bae8(1, menu_init_data, menu_structure_ptr);

    // Initialize menu structure fields
    *(unsigned short *)((char *)menu_structure_ptr + 10) = 0x100;
    *(short *)((char *)menu_structure_ptr + 2) =
        (short)(char)*((char *)menu_structure_ptr + 6);

    menu_structure_ptr = (char *)menu_structure_ptr + 0x28;
  } while (loop_counter < 7);

  // Set system initialization flags
  set_flag_state(0x501);
  FUN_00251dc0(0x58beb0);
  DAT_00343692 = 0;

  // Clear additional system memory areas
  memset_zero((void *)0x343838, 0x40); // 64 bytes
  memset_zero((void *)0x3437b8, 0x80); // 128 bytes

  // Conditional system state initialization
  if (DAT_003555c7 != '\0')
  {
    DAT_003437f4 = 1;
    DAT_003437f6 = 1;
    DAT_003437f5 = 1;
  }

  // Perform additional system initialization
  FUN_002294b8();
  DAT_00355638 = 0;

  // Set final system flags
  set_flag_state(0x513);
  set_flag_state(0x7bc);
}

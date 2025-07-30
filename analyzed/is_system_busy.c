#include "orphen_globals.h"

/**
 * Check if the system is currently busy/blocked
 *
 * This function checks a global system busy state flag. When this flag is set,
 * it indicates that some critical system operation is in progress and the
 * main game system manager should not proceed with normal processing.
 *
 * Used by the game system manager as one of several early-exit conditions
 * to prevent conflicts during system-level operations.
 *
 * Original function: FUN_00237c60
 * Address: 0x00237c60
 *
 * @return true if system is busy/blocked, false if system is available
 */
bool is_system_busy(void)
{
  return system_busy_state != 0;
}

// Global variable for this function:

/**
 * System busy state flag
 * When non-zero, indicates system is busy with critical operations
 * Original: iGpffffaec0
 */
extern int system_busy_state;

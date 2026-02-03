// Opcode 0xE0 — Destroy Battle Logo Entity (analyzed)
// Original: FUN_00264fc0
// Address: 0x00264fc0
//
// Summary:
// - Destroys/removes the "SORCEROUS STABBER ORPHEN" battle logo entity at 0x58C7E8.
// - Counterpart to opcode 0xDF which creates the logo.
// - US version scripts skip both 0xDF and 0xE0 to disable logo system entirely.
// - Always returns 0.
//
// Behavior:
// 1. Check if logo entity is initialized (DAT_0058c7e8 > 0)
// 2. If initialized:
//    a. Call FUN_00265ec0(0x58c7e8) to destroy the entity
//    b. Set DAT_0058c7e8 = -2 (marks as destroyed/disabled)
// 3. Return 0
//
// FUN_00265ec0 (entity destroyer):
// - Generic entity cleanup function used throughout codebase
// - Clears entity from global lookup table (DAT_005a96b0)
// - Calls FUN_00266098, FUN_00265f70, FUN_0020e7e0 for cleanup
// - If flag bit 0x8000 set, triggers script callback
// - Resets entity ID to 0 (fully cleared)
//
// Related Functions:
// - Opcode 0xDF (FUN_00264fa0): Creates/initializes the logo
// - FUN_00224798: Conditional logo removal when type=0x49 and flag bit 0x01 set
// - FUN_00271858: Checks if logo needs initialization (ID < 1)
//
// Context:
// - Found in opcode dispatch table at index 0xE0
// - Logo entity at 0x58C7E8 is separate from main entity pool
// - ID values: 0 = uninitialized, 0x49 = active logo, -2 = destroyed
// - US localization removed logo by skipping opcodes in battle setup scripts
//
// Notes:
// - Setting ID to -2 (not 0) prevents re-initialization checks from triggering
// - Multiple code paths can destroy logo (opcodes, flag checks, cleanup routines)
// - Logo system appears to be JP-specific battle presentation feature
//
// Original signature: undefined8 FUN_00264fc0(void)

#include <stdint.h>

// Logo entity ID at fixed address
extern int16_t DAT_0058c7e8;

// Generic entity destroyer (cleanup and reset)
extern void FUN_00265ec0(uint64_t entity_addr);

// Original signature: undefined8 FUN_00264fc0(void)
uint64_t opcode_0xe0_destroy_battle_logo(void)
{
  // Check if logo entity is active (ID > 0 means initialized)
  if (DAT_0058c7e8 > 0)
  {
    // Destroy the logo entity and clear resources
    FUN_00265ec0(0x58c7e8);

    // Mark as destroyed (-2 prevents re-initialization)
    DAT_0058c7e8 = -2;
  }

  return 0;
}

// Original signature wrapper
uint64_t FUN_00264fc0(void)
{
  return opcode_0xe0_destroy_battle_logo();
}

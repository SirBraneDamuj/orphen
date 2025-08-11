// Analysis of FUN_00261068 -> set_game_state_and_copy_block (provisional)
// Original: undefined8 FUN_00261068(void)
// Purpose: Sets global state flag bits in DAT_003551ec then copies a 0x0C-byte block from 0x31e668 to 0x325340.
// Observations:
//  - DAT_003551ec is a widely used global status/bitfield (various flag constants: 0x2, 0x4, 0x20000, etc.). This handler assigns 0x40001.
//    That both sets bit 0 (maybe "active" / main loop) and bit 0x40000 (high-order mode flag) clearing other bits.
//  - Immediately invokes FUN_00267da0(dest=0x325340, src=0x31e668, size=0x0c). This function is an optimized memcpy variant handling alignment.
//  - Likely scenario: entering a specific subsystem or resetting a small fixed configuration struct (12 bytes) tied to this state transition.
//  - Returns 0 (as per convention of many opcode handlers returning status ignored by caller).
// Future refinement:
//  - Once addresses 0x325340 / 0x31e668 are typed (via globals.json) rename to meaningful e.g. current_mode_config / default_mode_config.
//  - After more handlers set DAT_003551ec to patterns like 0x20001, 0x2001, 0x80007, we can enumerate flag bits and replace raw literal with named constants.
//
// Retains original FUN_ name in comment for traceability.

#include <stdint.h>

// Externals (raw names retained until fully mapped):
extern uint32_t DAT_003551ec;                                  // Global engine state / flags
extern void FUN_00267da0(void *dst, void *src, uint32_t size); // memcpy-like

uint64_t set_game_state_and_copy_block(void)
{                         // FUN_00261068 analyzed
  DAT_003551ec = 0x40001; // TODO: replace with (STATE_ACTIVE | STATE_MODE_X) once flags known
  FUN_00267da0((void *)0x325340, (void *)0x31e668, 0x0c);
  return 0;
}

// Opcode 0x34 — probe_system_busy
// Original handler: FUN_0025d728 (src/FUN_0025d728.c)
//
// Summary
//   Thin wrapper that invokes the global busy-state check (is_system_busy). The
//   original function calls FUN_00237c60() and ignores its return value. We expose
//   the analyzed name here for clarity and documentation.
//
// Notes
//   - No VM arguments are consumed.
//   - Side effects: none (pure read of a global flag).
//   - Likely used as a scheduling/no-op probe to keep the interpreter in sync with
//     frame pacing or to tick a state machine that cares about stall windows.
//
// Original signature
//   void FUN_0025d728(void);

#include <stdbool.h>

// analyzed helper for FUN_00237c60
extern bool is_system_busy(void);

void opcode_0x34_probe_system_busy(void)
{
  // Match original behavior: call and discard result
  (void)is_system_busy();
}

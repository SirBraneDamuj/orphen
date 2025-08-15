// Opcode 0x33 — advance_ip_and_sync_frame
// Original: FUN_0025d6f8 (src/FUN_0025d6f8.c)
//
// Summary
//   Advances the interpreter pointer by 4 bytes (skipping an immediate/operand block) and
//   calls FUN_0025c220() which, per analyzed notes, adjusts a global and likely synchronizes
//   with a frame or code-relative pointer.
//
// Behavior
//   FUN_00237b38(DAT_00355cd0 + 4);
//   FUN_0025c220();
//   return 0;
//
// See also
//   analyzed/advance_relative_code_pointer.c for context on FUN_0025c220 semantics.

#include <stdint.h>

extern unsigned char *DAT_00355cd0; // instruction pointer
extern void FUN_00237b38(unsigned char *p);
extern void FUN_0025c220(void);

unsigned long opcode_0x33_advance_ip_and_sync_frame(void)
{
  FUN_00237b38(DAT_00355cd0 + 4);
  FUN_0025c220();
  return 0;
}

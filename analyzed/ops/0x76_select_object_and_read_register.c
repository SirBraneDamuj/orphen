// Opcode 0x76 — select_object_and_read_register
// Original handler: FUN_00260318 (src/FUN_00260318.c)
//
// Summary
//   Reads two VM arguments: (a) a target selector and (b) a register id. It then
//   updates the current object pointer via select_current_object_frame(selector, currentObj)
//   (original: FUN_0025d6c0) and performs
//   a read from the script register bank via FUN_0025c548(regId). The return value of the
//   register read is intentionally ignored here (matches original behavior).
//
// Semantics and context
//   - select_current_object_frame(arg, obj): if arg != 0x100, sets DAT_00355044
//     to (&DAT_0058beb0 + arg*0xEC) (orig: FUN_0025d6c0);
//     otherwise sets DAT_00355044 to obj. This selects which entity/frame subsequent ops target.
//   - read_script_register(regId): returns a value from the current object’s register bank (no writes).
//     Other opcodes (0x77–0x7C) combine this read with an immediate and write back via FUN_0025c8f8.
//   - This opcode likely “touches” or preloads a value; the C decompile discards it, so treat
//     it as a no-op read with possible engine-side cache effects only.
//
// Original signature
//   void FUN_00260318(void);

#include <stdint.h>
#include <stdbool.h>

// Globals
extern void *DAT_00355044; // current object pointer

// VM helpers
extern void bytecode_interpreter(void *out);                                   // analyzed: fetch next immediate/argument (orig: FUN_0025c258)
extern void select_current_object_frame(uint32_t selector, void *fallbackPtr); // analyzed helper (orig: FUN_0025d6c0)
extern unsigned long read_script_register(unsigned long);                      // analyzed helper (orig: FUN_0025c548)

unsigned long opcode_0x76_select_object_and_read_register(void)
{
  // Capture current object (passed back to select_current_object_frame when arg == 0x100 path used)
  int obj = (int)(intptr_t)DAT_00355044;

  // Read two arguments: selector and register id
  uint32_t args[2];
  bytecode_interpreter(args);
  bytecode_interpreter((void *)((uintptr_t)args | 4));

  // Select target frame/object, then perform a register read (result intentionally ignored)
  select_current_object_frame(args[0], (void *)(intptr_t)obj);
  (void)read_script_register(args[1]);

  return 0;
}

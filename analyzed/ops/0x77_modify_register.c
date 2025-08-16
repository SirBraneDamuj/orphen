// Opcode 0x77 — modify_register (RMW family 0x77–0x7C)
// Original handler: FUN_00260360 (src/FUN_00260360.c)
//
// Summary
//   Reads three VM arguments:
//     (a) a target selector, (b) a register id, (c) an immediate value.
//   Then selects the current object via select_current_object_frame(selector, currentObj)
//   and performs a read-modify-write on the addressed register depending on the opcode:
//     - 0x77: write immediate
//     - 0x78: AND existing with immediate
//     - 0x79: OR existing with immediate
//     - 0x7A: XOR existing with immediate
//     - 0x7B: ADD immediate to existing
//     - 0x7C: SUB immediate from existing
//   Returns the result of the write dispatcher.
//
// Details
//   - Dispatcher uses DAT_00355cd8 (current opcode) to choose the arithmetic op.
//   - Register read uses analyzed helper read_script_register (orig: FUN_0025c548).
//   - Register write still uses original FUN_0025c8f8 (write dispatcher), not yet analyzed.
//   - Selector semantics documented in select_current_object_frame (orig: FUN_0025d6c0).
//
// Original signature
//   undefined8 FUN_00260360(void);

#include <stdint.h>
#include <stdbool.h>

// Globals
extern void *DAT_00355044;          // current object pointer
extern unsigned short DAT_00355cd8; // current opcode (set by main interpreter)

// VM helpers and register bank
extern void bytecode_interpreter(void *out);                                   // analyzed (orig: FUN_0025c258)
extern void select_current_object_frame(uint32_t selector, void *fallbackPtr); // analyzed (orig: FUN_0025d6c0)
extern unsigned long read_script_register(unsigned long regId);                // analyzed (orig: FUN_0025c548)
extern unsigned long FUN_0025c8f8(unsigned long regId, unsigned long value);   // write dispatcher (unanalyzed)

unsigned long opcode_0x77_modify_register(void)
{
  // Preserve current object for the 0x100 selector path
  int obj = (int)(intptr_t)DAT_00355044;

  // Read arguments: selector, register id, immediate
  uint32_t args[3];
  bytecode_interpreter(args);
  bytecode_interpreter((void *)((uintptr_t)args | 4));
  bytecode_interpreter((void *)((uintptr_t)args | 8));

  // Select target frame/object
  select_current_object_frame(args[0], (void *)(intptr_t)obj);

  // Decode operation by opcode
  unsigned long regId = args[1];
  unsigned long imm = args[2];
  unsigned long cur;

  switch (DAT_00355cd8)
  {
  case 0x77: // write immediate
    return FUN_0025c8f8(regId, imm);

  case 0x78: // AND
    cur = read_script_register(regId);
    return FUN_0025c8f8(regId, cur & imm);

  case 0x79: // OR
    cur = read_script_register(regId);
    return FUN_0025c8f8(regId, cur | imm);

  case 0x7A: // XOR
    cur = read_script_register(regId);
    return FUN_0025c8f8(regId, cur ^ imm);

  case 0x7B: // ADD
    cur = read_script_register(regId);
    return FUN_0025c8f8(regId, (unsigned long)((long)cur + (long)imm));

  case 0x7C: // SUB
    cur = read_script_register(regId);
    return FUN_0025c8f8(regId, (unsigned long)((long)cur - (long)imm));

  default:
    return 0; // should not occur for this entry
  }
}

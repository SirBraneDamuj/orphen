// Opcode 0xBD — object_method_dispatch (analyzed)
// Original: FUN_00263e80
//
// Summary:
// - Reads four VM expressions: selector, method_code, arg0, arg1.
// - Selects the working object via FUN_0025d6c0(selector, uGpffffb0d4) — index or fallback pointer.
// - Dispatches to an object/system method via FUN_00242a18(uGpffffb0d4, method_code, arg0, arg1).
//
// Method table (from FUN_00242a18):
// - Cases include 1, 2, 3, 0x64..0x6A, 0x6F..0x7D mapping to various FUN_00242xxxx entries.
//   This indicates a compact API surface for per-object/system actions (pose, anim, flags, etc.).
//
// Notes:
// - We keep FUN_* callees as-is until individually analyzed; this wrapper documents call shape and
//   parameters for tracing.
// - The evaluator writes 32-bit values; method_code is used as an 8-bit selector in the callee.

#include <stdint.h>

// VM evaluator (original FUN_0025c258)
extern void bytecode_interpreter(void *result_out);

// Object selection helper (sets current selection based on index or pointer fallback)
extern void FUN_0025d6c0(int selector, void *fallback_ptr);

// Global working object pointer
extern void *uGpffffb0d4;

// Object/system method dispatch
extern void FUN_00242a18(void *obj_ptr, uint8_t method_code, uint32_t arg0, uint32_t arg1);

// Original signature: void FUN_00263e80(void)
void opcode_0xBD_object_method_dispatch(void)
{
  int selector;
  uint32_t method_code, arg0, arg1;

  // Read four expressions
  bytecode_interpreter(&selector);
  bytecode_interpreter(&method_code);
  bytecode_interpreter(&arg0);
  bytecode_interpreter(&arg1);

  // Select object (by index or keep fallback)
  void *fallback = uGpffffb0d4;
  FUN_0025d6c0(selector, fallback);

  // Dispatch method on the selected object/context
  FUN_00242a18(uGpffffb0d4, (uint8_t)method_code, arg0, arg1);
}

// Opcode 0x9E — finish_process_slot
// Original handler: FUN_00261d18 (src/FUN_00261d18.c)
//
// Summary
//   Reads one VM argument (slot index) and clears an entry in the process/slot table at
//   base iGpffffbd84. If the arg is negative, it uses the current slot index uGpffffbd88
//   (when non-negative). Otherwise it requires arg < 0x41 (<= 64), else logs "finish_proc error".
//   Returns 0.
//
// Semantics
//   - Table base: iGpffffbd84; entries are 4-byte pointers/handles.
//   - Current slot cursor: uGpffffbd88 (used when arg < 0).
//   - Valid slot range: 0..0x40 inclusive.
//   - Effect: *(iGpffffbd84 + index*4) = 0.
//
// Related opcodes
//   - 0x9D (FUN_00261cb8): write into slot table with computed pointer.
//   - 0x9F (FUN_00261d88): query slot non-zero state.
//
// Original signature
//   undefined8 FUN_00261d18(void)

#include <stdint.h>
#include <stdbool.h>

extern void bytecode_interpreter(void *out); // analyzed VM fetcher

// Globals (as in decompiled code)
extern uint32_t iGpffffbd84;                 // base address of slot table
extern uint32_t uGpffffbd88;                 // current slot index
extern void FUN_0026bfc0(uint32_t fmt_addr); // debug print: "finish_proc error"

unsigned long opcode_0x9E_finish_process_slot(void)
{
  uint32_t arg;
  bytecode_interpreter(&arg);

  uint32_t index;
  if ((int32_t)arg < 0)
  {
    // use current slot if non-negative
    index = uGpffffbd88;
    if ((int32_t)uGpffffbd88 < 0)
    {
      FUN_0026bfc0(0x34d160); // finish_proc error
      return 0;
    }
  }
  else
  {
    index = arg;
  }

  if (index < 0x41)
  {
    *(uint32_t *)(iGpffffbd84 + index * 4) = 0;
  }
  else
  {
    FUN_0026bfc0(0x34d160); // finish_proc error
  }

  return 0;
}

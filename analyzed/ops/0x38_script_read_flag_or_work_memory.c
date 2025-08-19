// Opcode 0x38 — script_read_flag_or_work_memory (alias wrapper)
// Original handler: FUN_0025d768 (src/FUN_0025d768.c)
// This thin wrapper delegates to the shared analyzed implementation:
//   analyzed/script_read_flag_or_work_memory.c
// Note: The same underlying function is used by opcode 0x36 (branch by DAT_00355cd8).

#include <stdint.h>

// Shared analyzed implementation
extern unsigned int script_read_flag_or_work_memory(void);

// Original signature: uint FUN_0025d768(void)
unsigned int opcode_0x38_script_read_flag_or_work_memory(void)
{
  return script_read_flag_or_work_memory();
}

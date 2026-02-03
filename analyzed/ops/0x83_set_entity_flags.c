// Opcode 0x83 — set_entity_flags
// Original: FUN_00260b60
//
// Summary:
// - Reads entity index and 16-bit flag value from VM
// - Validates entity index against pool capacity
// - Writes flag value to entity[+0x02] (flags field)
//
// Entity structure:
// - Size: 0x74 bytes
// - Pool base: iGpffffb770
// - Pool capacity: iGpffffb76c
// - +0x02: uint16 flags field
//
// Side effects:
// - Overwrites entity flags at offset +0x02
//
// PS2-specific notes:
// - Same entity pool as 0x7D/0x7E/0x81/0x82 (0x74 stride)
// - Direct flag write without read-modify-write
// - Likely controls entity behavior, rendering, or state
//
// Error strings:
// - 0x34d048: Entity index out of bounds

#include <stdint.h>

// VM evaluator (analyzed name for FUN_0025c258)
extern void bytecode_interpreter(void *result_out);

// Error handler
extern void FUN_0026bfc0(uint32_t string_addr);

// Globals
extern int iGpffffb770; // Entity pool base (0x74 stride)
extern int iGpffffb76c; // Entity pool capacity

// Original signature: undefined8 FUN_00260b60(void)
uint64_t opcode_0x83_set_entity_flags(void)
{
  int entityIndex;
  uint16_t flags;

  // Read entity index and flags from VM
  bytecode_interpreter(&entityIndex);
  bytecode_interpreter(&flags);

  // Validate entity index
  if (entityIndex >= iGpffffb76c)
  {
    FUN_0026bfc0(0x34d048);
  }

  // Write flags to entity[+0x02]
  *(uint16_t *)(entityIndex * 0x74 + iGpffffb770 + 0x02) = flags;

  return 0;
}

// Original signature preserved for cross-reference
// undefined8 FUN_00260b60(void)

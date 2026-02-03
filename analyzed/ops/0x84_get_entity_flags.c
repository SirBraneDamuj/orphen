// Opcode 0x84 — get_entity_flags
// Original: FUN_00260bc8
//
// Summary:
// - Reads entity index from VM
// - Validates entity index against pool capacity
// - Returns 16-bit flag value from entity[+0x02]
//
// Entity structure:
// - Size: 0x74 bytes
// - Pool base: iGpffffb770
// - Pool capacity: iGpffffb76c
// - +0x02: uint16 flags field
//
// Side effects:
// - None (read-only query)
//
// PS2-specific notes:
// - Same entity pool as 0x7D/0x7E/0x81/0x82/0x83 (0x74 stride)
// - Counterpart to 0x83 (set_entity_flags)
// - Returns value for VM expression evaluation
//
// Error strings:
// - 0x34d068: Entity index out of bounds

#include <stdint.h>

// VM evaluator (analyzed name for FUN_0025c258)
extern void bytecode_interpreter(void *result_out);

// Error handler
extern void FUN_0026bfc0(uint32_t string_addr);

// Globals
extern int iGpffffb770; // Entity pool base (0x74 stride)
extern int iGpffffb76c; // Entity pool capacity

// Original signature: undefined2 FUN_00260bc8(void)
uint16_t opcode_0x84_get_entity_flags(void)
{
  int entityIndex;

  // Read entity index from VM
  bytecode_interpreter(&entityIndex);

  // Validate entity index
  if (entityIndex >= iGpffffb76c)
  {
    FUN_0026bfc0(0x34d068);
  }

  // Read and return flags from entity[+0x02]
  return *(uint16_t *)(entityIndex * 0x74 + iGpffffb770 + 0x02);
}

// Original signature preserved for cross-reference
// undefined2 FUN_00260bc8(void)

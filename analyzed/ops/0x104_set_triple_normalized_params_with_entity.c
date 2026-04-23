// Opcode 0x104 — set_triple_normalized_params_with_entity
// Original: FUN_002624d8
//
// Summary:
// - Reads 7 parameters from VM
// - Normalizes three coordinate parameters
// - Optionally selects entity based on index
// - Calls graphics function
// - Returns 0
//
// Parameters:
// - param0: First value (passed directly)
// - param1: First coordinate (normalized by DAT_00352c68)
// - param2: Additional value (passed directly)
// - param3: Additional value (passed directly)
// - param4: Second coordinate (normalized)
// - param5: Third coordinate (normalized)
// - param6: Entity index (-1=null, 0-255=select from pool, else null)
//
// Entity selection:
// - If param6 < 0: Use null pointer
// - If param6 < 0x100: Select from pool (DAT_0058beb0 + index * 0xEC)
// - Otherwise: Use null pointer
//
// Side effects:
// - Calls FUN_0021b4b8(norm_x, norm_y, norm_z, param0, param2, param3, entity)
//
// Usage pattern:
// Three normalized coordinates suggest 3D position, direction vector,
// or RGB color components. Combined with entity pointer for
// entity-specific rendering parameters.
//
// Related opcodes:
// - 0x102/0x106/0x108: Quad params with entity (4 normalized values)
// - 0x103: Single normalized param (1 value)
//
// PS2-specific notes:
// - Entity pool at 0x58beb0 (stride 0xEC/236 bytes)
// - Three coordinates likely XYZ or RGB
// - Entity pointer suggests per-object rendering state

#include <stdint.h>

// VM evaluator (analyzed name for FUN_0025c258)
extern void bytecode_interpreter(void *result_out);

// Graphics function
extern void FUN_0021b4b8(float coord1, float coord2, float coord3,
                         uint32_t p0, uint32_t p2, uint32_t p3, void *entity);

// Normalization scale factor
extern float DAT_00352c68;

// Current entity pointer
extern void *DAT_00355044;

// Entity pool base
extern uint8_t DAT_0058beb0;

// Original signature: undefined8 FUN_002624d8(void)
uint64_t opcode_0x104_set_triple_normalized_params_with_entity(void)
{
  uint32_t param0, param2, param3;
  int32_t coord1, coord2, coord3;
  int32_t entity_index;
  void *entity;

  entity = DAT_00355044;

  // Read parameters
  bytecode_interpreter(&param0);
  bytecode_interpreter((uint32_t)&param0 | 4);
  bytecode_interpreter((uint32_t)&param0 | 8);
  bytecode_interpreter((uint32_t)&param0 | 0xC);
  bytecode_interpreter(&coord2);
  bytecode_interpreter(&coord3);
  bytecode_interpreter(&entity_index);

  // Select entity
  if (entity_index < 0)
  {
    entity = NULL;
  }
  else if (entity_index < 0x100)
  {
    entity = &DAT_0058beb0 + entity_index * 0xEC;
  }

  // Normalize coordinates and call function
  FUN_0021b4b8((float)coord1 / DAT_00352c68,
               (float)coord2 / DAT_00352c68,
               (float)coord3 / DAT_00352c68,
               param0, param2, param3, entity);

  return 0;
}

// Original signature preserved for cross-reference
// undefined8 FUN_002624d8(void)

// Opcode 0x102/0x106/0x108 — set_quad_params_normalized_with_entity
// Original: FUN_00262250
//
// Summary:
// - Reads 8 parameters from VM
// - Normalizes four coordinate parameters
// - Optionally selects entity based on index
// - Calls different functions based on opcode ID
// - Returns 0
//
// Parameters:
// - param0: First value (passed directly)
// - param1: First X coordinate (normalized)
// - param2: First Y coordinate (normalized)
// - param3: Additional value (passed directly)
// - param4: Additional value (passed directly)
// - param5: Second X coordinate (normalized)
// - param6: Second Y coordinate (normalized)
// - param7: Entity index (-1=current, 0-255=select from pool, else null)
//
// Entity selection:
// - If param7 < 0: Use current entity (DAT_00355044)
// - If param7 < 0x100: Select from pool (DAT_0058beb0 + index * 0xEC)
// - Otherwise: Use null pointer
//
// Opcode branching:
// - 0x102: Normalizes by DAT_00352c58, calls FUN_0021ac00
// - 0x106: Normalizes by DAT_00352c5c, calls FUN_0021d448
// - 0x108: Normalizes by DAT_00352c60, calls FUN_0021c748
//
// Side effects:
// - Calls graphics function with 4 normalized floats, 3 raw params, and entity pointer
//
// Usage pattern:
// Four normalized coordinates suggest quad/rectangle definition,
// texture mapping, or 2D bounding box specification.
//
// Related opcodes:
// - 0x101/0x105/0x107: Similar pattern with 3 params (simpler version)
//
// PS2-specific notes:
// - Entity pool at 0x58beb0 (stride 0xEC/236 bytes)
// - Likely sets rendering parameters tied to entity
// - Different scales for different rendering contexts

#include <stdint.h>

// VM evaluator (analyzed name for FUN_0025c258)
extern void bytecode_interpreter(void *result_out);

// Graphics functions for each opcode
extern void FUN_0021ac00(float x1, float y1, float x2, float y2,
                         uint32_t p0, uint32_t p3, uint32_t p4, void *entity); // 0x102
extern void FUN_0021d448(float x1, float y1, float x2, float y2,
                         uint32_t p0, uint32_t p3, uint32_t p4, void *entity); // 0x106
extern void FUN_0021c748(float x1, float y1, float x2, float y2,
                         uint32_t p0, uint32_t p3, uint32_t p4, void *entity); // 0x108

// Normalization scale factors
extern float DAT_00352c58; // 0x102
extern float DAT_00352c5c; // 0x106
extern float DAT_00352c60; // 0x108

// Opcode ID (set by interpreter)
extern int16_t DAT_00355cd8;

// Current entity pointer
extern void *DAT_00355044;

// Entity pool base
extern uint8_t DAT_0058beb0;

// Original signature: undefined8 FUN_00262250(void)
uint64_t opcode_0x102_0x106_0x108_set_quad_params_normalized_with_entity(void)
{
  uint32_t param0, param3, param4;
  int32_t x1, y1, x2, y2;
  int32_t entity_index;
  int16_t opcode;
  void *entity;

  opcode = DAT_00355cd8;
  entity = DAT_00355044;

  // Read parameters
  bytecode_interpreter(&param0);
  bytecode_interpreter((uint32_t)&param0 | 4);
  bytecode_interpreter((uint32_t)&param0 | 8);
  bytecode_interpreter((uint32_t)&param0 | 0xC);
  bytecode_interpreter(&param4);
  bytecode_interpreter(&x2);
  bytecode_interpreter(&y2);
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

  // Branch based on opcode ID
  if (opcode == 0x106)
  {
    FUN_0021d448((float)x1 / DAT_00352c5c, (float)y1 / DAT_00352c5c,
                 (float)x2 / DAT_00352c5c, (float)y2 / DAT_00352c5c,
                 param0, param3, param4, entity);
  }
  else if (opcode < 0x107)
  {
    if (opcode == 0x102)
    {
      FUN_0021ac00((float)x1 / DAT_00352c58, (float)y1 / DAT_00352c58,
                   (float)x2 / DAT_00352c58, (float)y2 / DAT_00352c58,
                   param0, param3, param4, entity);
    }
  }
  else if (opcode == 0x108)
  {
    FUN_0021c748((float)x1 / DAT_00352c60, (float)y1 / DAT_00352c60,
                 (float)x2 / DAT_00352c60, (float)y2 / DAT_00352c60,
                 param0, param3, param4, entity);
  }

  return 0;
}

// Original signature preserved for cross-reference
// undefined8 FUN_00262250(void)

// Opcodes 0x74/0x75 — calculate_entity_distance_or_angle (cal_targetdist)
// Original: FUN_002601f8
//
// Summary:
// - Reads two entity selector expressions from the VM
// - Selects two entity pointers from the pool (with flexible selection logic)
// - Computes either distance (0x74) or angle (0x75) between the two entities
// - Scales the result and submits it to the parameter system
//
// Selection logic:
// - If first selector < 0x100:
//     - If second selector < 0x100: both are direct pool indices
//     - Else: first is pool index, second uses select_current_object_frame
// - Else:
//     - First uses select_current_object_frame with saved current entity
//     - Second must be < 0x100 (pool index), else error: "param error [cal_targetdist]"
//
// Calculations:
// - Opcode 0x74: FUN_0023a4e8(entity2) → FUN_00216608(e2.Z - e1.Z, e2.X - e1.X) = sqrt((dx)² + (dz)²) = horizontal distance
// - Opcode 0x75: FUN_0023a4b8(entity2, entity1) → FUN_00305408(e2.Z - e1.Z, e2.X - e1.X) = atan2(dz, dx) = horizontal angle
//
// Entity offsets:
// - +0x20: float X position
// - +0x24: float Z position (horizontal plane, not Y vertical)
//
// Side effects:
// - Updates DAT_00355044 (current entity pointer) via select_current_object_frame
// - Submits computed value via FUN_0030bd20 (float→int32 saturating conversion)
//
// PS2-specific notes:
// - Uses 2D XZ horizontal plane for distance/angle calculations (typical for 3D games)
// - The 0xEC entity stride is consistent across all entity pool operations
// - Error string "param error [cal_targetdist]" indicates original function name "cal_targetdist"

#include <stdint.h>
#include <stdbool.h>

// VM evaluator (analyzed name for FUN_0025c258)
extern void bytecode_interpreter(void *result_out);

// Select current entity frame by index or direct pointer
// selector==0x100: use fallbackPtr directly; else: (&DAT_0058beb0) + selector * 0xEC
extern void select_current_object_frame(uint32_t selector, void *fallbackPtr);

// Calculate 2D horizontal distance: sqrt((x2-x1)² + (z2-z1)²)
// Internally calls FUN_00216608 with XZ deltas
extern float FUN_0023a4e8(int entity2, int entity1);

// Calculate 2D horizontal angle: atan2(z2-z1, x2-x1)
// Internally calls FUN_00305408 (atan2f) with XZ deltas
extern float FUN_0023a4b8(int entity2, int entity1);

// Submit scaled parameter (float→int32 conversion and dispatch)
extern unsigned long FUN_0030bd20(float value);

// Error/abort handler with string address
extern void FUN_0026bfc0(uint32_t string_addr);

// Globals
extern int16_t DAT_00355cd8;   // Current opcode (0x74 or 0x75)
extern uint16_t *DAT_00355044; // Current entity pointer
extern uint8_t DAT_0058beb0;   // Entity pool base (byte-typed for pointer arithmetic)
extern float DAT_00352c04;     // Scale factor for result

// Analyzed implementation
unsigned long opcode_0x75_calculate_entity_distance_or_angle(void)
{
  int16_t currentOpcode;
  unsigned long result;
  uint16_t *entity1;
  uint16_t *entity2;
  float calculatedValue;
  int selector1;
  int selector2;

  // Save current opcode and entity pointer
  currentOpcode = DAT_00355cd8;
  entity1 = DAT_00355044;

  // Read two entity selectors from VM
  bytecode_interpreter(&selector1);
  bytecode_interpreter(&selector2);

  // Selection logic: determine which two entities to compare
  if (selector1 < 0x100)
  {
    // First selector is a direct pool index
    if (selector2 < 0x100)
    {
      // Both are direct pool indices
      entity1 = (uint16_t *)((uintptr_t)&DAT_0058beb0 + (uintptr_t)selector2 * 0xECu);
    }
    else
    {
      // First is pool index, second uses flexible selection
      select_current_object_frame(selector2, DAT_00355044);
      entity1 = DAT_00355044;
    }
    entity2 = (uint16_t *)((uintptr_t)&DAT_0058beb0 + (uintptr_t)selector1 * 0xECu);
  }
  else
  {
    // First selector uses flexible selection with saved entity
    select_current_object_frame(selector1, entity1);

    // Second selector must be a direct pool index
    if (selector2 > 0xFF)
    {
      // "param error [cal_targetdist]"
      FUN_0026bfc0(0x34cf28);
    }

    entity1 = (uint16_t *)((uintptr_t)&DAT_0058beb0 + (uintptr_t)selector2 * 0xECu);
    entity2 = DAT_00355044;
  }

  // Compute distance or angle based on opcode
  if (currentOpcode == 0x74)
  {
    // Opcode 0x74: Calculate horizontal distance (2D XZ plane)
    // FUN_0023a4e8 → FUN_00216608(e2.Z - e1.Z, e2.X - e1.X) = sqrt(dx² + dz²)
    calculatedValue = FUN_0023a4e8((int)entity2, (int)entity1);
  }
  else
  {
    // Opcode 0x75 (or other): Calculate horizontal angle (2D XZ plane)
    // FUN_0023a4b8 → FUN_00305408(e2.Z - e1.Z, e2.X - e1.X) = atan2(dz, dx)
    if (currentOpcode != 0x75)
    {
      return 0; // Unknown opcode, early exit
    }
    calculatedValue = FUN_0023a4b8((int)entity2, (int)entity1);
  }

  // Scale result and submit to parameter system
  result = FUN_0030bd20(calculatedValue * DAT_00352c04);
  return result;
}

// Original signature preserved for cross-reference
// undefined8 FUN_002601f8(void)

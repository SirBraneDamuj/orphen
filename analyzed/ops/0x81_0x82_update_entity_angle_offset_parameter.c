// Opcodes 0x81/0x82 — update_entity_angle_offset_parameter
// Original: FUN_00260958
//
// Summary:
// - Reads entity index, parameter slot (0-2), value, and rate from VM
// - Computes cos() of current angle parameter, adds normalized offset value
// - Stores result via FUN_00216690 conversion
// - Updates angle parameter by rate * frame_time (frame-rate independent)
// - Sets status flags to indicate parameter group initialized
//
// Branching behavior:
// - Opcode 0x81: Operates on entity[+0x3C + slot*4] and entity[+0x68 + slot*4], flag 0x02
// - Opcode 0x82: Operates on entity[+0x48 + slot*4] and entity[+0x5C + slot*4], flag 0x01
//
// Pattern:
// - Reads current angle value from [+0x68 or +0x5C]
// - Computes: cos(angle) + (value / scale_factor)
// - Stores packed result via FUN_00216690 to [+0x3C or +0x48]
// - Updates angle: angle += (rate / scale_factor) * frame_delta * 0.03125
// - Frame delta from iGpffffb64c (same as 0x6B interpolation stepper)
//
// Scale factors:
// - 0x81: fGpffff8ca8 (value), fGpffff8cac (rate)
// - 0x82: fGpffff8cb0 (value and rate)
//
// Entity structure:
// - Size: 0x74 bytes (different from main 0xEC pool)
// - Pool base: iGpffffb770
// - Pool capacity: iGpffffb76c
// - +0x3C-0x44: Parameter group A (3 slots)
// - +0x48-0x54: Parameter group B (3 slots)
// - +0x5A: Status flags (bit 0x01 = group B, bit 0x02 = group A)
// - +0x5C-0x64: Angle parameters for group B (3 slots)
// - +0x68-0x70: Angle parameters for group A (3 slots)
//
// Side effects:
// - Writes to entity parameter arrays (packed or float)
// - Updates angle arrays for frame-rate independent animation
// - Sets status flags at entity[+0x5A]
//
// PS2-specific notes:
// - Frame-rate independence via iGpffffb64c * 0.03125 (1/32 scale)
// - Trigonometric parameter update system (cos-based offsets)
// - FUN_00216690 likely packs float to fixed-point or applies transform
// - Different entity pool from main 0xEC stride pool
// - Similar pattern to 0x7D/0x7E but with angle updates
//
// Error strings:
// - 0x34d018: Parameter slot > 2
// - 0x34d030: Entity index out of bounds

#include <stdint.h>
#include <stdbool.h>

// VM evaluator (analyzed name for FUN_0025c258)
extern void bytecode_interpreter(void *result_out);

// Read immediate byte from script stream
extern uint8_t *pbGpffffbd60;

// Trigonometric function (cosine)
extern float FUN_00305218(float angle);

// Value conversion/packing function
extern uint32_t FUN_00216690(float value);

// Error handler
extern void FUN_0026bfc0(uint32_t string_addr);

// Globals
extern int16_t sGpffffbd68; // Current opcode (0x81 or 0x82)
extern int iGpffffb770;     // Entity pool base (0x74 stride)
extern int iGpffffb76c;     // Entity pool capacity
extern int iGpffffb64c;     // Frame delta (timing)
extern float fGpffff8ca8;   // Scale factor for 0x81 value
extern float fGpffff8cac;   // Scale factor for 0x81 rate
extern float fGpffff8cb0;   // Scale factor for 0x82

// Original signature: undefined8 FUN_00260958(void)
uint64_t opcode_0x81_0x82_update_entity_angle_offset_parameter(void)
{
  int16_t currentOpcode;
  uint8_t paramSlot;
  int entityIndex;
  int value;
  int rate;
  int entityBase;
  float angleValue;
  float cosValue;
  uint32_t packed;
  float scaleFactor;
  int slotOffset;
  float *anglePtr;

  // Save current opcode
  currentOpcode = sGpffffbd68;

  // Read entity index
  bytecode_interpreter(&entityIndex);

  // Read parameter slot (0-2)
  paramSlot = *pbGpffffbd60;
  pbGpffffbd60++;

  // Read value and rate
  bytecode_interpreter(&value);
  bytecode_interpreter(&rate);

  // Validate parameter slot
  if (paramSlot > 2)
  {
    FUN_0026bfc0(0x34d018);
  }

  // Validate entity index
  if (entityIndex >= iGpffffb76c)
  {
    FUN_0026bfc0(0x34d030);
  }

  // Calculate entity base address
  entityBase = iGpffffb770 + entityIndex * 0x74;
  slotOffset = paramSlot * 4;

  if (currentOpcode == 0x81)
  {
    // Opcode 0x81: Group A parameters (+0x3C, +0x68)
    scaleFactor = fGpffff8cac;

    // Get current angle from +0x68 array
    angleValue = *(float *)(entityBase + 0x68 + slotOffset);

    // Compute cos(angle) + (value / scale)
    cosValue = FUN_00305218(angleValue);
    packed = FUN_00216690(cosValue + (float)value / fGpffff8ca8);

    // Store packed result to +0x3C array
    *(uint32_t *)(entityBase + 0x3C + slotOffset) = packed;

    // Set status flag (group A = 0x02)
    if (*(uint8_t *)(entityBase + 0x5A) < 2)
    {
      *(uint8_t *)(entityBase + 0x5A) = 2;
    }
    else
    {
      *(uint8_t *)(entityBase + 0x5A) |= 2;
    }

    // Update angle: angle += (rate / scale) * frame_delta * 0.03125
    anglePtr = (float *)(entityBase + 0x68 + slotOffset);
    *anglePtr = *anglePtr + ((float)rate / scaleFactor) * (float)iGpffffb64c * 0.03125f;
  }
  else
  {
    // Opcode 0x82: Group B parameters (+0x48, +0x5C)
    scaleFactor = fGpffff8cb0;

    // Get current angle from +0x5C array
    anglePtr = (float *)(entityBase + 0x5C + slotOffset);
    angleValue = *anglePtr;

    // Compute cos(angle) + (value / scale)
    cosValue = FUN_00305218(angleValue);
    packed = FUN_00216690(cosValue + (float)value / scaleFactor);

    // Store packed result to +0x48 array
    *(uint32_t *)(entityBase + 0x48 + slotOffset) = packed;

    // Update angle: angle += (rate / scale) * frame_delta * 0.03125
    *anglePtr = *anglePtr + ((float)rate / scaleFactor) * (float)iGpffffb64c * 0.03125f;

    // Set status flag (group B = 0x01)
    if (*(uint8_t *)(entityBase + 0x5A) < 1)
    {
      *(uint8_t *)(entityBase + 0x5A) = 1;
    }
    else
    {
      *(uint8_t *)(entityBase + 0x5A) |= 1;
    }
  }

  return 0;
}

// Original signature preserved for cross-reference
// undefined8 FUN_00260958(void)

// Opcode 0x9A — dispatch_indexed_event_with_dual_rgb
// Original: FUN_00261b80
//
// Summary:
// - Reads 8 parameters from VM
// - Validates index (must be < 16)
// - Packs two RGB colors (24-bit each)
// - Dispatches event via FUN_0025d408
// - Returns 0
//
// Parameters:
// - param0: Index (validated < 16, else error)
// - param1: R1 value (0-255, bits 16-23 of color1)
// - param2: G1 value (0-255, bits 8-15 of color1)
// - param3: B1 value (0-255, bits 0-7 of color1)
// - param4: R2 value (0-255, bits 16-23 of color2)
// - param5: G2 value (0-255, bits 8-15 of color2)
// - param6: B2 value (0-255, bits 0-7 of color2)
// - param7: Additional parameter (passed to dispatch)
//
// RGB packing:
// - color1 = (R1<<16) | (G1<<8) | B1
// - color2 = (R2<<16) | (G2<<8) | B2
//
// Side effects:
// - Calls FUN_0026bfc0(0x34d100) if index >= 16 (error handler)
// - Calls FUN_0025d408(index, color1, color2, param7)
//
// Related opcodes:
// - 0x85/0x87: dispatch_rgb_event (single RGB)
// - 0x89: dispatch_tagged_event
// - 0x96: set_global_rgb_color
//
// PS2-specific notes:
// - Dual RGB suggests gradient or dual-tone effect
// - Index-based dispatch (max 16 entries)
// - Error at 0x34d100 likely assertion/debug message

#include <stdint.h>

// VM evaluator (analyzed name for FUN_0025c258)
extern void bytecode_interpreter(void *result_out);

// Error handler
extern void FUN_0026bfc0(uint32_t error_address);

// Event dispatch function
extern void FUN_0025d408(int32_t index, uint32_t color1, uint32_t color2, uint32_t param);

// Original signature: undefined8 FUN_00261b80(void)
uint64_t opcode_0x9a_dispatch_indexed_event_with_dual_rgb(void)
{
  int32_t index;
  int32_t r1, g1, b1;
  int32_t r2, g2, b2;
  uint32_t param7;
  uint32_t color1, color2;

  // Read index
  bytecode_interpreter(&index);

  // Read first RGB
  bytecode_interpreter((uint32_t)&index | 4);
  bytecode_interpreter((uint32_t)&index | 8);
  bytecode_interpreter((uint32_t)&index | 0xC);

  // Read second RGB
  bytecode_interpreter(&r2);
  bytecode_interpreter(&g2);
  bytecode_interpreter(&b2);

  // Read additional parameter
  bytecode_interpreter(&param7);

  // Validate index
  if (index > 15)
  {
    FUN_0026bfc0(0x34d100); // Error: index out of bounds
  }

  // Pack RGB colors
  color1 = (r1 << 16) | (g1 << 8) | b1;
  color2 = (r2 << 16) | (g2 << 8) | b2;

  // Dispatch event
  FUN_0025d408(index, color1, color2, param7);

  return 0;
}

// Original signature preserved for cross-reference
// undefined8 FUN_00261b80(void)

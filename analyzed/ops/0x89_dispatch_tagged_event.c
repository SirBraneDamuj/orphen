// Opcode 0x89 — dispatch_tagged_event
// Original: FUN_00260ce0
//
// Summary:
// - Reads two values from VM
// - Combines them: value1 | (value2 << 24)
// - Dispatches event via FUN_0025d0e0 with combined value and low byte of value2
//
// Pattern:
// - First value: base event/parameter
// - Second value: tag/modifier byte (upper 24 bits clear, then shifted to MSB)
// - Result: 32-bit value with tag in upper byte
//
// Side effects:
// - Dispatches system event via FUN_0025d0e0
//
// PS2-specific notes:
// - Event tagging system for categorized events
// - FUN_0025d0e0 is event dispatcher (related to FUN_0025d1c0)
// - Tag byte may control event routing or priority

#include <stdint.h>

// VM evaluator (analyzed name for FUN_0025c258)
extern void bytecode_interpreter(void *result_out);

// Event dispatcher
extern void FUN_0025d0e0(uint32_t tagged_value, uint8_t tag_byte);

// Original signature: undefined8 FUN_00260ce0(void)
uint64_t opcode_0x89_dispatch_tagged_event(void)
{
  uint32_t value1;
  int value2;

  // Read two values from VM
  bytecode_interpreter(&value1);
  bytecode_interpreter(&value2);

  // Combine: value1 | (value2 << 24)
  uint32_t taggedValue = value1 | ((uint32_t)value2 << 24);

  // Dispatch with combined value and low byte of value2
  FUN_0025d0e0(taggedValue, (uint8_t)value2);

  return 0;
}

// Original signature preserved for cross-reference
// undefined8 FUN_00260ce0(void)

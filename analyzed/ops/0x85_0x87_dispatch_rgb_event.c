// Opcodes 0x85/0x87 — dispatch_rgb_event
// Original: FUN_00260c20
//
// Summary:
// - Reads event ID and RGB bit flags from VM
// - Extracts R, G, B values (0x00 or 0xFF) from flag bits
// - Dispatches event via FUN_0025d1c0 with RGB packed into single parameter
// - Branches on opcode: 0x85 uses buffer 1, 0x87 uses buffer 0
//
// Branching behavior:
// - Opcode 0x85: Passes 1 to FUN_0025d1c0 (buffer selection)
// - Opcode 0x87: Passes 0 to FUN_0025d1c0 (buffer selection)
//
// RGB flag decoding:
// - Bit 0x01: Red   (0=0x00, 1=0xFF)
// - Bit 0x02: Green (0=0x00, 1=0xFF)
// - Bit 0x04: Blue  (0=0x00, 1=0xFF)
// - Packed as: (B << 16) | (G << 8) | R
//
// Side effects:
// - Dispatches system event via FUN_0025d1c0
// - Event handling depends on buffer selection (0x85 vs 0x87)
//
// PS2-specific notes:
// - RGB event system for color-based triggers
// - Likely controls rendering, lighting, or visual effects
// - FUN_0025d1c0 is event dispatcher (see analyzed/dispatch_system_event.c)
// - Buffer selection may control double-buffering or state switching
//
// Note: Opcode 0x87 shares this handler (same function in dispatch table)

#include <stdint.h>
#include <stdbool.h>

// VM evaluator (analyzed name for FUN_0025c258)
extern void bytecode_interpreter(void *result_out);

// System event dispatcher
extern void FUN_0025d1c0(int buffer_select, uint16_t event_id, int rgb_param);

// Current opcode
extern int16_t sGpffffbd68;

// Original signature: undefined8 FUN_00260c20(void)
uint64_t opcode_0x85_0x87_dispatch_rgb_event(void)
{
  int16_t currentOpcode;
  uint16_t eventId;
  uint32_t rgbFlags;
  uint32_t red;
  uint32_t green;
  uint32_t blue;
  bool useBuffer1;

  // Save current opcode
  currentOpcode = sGpffffbd68;

  // Read event ID and RGB flags from VM
  bytecode_interpreter(&eventId);
  bytecode_interpreter(&rgbFlags);

  // Extract RGB values from flags (0x00 or 0xFF for each component)
  red = (rgbFlags & 0x01) ? 0xFF : 0x00;
  green = (rgbFlags & 0x02) ? 0xFF : 0x00;
  blue = (rgbFlags & 0x04) ? 0xFF : 0x00;

  // Pack RGB: (B << 16) | (G << 8) | R
  uint32_t rgbPacked = (blue << 16) | (green << 8) | red;

  // Determine buffer selection based on opcode
  useBuffer1 = (currentOpcode == 0x85);

  // Dispatch event
  FUN_0025d1c0(useBuffer1, eventId, rgbPacked);

  return 0;
}

// Original signature preserved for cross-reference
// undefined8 FUN_00260c20(void)

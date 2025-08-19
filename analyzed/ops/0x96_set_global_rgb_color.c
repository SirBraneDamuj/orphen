// Opcode 0x96 — Set global 24-bit RGB color (0xRRGGBB)
// Original: FUN_002618c0
//
// Summary
// - Evaluates three VM expressions (int32 each) and packs their low bytes into a 24-bit RGB value:
//     uGpffffb6fc = (R << 16) | (G << 8) | B
// - Returns 0.
//
// Inferred semantics
// - This appears to set a default/global UI draw color or palette entry used elsewhere.
//   Evidence: many callsites read/write uGpffffb6fc; initialization uses 0x101010/0x202020; copied into a per-scene block at offset +0x1da68.
// - Values are treated as bytes; upper bits (if any) are ignored when packed.
//
// Notes
// - Keep original FUN_/global names for traceability. Do not rename globals here.
// - VM evaluator writes 4 bytes; we read into 32-bit temporaries and mask to 8-bit when packing.

#include <stdint.h>

// VM expression evaluator (writes a 32-bit result to the provided address)
extern void FUN_0025c258(void *out4);

// Global 24-bit RGB color accumulator (usage: uGpffffb6fc = 0xRRGGBB)
extern uint32_t uGpffffb6fc;

// NOTE: Original signature: undefined8 FUN_002618c0(void)
void opcode_0x96_set_global_rgb_color(void)
{
  uint32_t r32, g32, b32;
  FUN_0025c258(&r32);
  FUN_0025c258(&g32);
  FUN_0025c258(&b32);

  uint32_t R = (uint8_t)r32;
  uint32_t G = (uint8_t)g32;
  uint32_t B = (uint8_t)b32;
  uGpffffb6fc = (R << 16) | (G << 8) | B;
}

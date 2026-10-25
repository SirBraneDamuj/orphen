// Opcode 0xDF — Initialize Battle Logo Entity (analyzed)
// Original: FUN_00264fa0
// Address: 0x00264fa0
//
// Summary:
// - Initializes the "SORCEROUS STABBER ORPHEN" battle logo entity at address 0x58C7E8.
// - This logo appears before battles in the Japanese version.
// - US scripts disable it two ways (tools/scrdis diff of scr_jp vs the US disc, 2026-10-25):
//   13 calls in 11 story/boss battles (s14_e001-e004, e024-e030) are wrapped in
//   `if (0 == 1)`; the other 85 calls, in 42 generic encounter scripts, were deleted
//   along with their reg(5, ...) setup and 90-frame ramp wait.
// - Sets up logo entity with type 0x49 and scale 12.0.
//
// Behavior:
// 1. Calls FUN_0025d5b8 to set up logo entity at 0x58C7E8
// 2. Returns 0 (standard opcode completion)
//
// Related Opcodes:
// - Opcode 0xE0 (FUN_00264fc0): Destroys/removes the logo via FUN_00265ec0
// - Entity type 0x49 is specifically for the battle logo
// - Type 0x48 also seen (alternate logo/splash screen?)
//
// Notes:
// - 0x58C7E8 is entity pool slot 5 (0x58BEB0 + 5 * 0x1D8), the same slot the
//   title screen uses for type 0x48. FUN_0025d5b8 also selects it (DAT_00355044),
//   sets +0x08 |= 0x40, and scales it by 12.0 via FUN_00229ef0. Scripts then address
//   it as entity 5 or `current`.

#include <stdint.h>

// Battle logo entity setup function (initializes logo at 0x58C7E8)
extern void *FUN_0025d5b8(void);

// Original signature: undefined8 FUN_00264fa0(void)
uint64_t opcode_0xdf_initialize_battle_logo(void)
{
  FUN_0025d5b8();
  return 0;
}

// Original signature wrapper
uint64_t FUN_00264fa0(void)
{
  return opcode_0xdf_initialize_battle_logo();
}

// Opcode 0xDF — Initialize Camera Entity (analyzed)
// Original: FUN_00264fa0
// Address: 0x00264fa0
//
// Summary:
// - Wrapper opcode that initializes a special camera entity at address 0x58C7E8.
// - Sets up camera entity with type 0x49 and applies initial scale of 12.0.
// - This is likely called once during scene initialization to create the active camera.
//
// Behavior:
// 1. Calls FUN_0025d5b8 to set up camera entity at 0x58C7E8
// 2. Returns 0 (standard opcode completion)
//
// Related Opcodes:
// - This appears to be part of a camera management system
// - Entity type 0x49 is specifically a camera entity
//
// Notes:
// - Camera entity lives at fixed address 0x58C7E8 (not in main entity pool)
// - Initialization only happens if entity ID is < 1 (uninitialized)

#include <stdint.h>

// Camera entity setup function (initializes camera at 0x58C7E8)
extern void *FUN_0025d5b8(void);

// Original signature: undefined8 FUN_00264fa0(void)
uint64_t opcode_0xdf_initialize_camera_entity(void)
{
  FUN_0025d5b8();
  return 0;
}

// Original signature wrapper
uint64_t FUN_00264fa0(void)
{
  return opcode_0xdf_initialize_camera_entity();
}

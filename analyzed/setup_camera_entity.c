// Camera Entity Setup Function (analyzed)
// Original: FUN_0025d5b8
// Address: 0x0025d5b8

// HUMAN NOTE: most of this file is AI generated but I just want to clarify, this appears
// to actually be the setup function for the SORCEROUS STABBER ORPHEN logo that shows up
// before battles in the JP version.
//
// Summary:
// - Initializes the camera entity at fixed address 0x58C7E8 in memory.
// - Creates a camera entity (type 0x49) if not already initialized.
// - Sets camera scale to 12.0 and enables specific camera flags.
// - Camera entity is NOT part of the main entity pool (DAT_0058beb0).
//
// Camera Entity Memory:
// - Base Address: 0x58C7E8 (fixed, dedicated camera slot)
// - Entity Type: 0x49 (camera entity type)
// - Initial Scale: 12.0 (0x41400000 in IEEE-754)
//
// Initialization Process:
// 1. Set DAT_00355044 (current object pointer) to camera entity address
// 2. Check if camera entity ID is initialized (< 1 means uninitialized)
// 3. If uninitialized:
//    a. Call FUN_00229c40(0x58c7e8, 0x49) - initializes entity structure with type 0x49
//    b. Set flag bit 0x40 in entity flags at offset +0x08
//    c. Call FUN_00229ef0(12.0f, entity) - sets up camera scale and size parameters
// 4. Return pointer to camera entity
//
// Entity Structure (camera at 0x58C7E8):
// - +0x00: Entity ID/type (initialized to 0x49 = camera)
// - +0x08: Flags (bit 0x40 set = camera-specific flag)
// - +0x14C: Scale parameter (set to 12.0)
// - +0x150: Additional scale (set to 12.0)
// - Other offsets populated by FUN_00229c40 and FUN_00229ef0
//
// Global Side Effects:
// - Sets DAT_00355044 to point to camera entity (0x58C7E8)
// - Marks camera as initialized (entity ID becomes 0x49)
// - Enables camera rendering/update flag (bit 0x40)
//
// FUN_00229c40 (entity initializer):
// - Initializes entity structure with specified type
// - Loads entity descriptor/configuration data
// - Clears entity memory (0x1D8 bytes)
// - Sets up entity type-specific parameters
//
// FUN_00229ef0 (scale setter):
// - Sets entity scale at multiple offsets (+0x14C, +0x150, +0x11C, +0x54, etc.)
// - Applies scale multipliers from descriptor data
// - Calculates derived scale values (e.g., scale * 1.5)
// - Updates both current and target scale values
//
// Notes:
// - Camera entity is a singleton - only one exists at fixed address
// - Type 0x49 appears to be specifically for camera entities
// - Scale of 12.0 is the default camera distance/zoom
// - Bit 0x40 in flags likely enables camera updates in entity processing loop
// - Unlike regular entities, camera is not allocated from entity pool

#include <stdint.h>

// Current selected object pointer (global context)
extern void *DAT_00355044;

// Camera entity (fixed address, type 0x49)
extern int16_t DAT_0058c7e8;

// Entity initializer - sets up entity structure with type
// param_1: entity address, param_2: entity type ID
extern void FUN_00229c40(uint64_t entity_addr, long entity_type);

// Scale setter - applies scale value to entity at multiple offsets
// param_1: scale (float), param_2: entity pointer
extern void FUN_00229ef0(float scale, uint64_t entity_ptr);

// Original signature: undefined2 * FUN_0025d5b8(void)
void *setup_camera_entity(void)
{
  void *camera_entity;

  // Set current object pointer to camera entity
  DAT_00355044 = (void *)0x58C7E8; // Camera entity fixed address
  camera_entity = DAT_00355044;

  // Check if camera needs initialization (ID < 1 = uninitialized)
  if (DAT_0058c7e8 < 1)
  {
    // Initialize camera entity structure with type 0x49
    FUN_00229c40(0x58C7E8, 0x49);

    // Set camera-specific flag (bit 0x40 at offset +0x08)
    uint16_t *camera_flags = (uint16_t *)((char *)DAT_00355044 + 0x08);
    *camera_flags = *camera_flags | 0x40;

    // Set camera scale to 12.0 (default camera distance/zoom)
    FUN_00229ef0(12.0f, (uint64_t)DAT_00355044);
  }

  return DAT_00355044;
}

// Original signature wrapper
void *FUN_0025d5b8(void)
{
  return setup_camera_entity();
}

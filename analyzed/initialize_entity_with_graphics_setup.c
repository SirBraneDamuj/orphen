/*
 * Entity Initialization with Graphics Setup - FUN_002740c0
 *
 * Initializes an entity or system component with graphics resources and state configuration.
 * This function sets up default parameters, configures graphics/GPU resources, and establishes
 * the entity's operational state based on conditional logic.
 *
 * The function handles two initialization scenarios:
 * - Fresh initialization: Full resource allocation and setup when offset +0xd4 is zero
 * - Re-initialization: Minimal state setup when offset +0xd4 is non-zero
 *
 * Key responsibilities:
 * 1. Set default float parameters (1.0f values)
 * 2. Initialize data structures via buffer operations
 * 3. Configure graphics/GPU resources with three resource addresses
 * 4. Handle conditional initialization based on entity state
 * 5. Provide error handling for failed resource allocation
 *
 * Original function: FUN_002740c0
 * Address: 0x002740c0
 */

#include "orphen_globals.h"

// Forward declarations for referenced functions
extern void FUN_0025bae8(int param_1, long param_2, void *output_buffer); // Data structure setup (menu/system initialization)
extern void FUN_00216078(long resource_id, int index, int resource_addr); // Graphics/GPU resource loader
extern long FUN_0023f8b8(undefined8 entity);                              // Resource allocation/initialization
extern void FUN_0026bfc0(int error_message_addr);                         // Error reporting function
extern void FUN_002751a8(undefined8 entity);                              // Additional entity setup
extern void FUN_00225bf0(undefined8 entity, uint state, uint substate);   // Entity state setter

/**
 * Initialize Entity with Graphics Setup
 *
 * Sets up an entity with graphics resources, default parameters, and operational state.
 * The function performs conditional initialization based on the entity's current state
 * at offset +0xd4, allowing for both fresh setup and re-initialization scenarios.
 *
 * Graphics Resource Addresses:
 * - 0x573758: Graphics resource/shader parameter 1
 * - 0x57375c: Graphics resource/shader parameter 2
 * - 0x573760: Graphics resource/shader parameter 3
 *
 * @param entity_ptr Pointer to the entity structure to initialize
 */
void initialize_entity_with_graphics_setup(undefined8 entity_ptr)
{
  long resource_result;
  undefined2 *entity;
  undefined1 buffer_data[6]; // Buffer for data structure setup
  char value_1;              // Extracted values from buffer
  char value_2;
  char value_3;
  undefined4 param_1; // Parameters extracted from buffer
  undefined4 param_2;

  entity = (undefined2 *)entity_ptr;

  // Set default float parameters (1.0f = 0x3f800000)
  *(undefined4 *)(entity + 0xa6) = 0x3f800000; // Default scale/multiplier 1
  *(undefined4 *)(entity + 0xa8) = 0x3f800000; // Default scale/multiplier 2

  // Initialize data structure and extract values
  FUN_0025bae8(0, *entity, buffer_data);
  *(undefined4 *)(entity + 0x2a) = param_1; // Store extracted parameter 1
  *(undefined4 *)(entity + 0x2c) = param_2; // Store extracted parameter 2
  entity[0x95] = (short)value_1;            // Store extracted value 1
  entity[0x96] = (short)value_2;            // Store extracted value 2
  entity[0x97] = (short)value_3;            // Store extracted value 3

  // Copy parameter to backup location
  *(undefined4 *)(entity + 0xce) = *(undefined4 *)(entity + 0x2e);

  // Set entity flags
  entity[2] = entity[2] | 8; // Set bit 3 in entity flags

  // Store parameters in alternate locations
  *(undefined4 *)(entity + 0x8e) = param_1;
  *(undefined4 *)(entity + 0x90) = param_2;
  entity[0x94] = (short)value_1;

  // Configure graphics/GPU resources
  FUN_00216078(*entity, 0, 0x573758); // Load graphics resource 1
  FUN_00216078(*entity, 1, 0x57375c); // Load graphics resource 2
  FUN_00216078(*entity, 2, 0x573760); // Load graphics resource 3

  // Conditional initialization based on entity state
  if (*(int *)(entity + 0xd4) == 0)
  {
    // Fresh initialization path
    *(byte *)(entity + 0x4b) = *(byte *)(entity + 0x4b) | 1; // Set initialization flag

    // Allocate primary resource
    resource_result = FUN_0023f8b8(entity_ptr);
    *(int *)(entity + 0xcc) = (int)resource_result;

    if (resource_result == 0)
    {
      // Resource allocation failed - report error
      FUN_0026bfc0(0x34e428); // "Script Area error"
    }

    // Complete entity setup
    FUN_002751a8(entity_ptr);
    *(undefined1 *)(entity + 0xd6) = 1; // Mark as fully initialized
    FUN_00225bf0(entity_ptr, 1, 0);     // Set entity to state 1, substate 0
  }
  else
  {
    // Re-initialization path
    entity[2] = entity[2] | 1;      // Set bit 0 in entity flags
    FUN_00225bf0(entity_ptr, 8, 3); // Set entity to state 8, substate 3
  }

  return;
}

/*
 * ANALYSIS NOTES:
 *
 * Entity Structure Offsets (identified):
 * +0x00: Entity ID (*entity)
 * +0x04: Entity flags (entity[2])
 * +0x2a/+0x2c: Primary parameters
 * +0x2e: Backup parameter source
 * +0x4b: Initialization status flags
 * +0x8e/+0x90: Alternate parameter storage
 * +0x94-0x97: Extracted values storage
 * +0xa6/+0xa8: Scale/multiplier values (set to 1.0f)
 * +0xcc: Primary resource pointer
 * +0xce: Backup parameter storage
 * +0xd4: Initialization state flag
 * +0xd6: Full initialization marker
 *
 * Graphics Resource System:
 * The function configures three graphics resources using consecutive calls to FUN_00216078.
 * These likely represent shader parameters, texture bindings, or GPU state configuration.
 * The addresses (0x573758, 0x57375c, 0x573760) are 4-byte aligned and sequential,
 * suggesting they're part of a graphics resource table or configuration structure.
 *
 * Initialization Modes:
 * - Mode 1 (offset +0xd4 == 0): Fresh initialization with full resource allocation
 * - Mode 2 (offset +0xd4 != 0): Re-initialization with minimal state changes
 *
 * This dual-mode approach allows the same function to handle both initial entity
 * creation and subsequent resets/reinitializations during gameplay.
 */

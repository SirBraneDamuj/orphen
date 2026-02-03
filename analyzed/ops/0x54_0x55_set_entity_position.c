// Opcode 0x54/0x55 — set_entity_position (analyzed)
// Original: FUN_0025eeb0
//
// Summary:
// - Evaluates 4 expressions: entity index (or 0x100 for current), x, y, z coordinates.
// - Normalizes coordinates by fGpffff8c40 (global scale factor).
// - Selects target entity: if index==0x100 uses puGpffffb0d4, else &DAT_0058beb0[index*0xEC].
// - Calls FUN_002662e0 to write coordinates to entity offsets:
//     +0x20: X position
//     +0x24: Y position
//     +0x28: Z position
//     +0x4C: Z position (duplicate)
// - If opcode is 0x55 (not 0x54): additionally calls FUN_00227070(x, y, entity) to calculate
//   terrain height and stores result at entity+0x26 (likely ground/floor reference).
// - Updates puGpffffb0d4 to point to selected entity.
// - Returns 0.
//
// Typical usage:
//   0x54 <index> <x> <y> <z>  # Set entity position (coordinates only)
//   0x55 <index> <x> <y> <z>  # Set entity position + calculate terrain height
//
// Context:
// - Core entity positioning system for cutscenes and gameplay.
// - 0x54: Simple position update (no terrain interaction).
// - 0x55: Position update with terrain height calculation (useful for placing entities on ground).
// - FUN_002662e0 is a simple position setter (4 writes to entity structure).
// - FUN_00227070 is a complex terrain/collision height calculation function (285 lines).
// - Entity pool: base 0x0058beb0, stride 0xEC (236 bytes), max 256 entities.
//
// PS2 notes:
// - fGpffff8c40 is world space coordinate scale (script integers → world floats).
// - Entity offsets +0x20/24/28 are standard 3D position fields (X, Y, Z).
// - Offset +0x4C stores Z duplicate (possibly shadow/projection reference).
// - Offset +0x26 stores terrain height (when opcode 0x55 used).
// - FUN_00227070 uses scratchpad RAM (0x5C bytes allocation) for height calculation.
// - Height calculation likely involves collision mesh traversal or height map lookup.
//
// Keep unresolved externs by their original labels for traceability.

#include <stdint.h>

typedef unsigned int uint;
typedef unsigned long long uint64_t;

// VM entry (analyzed name)
extern void bytecode_interpreter(void *result_out); // orig FUN_0025c258

// Entity position setter (writes x,y,z to entity+0x20/24/28 and z to entity+0x4C)
extern void FUN_002662e0(float x, float y, float z, void *entity);

// Terrain height calculator (complex collision/height map function, returns float height)
extern uint32_t FUN_00227070(float x, float y, void *entity);

// Globals
extern float fGpffff8c40;    // Coordinate normalization scale factor
extern int16_t sGpffffbd68;  // Current opcode ID (0x54 or 0x55)
extern void *puGpffffb0d4;   // Current selected entity pointer (fallback when index==0x100)
extern uint8_t DAT_0058beb0; // Entity pool base address

// Entity structure offsets (referenced for documentation):
// +0x20: float X position
// +0x24: float Y position
// +0x28: float Z position
// +0x26: terrain height (set by opcode 0x55 via FUN_00227070)
// +0x4C: float Z position duplicate

// Original signature: undefined8 FUN_0025eeb0(void)
uint64_t opcode_0x54_0x55_set_entity_position(void)
{
  void *entity_ptr;
  float scale;
  int16_t opcode;
  float *pfVar4;
  int iVar5;
  uint32_t terrain_height;
  float afStack_90[4]; // Normalized coordinate buffer
  int iStack_80;       // Entity index
  int aiStack_7c[3];   // VM evaluator output buffer

  opcode = sGpffffbd68; // Save current opcode ID (0x54 or 0x55)
  pfVar4 = afStack_90;

  // Evaluate entity index expression
  bytecode_interpreter(&iStack_80);

  scale = fGpffff8c40;       // Load coordinate normalization scale
  entity_ptr = puGpffffb0d4; // Default to current entity pointer

  // If index != 0x100, select from entity pool
  if (iStack_80 != 0x100)
  {
    entity_ptr = (void *)(&DAT_0058beb0 + iStack_80 * 0xEC);
  }

  iVar5 = 2; // Countdown: 2, 1, 0 (3 coordinates)

  // Loop: evaluate 3 coordinate expressions and normalize
  do
  {
    iVar5 = iVar5 + -1;
    bytecode_interpreter(aiStack_7c);       // Evaluate next coordinate expression
    *pfVar4 = (float)aiStack_7c[0] / scale; // Normalize and store
    pfVar4 = pfVar4 + 1;                    // Advance to next float slot
  } while (-1 < iVar5);

  // Update current entity pointer
  puGpffffb0d4 = entity_ptr;

  // Set entity position (writes to +0x20, +0x24, +0x28, +0x4C)
  FUN_002662e0(afStack_90[0], afStack_90[1], afStack_90[2], entity_ptr);

  // If opcode is 0x55, calculate and store terrain height
  if (opcode == 0x55)
  {
    terrain_height = FUN_00227070(afStack_90[0], afStack_90[1], puGpffffb0d4);
    *(uint32_t *)((uint8_t *)puGpffffb0d4 + 0x26) = terrain_height;
  }

  return 0;
}

/*
 * Cross-references (globals.json):
 * - FUN_0025eeb0 uses stack/data operations (no direct global reads listed)
 * - Calls FUN_002662e0 (position setter) and FUN_00227070 (height calculator)
 *
 * Usage patterns:
 *
 * Opcode 0x54 - Simple position update (no terrain):
 *   0x54 <entity_idx> <x> <y> <z>  # Set position, skip height calculation
 *   Example: 0x54 00 [x] [y] [z]   # Move player entity to coordinates
 *
 * Opcode 0x55 - Position update with terrain height:
 *   0x55 <entity_idx> <x> <y> <z>  # Set position + calculate ground height
 *   Example: 0x55 05 [x] [y] [z]   # Move NPC entity, snap to terrain
 *
 * Typical scenarios:
 * - Cutscene positioning: Use 0x54 for flying/floating entities, 0x55 for grounded
 * - NPC spawning: 0x55 ensures NPCs spawn on walkable surfaces
 * - Teleportation: 0x54 for precise placement, 0x55 for terrain-relative placement
 * - Camera targets: 0x54 for floating focus points
 *
 * Entity index patterns:
 * - 0x00-0xFF: Direct entity pool index (0-255)
 * - 0x100: Use current selected entity (puGpffffb0d4)
 *
 * Related opcodes:
 * - 0x58: select_pw_slot_by_index (sets puGpffffb0d4 before position update)
 * - 0x5A: select_pw_by_index (find entity by ID, sets puGpffffb0d4)
 * - 0x4F: process_pending_spawn_requests (entity creation)
 * - 0x5C: destroy_entity_by_index (entity removal)
 *
 * FUN_002662e0 (position setter):
 * Simple function that writes x,y,z to entity structure:
 *   entity[+0x20] = x  // X position
 *   entity[+0x24] = y  // Y position (vertical)
 *   entity[+0x28] = z  // Z position
 *   entity[+0x4C] = z  // Z duplicate (shadow/projection reference?)
 *
 * FUN_00227070 (terrain height calculator):
 * Complex 285-line function that:
 * - Uses scratchpad RAM (0x5C byte allocation) for calculations
 * - Likely performs collision mesh traversal or height map lookup
 * - Returns terrain height at given XY coordinates for entity placement
 * - Result stored at entity+0x26 (2 bytes, likely int16 or float16)
 *
 * TODO:
 * - Analyze FUN_00227070 to understand height calculation algorithm
 * - Determine exact format of entity+0x26 (terrain height storage)
 * - Find example scripts showing 0x54 vs 0x55 usage patterns
 * - Document entity+0x4C purpose (why duplicate Z coordinate?)
 * - Investigate coordinate system (Y-up vs Z-up, right/left-handed)
 */

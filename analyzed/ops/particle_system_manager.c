// Opcodes 0x102, 0x106, 0x108 — Particle/Effect System Manager (analyzed)
// Original: FUN_00262250
// Address: 0x00262250
//
// Summary:
// - Shared handler for three related opcodes that manage particle/effect systems.
// - Reads 8 script parameters: 4 position/size values, 4 configuration values.
// - Normalizes position/size parameters by different divisors based on opcode.
// - Dispatches to one of three particle system managers based on opcode ID.
// - Supports optional entity binding for position tracking.
//
// Opcodes:
// - 0x102: Particle system type 1 (FUN_0021ac00) - divisor DAT_00352c58
// - 0x106: Particle system type 2 (FUN_0021d448) - divisor DAT_00352c5c
// - 0x108: Particle system type 3 (FUN_0021c748) - divisor DAT_00352c60
//
// Script Parameters (8 total):
// - param0 (int): Position/size X or width (normalized by divisor)
// - param1 (int): Position/size Y or height (normalized by divisor)
// - param2 (int): Position/size Z or depth (normalized by divisor)
// - param3 (int): Fourth dimension/size parameter (normalized by divisor)
// - param4 (int): Particle count or intensity
// - param5: Configuration value 1 (passed as-is)
// - param6: Configuration value 2 (passed as-is)
// - param7 (int): Entity index for position binding (-1 = world space, 0-255 = entity)
//
// Entity Binding (param7):
// - If param7 < 0: No entity binding (puVar2 = NULL, world-space particles)
// - If 0 <= param7 < 0x100: Bind to entity at (DAT_0058beb0 + param7 * 0xEC)
// - If param7 >= 0x100: Use current selected object (DAT_00355044)
//
// Particle System Managers:
// - FUN_0021ac00 (0x102): Manages particle pool with 0x14-byte stride, ~3000 max particles
// - FUN_0021d448 (0x106): Manages particle pool with 0x14-byte stride, ~1000 max particles
// - FUN_0021c748 (0x108): Manages particle pool with 0x28-byte stride, ~1000 max particles
//
// Normalization Divisors (from globals):
// - DAT_00352c58: Divisor for opcode 0x102 (likely ~100.0 for percentage-based)
// - DAT_00352c5c: Divisor for opcode 0x106 (likely ~1000.0 for world units)
// - DAT_00352c60: Divisor for opcode 0x108 (likely ~10.0 for small-scale effects)
//
// Common Particle System Operations (from callees):
// - Allocate/deallocate particle slots from fixed-size pools
// - Store normalized position/size parameters in global state
// - Set "active" flag (uGpffffad38 = 1, DAT_00354cb0 = 1, DAT_00354cb4 = 1)
// - Bind to entity for position tracking if entity index provided
// - Manage particle lifetime counters and recycling
//
// Usage Examples (inferred):
// - Opcode 0x102: Spell effects, magic particles (percentage-based positioning)
// - Opcode 0x106: Environmental effects, weather (world-space coordinates)
// - Opcode 0x108: Combat hit effects, small explosions (local-space effects)
//
// Side Effects:
// - Modifies global particle system state (different globals per opcode)
// - Allocates particle slots from system pools
// - Sets system "dirty" flags to trigger rendering
// - May fail silently if particle pools are full
//
// Notes:
// - All three systems use linked-list allocation with overflow protection
// - Particle pools have hard limits (1000-3000 depending on system)
// - Position normalization suggests script uses integer coordinates
// - Entity binding allows particles to follow moving objects

#include <stdint.h>

// Bytecode interpreter (reads script parameters)
extern void FUN_0025c258(void *out);

// Current selected object pointer (fallback for entity binding)
extern void *DAT_00355044;

// Current opcode ID
extern int16_t DAT_00355cd8;

// Entity pool base
extern uint8_t DAT_0058beb0;

// Normalization divisors for each particle system type
extern float DAT_00352c58; // Divisor for opcode 0x102
extern float DAT_00352c5c; // Divisor for opcode 0x106
extern float DAT_00352c60; // Divisor for opcode 0x108

// Particle system managers (each handles allocation and rendering)
// All take: (x, y, z, w, count, config1, config2, entity_ptr)
extern void FUN_0021ac00(float x, float y, float z, float w,
                         int count, int config1, int config2, void *entity);
extern void FUN_0021d448(float x, float y, float z, float w,
                         int count, int config1, int config2, void *entity);
extern void FUN_0021c748(float x, float y, float z, float w,
                         int count, int config1, int config2, void *entity);

// Original signature: undefined8 FUN_00262250(void)
uint64_t opcode_particle_system_manager(void)
{
  int16_t current_opcode = DAT_00355cd8;
  void *entity_ptr = DAT_00355044; // Default to current selected object

  // Read 8 script parameters
  int param0;       // Position/size X
  int param1;       // Position/size Y
  int param2;       // Position/size Z
  int param3;       // Position/size W (fourth dimension)
  int param4;       // Particle count/intensity
  int config1;      // Configuration value 1
  int config2;      // Configuration value 2
  int entity_index; // Entity binding index

  FUN_0025c258(&param0);
  FUN_0025c258(&param1);
  FUN_0025c258(&param2);
  FUN_0025c258(&param3);
  FUN_0025c258(&param4);
  FUN_0025c258(&config1);
  FUN_0025c258(&config2);
  FUN_0025c258(&entity_index);

  // Resolve entity binding from index
  if (entity_index < 0)
  {
    // No entity binding - world space particles
    entity_ptr = NULL;
  }
  else if (entity_index < 0x100)
  {
    // Bind to specific entity in pool
    entity_ptr = (void *)(&DAT_0058beb0 + entity_index * 0xEC);
  }
  // else: use default DAT_00355044 (current selected object)

  // Dispatch to appropriate particle system based on opcode
  if (current_opcode == 0x106)
  {
    // Particle system type 2 - environmental effects
    FUN_0021d448(
        (float)param0 / DAT_00352c5c,
        (float)param1 / DAT_00352c5c,
        (float)param2 / DAT_00352c5c,
        (float)param3 / DAT_00352c5c,
        param4,
        config1,
        config2,
        entity_ptr);
  }
  else if (current_opcode < 0x107)
  {
    if (current_opcode == 0x102)
    {
      // Particle system type 1 - spell/magic effects
      FUN_0021ac00(
          (float)param0 / DAT_00352c58,
          (float)param1 / DAT_00352c58,
          (float)param2 / DAT_00352c58,
          (float)param3 / DAT_00352c58,
          param4,
          config1,
          config2,
          entity_ptr);
    }
  }
  else if (current_opcode == 0x108)
  {
    // Particle system type 3 - combat/impact effects
    FUN_0021c748(
        (float)param0 / DAT_00352c60,
        (float)param1 / DAT_00352c60,
        (float)param2 / DAT_00352c60,
        (float)param3 / DAT_00352c60,
        param4,
        config1,
        config2,
        entity_ptr);
  }

  return 0;
}

// Original signature wrapper
uint64_t FUN_00262250(void)
{
  return opcode_particle_system_manager();
}

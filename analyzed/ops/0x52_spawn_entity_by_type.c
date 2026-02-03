// analyzed/ops/0x52_spawn_entity_by_type.c
// Original: FUN_0025edc8
// Opcode: 0x52
// Handler: Spawn entity by type from expression

// Behavior:
// - Evaluates one expression to get entity type ID.
// - If type ID == 0x55 (special case), returns false without spawning.
// - Otherwise calls FUN_00265dc0(10, 0xF6) to find free entity slot in pool range.
// - If slot found, initializes it via FUN_00229c40(entity_ptr, type_id).
// - Updates uGpffffb0d4 to point to newly spawned entity.
// - Returns true if spawned successfully, false otherwise.

// Related:
// - FUN_00265dc0: Entity pool allocator (searches DAT_005a96b0 status array for free slot).
// - FUN_00229c40: Entity initializer by type (219-line function loading descriptors/models).
// - Entity pool: DAT_0058beb0, stride 0xEC, status array DAT_005a96b0.
// - Type 0x55 is special-cased (possibly reserved/invalid for direct spawning).

// PS2 Architecture:
// - Entity pool allocation searches status bytes (0=free, -1=allocated).
// - Type-based initialization system loads descriptors and configures entity state.

#include <stdbool.h>
#include <stdint.h>

// External declarations
extern uint32_t iGpffffb0d4; // Current entity pointer (updated on successful spawn)
typedef void (*bytecode_evaluator_t)(void *);
extern bytecode_evaluator_t FUN_0025c258;                    // Bytecode expression evaluator
extern void *FUN_00265dc0(int start_offset, int count);      // Entity pool allocator
extern void FUN_00229c40(void *entity_ptr, int32_t type_id); // Entity initializer by type

// Entity pool constants
extern uint8_t DAT_005a96b0[]; // Entity slot status array (0=free, -1/0xFF=allocated)
extern void *DAT_0058beb0;     // Entity pool base address
#define ENTITY_STRIDE 0xEC     // 236 bytes per entity

bool opcode_0x52_spawn_entity_by_type(void)
{
  bool spawned;
  int32_t type_id;
  void *entity_ptr;

  // Evaluate expression to get entity type ID
  FUN_0025c258(&type_id);

  spawned = false;

  // Special case: type 0x55 returns false without spawning
  if (type_id != 0x55)
  {
    // Find free entity slot in pool range [10, 10+0xF6)
    // FUN_00265dc0 searches DAT_005a96b0 for first 0x00 byte, marks it 0xFF, returns entity ptr
    entity_ptr = FUN_00265dc0(10, 0xF6);

    // Update global entity pointer
    iGpffffb0d4 = (uint32_t)(uintptr_t)entity_ptr;

    // If slot found, initialize entity with type ID
    if (entity_ptr != NULL)
    {
      FUN_00229c40(entity_ptr, type_id);
      spawned = true;
    }
  }

  return spawned;
}

/*
 * Function Call Hierarchy:
 *
 * opcode_0x52_spawn_entity_by_type()
 *   ├─> FUN_0025c258(stack_var)           [Evaluates expression for type ID]
 *   ├─> FUN_00265dc0(10, 0xF6)            [Finds free entity slot in pool]
 *   │     └─> Searches DAT_005a96b0[10..266] for 0x00 byte
 *   │         Returns &DAT_0058beb0[slot_index * 0xEC]
 *   └─> FUN_00229c40(entity_ptr, type_id) [Initializes entity by type]
 *         └─> FUN_00229980(entity, type, ...)  [Loads descriptor]
 *             FUN_00267e78(entity, 0x1D8)      [Clears entity memory]
 *             FUN_0026bfc0(0x34C050, type)     [Type-specific setup]
 *
 * Memory Layout:
 * - DAT_0058beb0: Entity pool base (256 slots * 0xEC bytes = 59904 bytes)
 * - DAT_005a96b0: Parallel status array (256 bytes, 0=free, 0xFF=allocated)
 * - Pool range [10, 266): 246 available slots for dynamic spawning
 * - Slots 0-9 likely reserved for system entities
 *
 * Special Type IDs:
 * - 0x55: Reserved/invalid type (spawn rejected)
 *
 * Usage Patterns:
 * - Called during script execution to dynamically spawn entities.
 * - Type ID typically comes from script data (immediate or computed expression).
 * - Return value indicates spawn success (true) or failure (false/full pool/type 0x55).
 * - Updated uGpffffb0d4 allows subsequent opcodes to reference the new entity.
 *
 * Related Opcodes:
 * - 0x4F: process_pending_spawn_requests (batch spawner with resource loading)
 * - 0x51: set_pw_all_dispatch (spawns multiple entities by mode)
 * - 0x58: select_pw_slot_by_index (selects existing entity without spawning)
 * - 0x5C: destroy_entity_by_index (frees entity slot, sets status to 0)
 */

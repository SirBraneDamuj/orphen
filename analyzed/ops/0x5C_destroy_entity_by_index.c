// Opcode 0x5C — destroy_entity_by_index (analyzed)
// Original: FUN_0025f238
// Address: 0x0025f238
//
// Summary:
// - Evaluates an entity index from the VM stack
// - If index < 0x100, selects entity from pool: &DAT_0058beb0 + (index * 0xEC)
// - Otherwise uses currently selected entity (DAT_00355044)
// - Calls FUN_00265ec0 to destroy/cleanup the selected entity
// - Returns 0
//
// Behavior:
// 1. Evaluate entity index expression via bytecode_interpreter
// 2. If index < 256 (0x100):
//    - Calculate entity pointer: entity_pool_base + (index * 0xEC)
//    - Entity stride 0xEC (236 bytes) is consistent across all entity opcodes
// 3. Otherwise:
//    - Use DAT_00355044 (currently selected entity pointer)
// 4. Call FUN_00265ec0(entity_ptr) to destroy the entity:
//    - Clears entity from activation table (DAT_005a96b0)
//    - Calls cleanup functions (FUN_00266098, FUN_00265f70, FUN_0020e7e0)
//    - If entity has script callback flag (bit 0x8000), triggers it
//    - Resets entity ID to 0 (marks as destroyed)
// 5. Return 0
//
// Entity Destruction Process (FUN_00265ec0):
// - Clears activation flag in DAT_005a96b0 table
// - If entity ID >= 1: performs full cleanup sequence
// - If entity ID < 1: performs minimal cleanup (sets flags to 0)
// - Always resets entity slot ID to 0 when complete
//
// Use Cases:
// - Remove spawned entities when no longer needed
// - Clean up temporary battle entities
// - Destroy effect/particle entities after completion
// - Free entity slots during scene transitions
//
// Related Opcodes:
// - 0x58 (select_pw_slot_by_index): Selects entity without destroying
// - 0x59 (get_pw_slot_index): Returns current entity index
// - 0x5A (select_pw_by_index): Selects by slot ID value
// - 0xE0 (destroy_battle_logo): Destroys fixed logo entity at 0x58C7E8
//
// Entity Pool Structure:
// - Base: DAT_0058beb0 (0x0058beb0)
// - Stride: 0xEC (236 bytes per entity)
// - Maximum index: 255 (0x100 entities total)
// - Each entity slot has activation flag in DAT_005a96b0[index]
//
// PS2-specific Notes:
// - Entity memory pool at fixed address 0x0058beb0
// - 236-byte entity structure consistent across all entity opcodes
// - Activation tracking via separate byte array (1 byte per entity)
// - Cleanup may trigger VU0/VU1 operations via FUN_0020e7e0
//
// Context:
// - Common pattern: scripts spawn entities, use them, then destroy with 0x5C
// - Pairs with entity creation opcodes (0x4F process_pending_spawn_requests)
// - Destruction is immediate - no fade-out or delayed cleanup
//
// Original signature: undefined8 FUN_0025f238(void)

#include <stdint.h>

// Entity pool base address
extern uint8_t DAT_0058beb0;

// Currently selected entity pointer (used when index >= 0x100)
extern void *DAT_00355044;

// VM evaluator (analyzed)
extern void bytecode_interpreter(void *result_out);

// Generic entity destroyer (analyzed usage in 0xE0)
// - Clears entity from activation table
// - Calls cleanup functions
// - Resets entity ID to 0
extern void FUN_00265ec0(void *entity_ptr);

// Original signature: undefined8 FUN_0025f238(void)
uint64_t opcode_0x5c_destroy_entity_by_index(void)
{
  void *target_entity;
  int entity_index;

  // Default to currently selected entity
  target_entity = DAT_00355044;

  // Evaluate entity index from VM stack
  bytecode_interpreter(&entity_index);

  // If valid index, calculate entity pointer from pool
  if (entity_index < 0x100)
  {
    // Calculate: entity_pool_base + (index * 0xEC)
    target_entity = &DAT_0058beb0 + entity_index * 0xEC;
  }

  // Destroy the selected entity
  FUN_00265ec0(target_entity);

  return 0;
}

// Original signature wrapper
uint64_t FUN_0025f238(void)
{
  return opcode_0x5c_destroy_entity_by_index();
}

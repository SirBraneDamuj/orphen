/*
 * Opcode 0x62 - Find Entity by ID in Secondary Pool
 * Original function: FUN_0025f548
 *
 * Searches through a secondary entity pool to find an active entity with a specific
 * ID byte stored at offset +0x95. Returns the entity's pool index if found, or 0 if
 * not found.
 *
 * This opcode accesses a different entity pool than the main DAT_0058beb0 pool used
 * by most other opcodes. The secondary pool starts at DAT_0058d120 with a parallel
 * status array at DAT_005a96ba.
 *
 * BEHAVIOR:
 * 1. Evaluate one expression: target_id
 * 2. Loop through secondary entity pool (max 245 entities = 0xF5):
 *    - Check status byte at DAT_005a96ba[i] (0 = empty, non-zero = active)
 *    - If active, compare entity[+0x95] with target_id
 *    - If match found, return slot index
 * 3. If no match found, return 0
 *
 * Pool Structure (Secondary):
 * - Base Address: DAT_0058d120 (entity data)
 * - Status Array: DAT_005a96ba (1 byte per slot, 0=empty)
 * - Stride: 0xEC (236 bytes, same as main pool stride 0x1D8/2)
 * - Max Entities: 245 (0xF5)
 * - Starting Index: 0x50 (80 in decimal)
 * - Index Range: 80-324 (0x50-0x144)
 * - Index Calculation: (slot * 8) >> 3 = slot
 *
 * The stride is 0xEC (236 bytes) but accessed as 16-bit words (puVar3 = short*),
 * so the pointer arithmetic is: puVar3 + 0xEC (which is 0xEC shorts = 0x1D8 bytes).
 * However, the actual entity data stride appears to be 0xEC based on the
 * index calculation: iVar2 >> 3 where iVar2 increments by 8 per iteration.
 *
 * PARAMETERS (inline):
 * - target_id (int expression) - The entity ID to search for (stored at entity+0x95)
 *
 * RETURN VALUE:
 * int: Entity pool index (80-324) if found, 0 if not found
 *
 * GLOBAL READS:
 * - DAT_0058d120: Secondary entity pool base (entity data, stride 0xEC shorts = 0x1D8 bytes)
 * - DAT_005a96ba: Secondary entity pool status array (1 byte per slot, parallel to data)
 *   - Offset 0x95 from entity base: Entity ID byte
 *
 * CALL GRAPH:
 * - FUN_0025c258: Expression evaluator
 *
 * ENTITY ID LOCATION:
 * - Offset +0x95 from entity base (entity_ptr + 0x95)
 * - Stored as single byte (char)
 * - Appears to be entity type ID or spawn group ID
 * - Used for entity lookup and management
 *
 * POOL COMPARISON:
 * Main Pool (DAT_0058beb0):
 * - Used by opcodes 0x50-0x5C (spawn, select, modify entities)
 * - Stride: 0xEC bytes per entity
 * - Max: 246 entities
 * - Index range: 0-245
 * - Status: checked via entity flags at offset +0x4C
 *
 * Secondary Pool (DAT_0058d120):
 * - Used by opcode 0x62 (search by ID), 0x5A (select by tag)
 * - Stride: 0xEC (accessed as 0xEC shorts per iteration)
 * - Max: 245 entities
 * - Index range: 80-324 (0x50-0x144)
 * - Status: separate byte array DAT_005a96ba
 * - ID stored at: entity base + 0x95
 *
 * USE CASES:
 * - Find specific NPC by ID for scripted events
 * - Locate enemy by type for AI targeting
 * - Search for quest objects in scene
 * - Validate entity exists before interacting
 * - Cross-reference entities between pools
 *
 * TYPICAL SCRIPT SEQUENCES:
 *
 * Example 1: Find NPC by ID
 *   push npc_id        # Entity ID to find (e.g., 0x37)
 *   0x62              # Search secondary pool
 *   compare 0         # Check if found
 *   jump_if_equal not_found
 *   # Entity found, index on stack
 *
 * Example 2: Select found entity
 *   push target_id
 *   0x62              # Find entity by ID
 *   store slot_index
 *   push slot_index
 *   0x58              # Select entity by index (if in main pool)
 *   # Now DAT_00355044 points to entity
 *
 * Example 3: Count entities of type
 *   push entity_type
 *   0x62
 *   jump_if_zero skip_count
 *   increment counter
 *
 * NOTES:
 * - Returns 0 if entity not found (index 0 is never used due to start at 0x50)
 * - The iVar2 >> 3 calculation converts byte offset to entity index
 * - Initial iVar2 = 0x50 * 8 = 0x280, so index 0x50 = 80 decimal
 * - Each iteration increments iVar2 by 8, then shifts right by 3 (divide by 8)
 * - This results in sequential indices: 80, 81, 82, ... 324
 * - Entity stride puVar3 + 0xEC (shorts) = entity + 0x1D8 bytes (typical entity size)
 * - Status array DAT_005a96ba uses 1 byte per entity (0=free, non-zero=allocated)
 * - Offset +0x95 = 149 bytes into entity structure
 * - acStack_20[0] from FUN_0025c258 reads only first byte of eval result
 */

extern short DAT_0058d120; // Secondary entity pool base (short* access)
extern char DAT_005a96ba;  // Secondary entity pool status array base

extern void FUN_0025c258(char *out_result); // Expression evaluator

int find_entity_by_id_in_secondary_pool(void) // orig: FUN_0025f548
{
  char target_id;
  short *entity_ptr;
  char *status_ptr;
  int byte_offset;
  int entities_remaining;
  int entity_index;

  // Evaluate expression for target entity ID
  char result_buffer[16];
  FUN_0025c258(result_buffer);
  target_id = result_buffer[0]; // Extract first byte

  // Initialize search through secondary entity pool
  entity_ptr = &DAT_0058d120; // Entity data base (short pointer)
  status_ptr = &DAT_005a96ba; // Status array base (byte pointer)

  // Start at index 80 (0x50), counting down 245 entities (0xF5)
  entities_remaining = 0xF5; // 245 entities to check
  byte_offset = 0x50 * 8;    // Initial offset (0x280 = 640 bytes)

  // Search loop: check each active entity for matching ID
  while (true)
  {
    // Check if slot is active (status byte non-zero)
    // AND if entity ID at offset +0x95 matches target
    if ((*status_ptr != '\0') &&
        (*(char *)((int)entity_ptr + 0x95) == target_id))
    {
      // Found matching entity - return its index
      entity_index = byte_offset >> 3; // Convert byte offset to entity index
      return entity_index;
    }

    // Move to next entity
    byte_offset = byte_offset + 8;  // Increment byte offset
    entity_ptr = entity_ptr + 0xEC; // Next entity (0xEC shorts = 0x1D8 bytes)
    entities_remaining = entities_remaining - 1;
    status_ptr = status_ptr + 1; // Next status byte

    // Check if search exhausted
    if (entities_remaining == -1)
    {
      // No matching entity found
      return 0;
    }
  }
}

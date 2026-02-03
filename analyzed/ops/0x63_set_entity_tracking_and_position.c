/*
 * Opcode 0x63 - Set Entity Tracking and Position
 * Original function: FUN_0025f5d8
 *
 * Configures entity tracking relationships and sets position. This opcode appears to
 * link two entities together (tracker and target) and set the tracker's position.
 *
 * The opcode reads 7 expressions to determine:
 * 1. Source entity selector (tracker)
 * 2. Target entity selector (what it tracks)
 * 3. Tracking mode/flags
 * 4. Position coordinates (X, Y, Z)
 *
 * BEHAVIOR:
 * 1. Save current entity pointer (DAT_00355044)
 * 2. Evaluate 7 expressions:
 *    - selector1 (tracker entity)
 *    - selector2 (target entity)
 *    - mode (tracking flags/mode)
 *    - mode_byte (additional mode flags)
 *    - x_pos (X position)
 *    - y_pos (Y position)
 *    - z_pos (Z position)
 * 3. Select entities based on selectors:
 *    - If selector1 == 0x100 && selector2 == 0x100: no-op, return 0
 *    - If selector1 == 0x100: use saved entity, set target from selector2
 *    - If selector2 == 0x100: use saved entity as target, set tracker from selector1
 *    - Else: both are pool indices
 * 4. Write tracking data to tracker entity:
 *    - entity[+0xA0] = mode (tracking flags, short)
 *    - entity[+0x192] = current_slot_index (tracker's own index, short)
 *    - entity[+0x194] = mode_byte (tracking mode byte)
 * 5. Set tracker entity position via FUN_002662e0:
 *    - Normalize x/y/z by DAT_00352bd8
 *    - Update entity position fields
 * 6. Restore saved entity pointer
 * 7. Return 0
 *
 * PARAMETERS (inline):
 * - selector1 (int expression) - Tracker entity selector (0x100 = use current)
 * - selector2 (int expression) - Target entity selector (0x100 = use current)
 * - mode (int expression) - Tracking mode/flags (stored at +0xA0)
 * - mode_byte (byte in int) - Additional tracking mode (stored at +0x194)
 * - x_pos (int expression) - X position
 * - y_pos (int expression) - Y position
 * - z_pos (int expression) - Z position
 *
 * RETURN VALUE:
 * Always returns 0
 *
 * GLOBAL READS/WRITES:
 * - DAT_00355044: Current entity pointer (saved, modified, restored)
 * - DAT_0058beb0: Entity pool base
 * - DAT_00352bd8: Position normalization divisor (~100.0)
 *
 * ENTITY FIELDS MODIFIED:
 * - +0xA0 (offset 0x50 shorts): Tracking mode flags (short)
 * - +0x192 (offset 0xC9 shorts): Tracker's own pool index (short)
 * - +0x194 (offset 0xCA shorts): Tracking mode byte
 * - +0x20/24/28: Position (X, Y, Z) via FUN_002662e0
 *
 * CALL GRAPH:
 * - FUN_0025c258: Expression evaluator (called 7 times)
 * - FUN_0025f120: Get current entity pool index (analyzed as opcode 0x59)
 * - FUN_002662e0: Set entity position with normalized coordinates
 *
 * ENTITY TRACKING SYSTEM:
 * This opcode appears to implement an entity tracking/following system where one entity
 * (the tracker) can follow or orbit another entity (the target). The mode flags control
 * the tracking behavior (follow, orbit, face toward, etc.).
 *
 * Tracking Fields:
 * - +0xA0: Tracking mode flags (controls behavior type)
 * - +0x192: Self-reference (tracker's own pool index)
 * - +0x194: Tracking mode byte (fine-grained control)
 *
 * USE CASES:
 * - Camera following player character
 * - Enemy tracking player for AI
 * - Particle effects attached to entities
 * - Companion NPCs following player
 * - Orbital motion around target
 * - Cutscene entity choreography
 *
 * TYPICAL SCRIPT SEQUENCES:
 *
 * Example 1: Make entity A follow entity B
 *   push entity_a_idx     # Tracker
 *   push entity_b_idx     # Target
 *   push tracking_mode    # e.g., 0x01 = follow
 *   push mode_flags       # Additional flags
 *   push x_offset         # Relative position offset
 *   push y_offset
 *   push z_offset
 *   0x63                  # Set up tracking
 *
 * Example 2: Camera follow player
 *   push camera_idx
 *   push player_idx       # Usually 0
 *   push 0x02            # Camera follow mode
 *   push 0x00            # No special flags
 *   push 0               # Behind player
 *   push 200             # Height above
 *   push -300            # Distance back
 *   0x63
 *
 * Example 3: Particle effect attached to entity
 *   push particle_idx
 *   push parent_idx
 *   push 0x04            # Attach mode
 *   push 0x01            # Inherit rotation
 *   push 0               # Centered on parent
 *   push 0
 *   push 0
 *   0x63
 *
 * SELECTION LOGIC:
 * The dual-selector system with 0x100 sentinel allows flexible entity relationships:
 *
 * Case 1: Both 0x100
 *   - No-op, return immediately
 *   - Both entities remain unchanged
 *
 * Case 2: selector1 == 0x100, selector2 < 0x100
 *   - Tracker: currently selected entity (DAT_00355044)
 *   - Target: entity from pool at selector2 * 0xEC
 *   - Current selection unchanged
 *
 * Case 3: selector1 < 0x100, selector2 == 0x100
 *   - Tracker: entity from pool at selector1 * 0xEC
 *   - Target: currently selected entity (DAT_00355044)
 *   - Current selection set to tracker
 *
 * Case 4: Both < 0x100
 *   - Tracker: entity from pool at selector1 * 0xEC
 *   - Target: entity from pool at selector2 * 0xEC
 *   - Current selection set to tracker
 *
 * NOTES:
 * - Always restores original DAT_00355044 before returning
 * - Position is set on the tracker entity, not the target
 * - The tracker's own pool index is stored at +0x192 for self-reference
 * - Mode stored as short at +0xA0 (2 bytes)
 * - Mode byte stored at +0x194 (1 byte)
 * - Position normalization by DAT_00352bd8 allows integer inputs
 * - The 0x100 sentinel indicates "use current entity" for that selector
 * - FUN_002662e0 likely also sets entity velocity/direction based on position change
 * - Entity stride 0xEC consistent with other entity opcodes
 */

extern unsigned short *DAT_00355044; // Current entity pointer
extern unsigned short DAT_0058beb0;  // Entity pool base
extern float DAT_00352bd8;           // Position normalization divisor

extern void FUN_0025c258(int *out_result);                         // Expression evaluator
extern unsigned short FUN_0025f120(void);                          // Get entity pool index (0x59)
extern void FUN_002662e0(float x, float y, float z, void *entity); // Set entity position

unsigned long set_entity_tracking_and_position(void) // orig: FUN_0025f5d8
{
  unsigned short *saved_entity;
  unsigned short *tracker_entity;
  unsigned short *target_entity;
  int selector1;
  int selector2;
  int mode;
  unsigned char mode_byte;
  int x_pos;
  int y_pos;
  int z_pos;
  float divisor;
  unsigned short tracker_index;

  // Save current entity pointer
  saved_entity = DAT_00355044;

  // Evaluate 7 expressions from bytecode stream
  FUN_0025c258(&selector1);        // Param 1: Tracker entity selector
  FUN_0025c258(&selector2);        // Param 2: Target entity selector
  FUN_0025c258(&mode);             // Param 3: Tracking mode flags
  mode_byte = (unsigned char)mode; // Extract mode byte from int
  FUN_0025c258(&x_pos);            // Param 5: X position
  FUN_0025c258(&y_pos);            // Param 6: Y position
  FUN_0025c258(&z_pos);            // Param 7: Z position

  // Determine tracker and target entities based on selectors
  if (selector1 == 0x100)
  {
    // Tracker is currently selected entity
    if (selector2 == 0x100)
    {
      // Both selectors are 0x100 - no-op
      DAT_00355044 = saved_entity;
      return 0;
    }

    // Target from pool, tracker is current
    tracker_entity = &DAT_0058beb0 + selector2 * 0xEC;
    DAT_00355044 = saved_entity;
  }
  else
  {
    // Tracker from pool
    if (selector2 != 0x100)
    {
      // Target also from pool
      target_entity = &DAT_0058beb0 + selector2 * 0xEC;
    }
    else
    {
      // Target is saved entity
      target_entity = saved_entity;
    }

    // Set current to tracker
    DAT_00355044 = &DAT_0058beb0 + selector1 * 0xEC;
    tracker_entity = target_entity;
  }

  // Write tracking mode to tracker entity
  // Offset +0xA0 = 0x50 shorts
  tracker_entity[0x50] = (unsigned short)mode;

  // Get tracker's own pool index and store as self-reference
  // Offset +0x192 = 0xC9 shorts
  tracker_index = FUN_0025f120(); // Returns current entity index
  tracker_entity[0xC9] = tracker_index;

  // Save position normalization divisor
  divisor = DAT_00352bd8;

  // Store tracking mode byte
  // Offset +0x194 = 0xCA shorts (byte access)
  *(unsigned char *)(tracker_entity + 0xCA) = mode_byte;

  // Set tracker entity position with normalized coordinates
  FUN_002662e0(
      (float)x_pos / divisor,
      (float)y_pos / divisor,
      (float)z_pos / divisor,
      tracker_entity);

  return 0;
}

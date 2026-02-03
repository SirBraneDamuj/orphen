// analyzed/ops/0x3A_0x3B_0x3C_map_coordinate_opcodes.c
// Original: FUN_0025dab8 (0x3A), FUN_0025da78 (0x3B), FUN_0025daf8 (0x3C)
// Opcodes: 0x3A, 0x3B, 0x3C
// Handlers: Read/write map coordinate and scene parameters

// Shared Helper:
// FUN_0025da48 converts binary to BCD (Binary-Coded Decimal):
//   return (value & 0xFF) % 10 | (value & 0xFF) / 10 << 4
// Takes low byte, extracts ones digit (% 10) and tens digit (/ 10),
// packs them as BCD nibbles (e.g., 42 decimal -> 0x42 BCD).

// Behavior 0x3A (FUN_0025dab8):
// - No parameters evaluated.
// - Reads two backup indices: uGpffffae10 (high) and uGpffffae14 (low).
// - Converts each to BCD via FUN_0025da48.
// - Returns combined 16-bit value: high_bcd * 0x100 + low_bcd.
// - Used for reading saved map/section coordinates.

// Behavior 0x3B (FUN_0025da78):
// - No parameters evaluated.
// - Reads two scene parameters: uGpffffb284 (high) and uGpffffb280 (low).
// - Converts each to BCD via FUN_0025da48.
// - Returns combined 16-bit value: high_bcd * 0x100 + low_bcd.
// - Used for reading current map coordinates (format: "MP%02d%02d").

// Behavior 0x3C (FUN_0025daf8):
// - Evaluates one expression.
// - Stores result in uGpffffb298 (related to entity descriptor loading).
// - Returns 0 (standard opcode completion).

// Related:
// - uGpffffb284, uGpffffb280: Current map/scene coordinates (main_game_loop.c)
// - uGpffffae10, uGpffffae14: Backup/saved coordinates (mcb_debug_menu_interface.c)
// - uGpffffb298: Entity/descriptor counter (used in FUN_00229980, FUN_0025ba98)
// - Map format: "MP%02d%02d" seen in debug output (0x34bea8, 0x34be60)

// PS2 Architecture:
// - BCD encoding common for save data and coordinate systems.
// - Allows direct decimal digit access without division.
// - Map coordinates stored as separate bytes (tens, ones) in globals.

#include <stdint.h>

// External declarations
typedef void (*bytecode_evaluator_t)(void *);
extern bytecode_evaluator_t FUN_0025c258;     // Bytecode expression evaluator
extern uint32_t FUN_0025da48(uint32_t value); // Binary to BCD converter

// Map/scene coordinate globals
extern uint32_t uGpffffb284; // Current map coordinate (high byte, e.g., tens)
extern uint32_t uGpffffb280; // Current map coordinate (low byte, e.g., ones)
extern uint32_t uGpffffae10; // Backup map coordinate (high byte)
extern uint32_t uGpffffae14; // Backup map coordinate (low byte)
extern uint32_t uGpffffb298; // Descriptor/entity counter

// Opcode 0x3A: Read backup map coordinates in BCD format
int32_t opcode_0x3a_read_backup_map_coordinates_bcd(void)
{
  int32_t high_bcd;
  int32_t low_bcd;

  // Convert backup coordinates to BCD
  high_bcd = FUN_0025da48(uGpffffae10); // Backup section index (high)
  low_bcd = FUN_0025da48(uGpffffae14);  // Backup section index (low)

  // Combine into 16-bit BCD value (e.g., 0x1234 = map 12, section 34)
  return high_bcd * 0x100 + low_bcd;
}

// Opcode 0x3B: Read current map coordinates in BCD format
int32_t opcode_0x3b_read_current_map_coordinates_bcd(void)
{
  int32_t high_bcd;
  int32_t low_bcd;

  // Convert current coordinates to BCD
  high_bcd = FUN_0025da48(uGpffffb284); // Current scene parameter (high)
  low_bcd = FUN_0025da48(uGpffffb280);  // Current scene parameter (low)

  // Combine into 16-bit BCD value (e.g., 0x0512 = map 05, section 12)
  return high_bcd * 0x100 + low_bcd;
}

// Opcode 0x3C: Set descriptor/entity counter
uint64_t opcode_0x3c_set_descriptor_counter(void)
{
  int32_t value;

  // Evaluate expression to get counter value
  FUN_0025c258(&value);

  // Store in descriptor counter global
  uGpffffb298 = value;

  return 0;
}

/*
 * Function Call Hierarchy:
 *
 * opcode_0x3a_read_backup_map_coordinates_bcd()
 *   ├─> FUN_0025da48(uGpffffae10)        [Convert backup high to BCD]
 *   │     └─> return (val & 0xFF) % 10 | (val & 0xFF) / 10 << 4
 *   └─> FUN_0025da48(uGpffffae14)        [Convert backup low to BCD]
 *
 * opcode_0x3b_read_current_map_coordinates_bcd()
 *   ├─> FUN_0025da48(uGpffffb284)        [Convert current high to BCD]
 *   └─> FUN_0025da48(uGpffffb280)        [Convert current low to BCD]
 *
 * opcode_0x3c_set_descriptor_counter()
 *   └─> FUN_0025c258(&value)             [Eval expr: counter value]
 *
 * BCD Conversion (FUN_0025da48):
 * - Input: Binary value (low byte used)
 * - Process: Extract ones (% 10) and tens (/ 10) digits
 * - Output: BCD nibbles (e.g., 42 -> 0x42, 99 -> 0x99)
 * - Example: value=23 -> return (3 | (2 << 4)) = 0x23
 *
 * Map Coordinate Globals:
 * - uGpffffb284/uGpffffb280: Current map location
 *   - Used in main game loop
 *   - Debug format: "MAP>(MP%02d%02d)\n" and "~MP%02d%02d"
 *   - Example: b284=5, b280=12 -> Map 05, Section 12
 * - uGpffffae10/uGpffffae14: Backup/saved location
 *   - Used in save/load system
 *   - MCB (memory card block) interface references these
 *   - Restored during scene transitions
 *
 * Descriptor Counter (uGpffffb298):
 * - Used in FUN_00229980 (entity type loader)
 * - Used in FUN_0025ba98 (workspace initialization)
 * - Appears to count loaded entity descriptors
 * - Range typically 0-60 (0x3C = 60 max)
 *
 * Usage Patterns:
 * - 0x3A: Query backup coordinates before scene transition
 * - 0x3B: Query current coordinates for save system or conditionals
 * - 0x3C: Initialize descriptor counter before entity loading
 *
 * Typical Script Sequences:
 * ```
 * # Read current map coordinates
 * 0x3B                    # Get current map as BCD (e.g., 0x0512)
 * 0x20 0x0512             # Push target map value
 * 0x2E                    # Compare equal
 * 0x0D [offset]           # Branch if on specific map
 *
 * # Prepare for entity loading
 * 0x3C 0x00               # Reset descriptor counter
 * 0x4F                    # Process pending spawn requests
 * ```
 *
 * BCD Format Rationale:
 * - Facilitates decimal display without conversion
 * - Common in PS2 save data (easier hex editing for debugging)
 * - Direct nibble access for digit extraction
 * - Map IDs naturally decimal (Map 05 Section 12)
 *
 * Related Opcodes:
 * - 0x36/0x38: script_read_flag_or_work_memory (general data reads)
 * - 0x37/0x39: variable_or_flag_alu (ALU operations on variables)
 * - 0x4F: process_pending_spawn_requests (uses descriptor counter)
 *
 * Cross-References:
 * - analyzed/main_game_loop.c: uGpffffb284/b280 usage in map system
 * - analyzed/mcb_debug_menu_interface.c: uGpffffae10/ae14 backup logic
 * - analyzed/entity_memory_allocator.c: Descriptor loading system
 */

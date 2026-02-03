// analyzed/ops/0x4E_push_to_lookup_table.c
// Original: FUN_0025e730
// Opcode: 0x4E
// Handler: Push entry to lookup table (stack-based table storage)

// Behavior:
// - Evaluates 2 expressions (value1, value2).
// - Reads 1 immediate byte from stream (value3).
// - If table not full (DAT_0035504c < 0x10), stores 3-value entry:
//   - Stores at DAT_00571d00 + (count * 0xC) in 12-byte stride.
//   - Entry structure: [value1:u32] [value3:u8] [value2:u32]
//   - Increments counter (DAT_0035504c++).
// - If table full (count >= 16), calls debug error (FUN_0026bfc0 with 0x34ce88).
// - Returns 0 (standard opcode completion).

// Related:
// - DAT_0035504c: Table entry counter (0-16 entries max)
// - DAT_00571d00: Table base address (16 entries * 12 bytes = 192 bytes)
// - Entry offsets: +0x00 (value1), +0x04 (value3), +0x08 (value2)
// - FUN_0025c1d0: Reads immediate byte from stream, advances pointer
// - FUN_0026bfc0: Debug printf function (error/warning output)
// - 0x34ce88: Error message string (table overflow warning)

// Usage Context:
// - This table is used by opcode 0x51 (set_pw_all_dispatch) for ID lookups.
// - Opcode 0x51 searches this table for matching IDs (stride 0xC).
// - Table is cleared by FUN_0025b6d0 (sets DAT_0035504c = 0).
// - Entry structure suggests [id:u32] [type:u8] [data:u32] format.

// PS2 Architecture:
// - Table limit 16 entries (0x10) provides bounds checking.
// - 12-byte stride (0xC) for cache-aligned access.
// - Debug error on overflow (development/debug build).
// - Stack-like push operation (no pop, table cleared in bulk).

#include <stdint.h>

// External declarations
typedef void (*bytecode_evaluator_t)(void *);
extern bytecode_evaluator_t FUN_0025c258;          // Bytecode expression evaluator
extern uint32_t FUN_0025c1d0(void);                // Read immediate byte from stream
extern void FUN_0026bfc0(uint32_t error_msg_addr); // Debug printf (error output)

// Bytecode stream pointer
extern uint8_t *DAT_00355cd0;

// Lookup table globals
extern uint32_t DAT_0035504c; // Entry counter (0-16)
extern uint8_t DAT_00571d00;  // Table base address

// Opcode 0x4E: Push entry to lookup table
uint64_t opcode_0x4e_push_to_lookup_table(void)
{
  uint32_t value1[4];
  uint32_t value3;
  uint32_t value2[4];
  int32_t table_offset;

  // Evaluate first expression (value1)
  FUN_0025c258(value1);

  // Read immediate byte from stream (value3)
  value3 = FUN_0025c1d0();

  // Evaluate second expression (value2)
  FUN_0025c258(value2);

  // Check if table has space
  if (DAT_0035504c < 0x10)
  {
    // Calculate offset in table (12-byte stride)
    table_offset = DAT_0035504c * 0xC;

    // Increment entry counter
    DAT_0035504c = DAT_0035504c + 1;

    // Store entry in table (3 values: u32, u8, u32)
    *((uint32_t *)(&DAT_00571d00 + table_offset + 0x00)) = value1[0]; // +0x00
    *((uint32_t *)(&DAT_00571d00 + table_offset + 0x04)) = value3;    // +0x04
    *((uint32_t *)(&DAT_00571d00 + table_offset + 0x08)) = value2[0]; // +0x08
  }
  else
  {
    // Table full - output debug error message
    FUN_0026bfc0(0x34ce88); // Error message: table overflow
  }

  return 0;
}

/*
 * Function Call Hierarchy:
 *
 * opcode_0x4e_push_to_lookup_table()
 *   ├─> FUN_0025c258(&value1)        [Eval expr: first value]
 *   ├─> FUN_0025c1d0()               [Read immediate byte]
 *   ├─> FUN_0025c258(&value2)        [Eval expr: second value]
 *   └─> if (count < 16)
 *         ├─> Store entry in table   [Write 12 bytes]
 *         └─> count++                [Increment counter]
 *       else
 *         └─> FUN_0026bfc0(0x34ce88) [Debug error: table full]
 *
 * Table Structure:
 * - Base: DAT_00571d00
 * - Stride: 12 bytes (0xC)
 * - Max entries: 16 (0x10)
 * - Total size: 192 bytes (16 * 12)
 *
 * Entry Format (12 bytes):
 * +0x00: value1 (u32) - typically entity/object ID
 * +0x04: value3 (u32) - type/mode byte stored as u32
 * +0x08: value2 (u32) - data/parameter value
 *
 * Table Management:
 * - Push operation: This opcode (0x4E)
 * - Lookup operation: Opcode 0x51 (set_pw_all_dispatch)
 * - Clear operation: FUN_0025b6d0 (sets count = 0)
 * - Counter: DAT_0035504c (tracks entries)
 *
 * Usage by Opcode 0x51:
 * - Scans table for matching ID (value1)
 * - Uses stride 0xC to iterate entries
 * - Reads all 3 values for matched entry
 * - Mode value (value3) determines spawn behavior
 *
 * Typical Script Sequences:
 * ```
 * # Build lookup table
 * 0x4E [id1] [type1] [data1]  # Push entry 1
 * 0x4E [id2] [type2] [data2]  # Push entry 2
 * 0x4E [id3] [type3] [data3]  # Push entry 3
 *
 * # Use table for spawning
 * 0x51                         # Dispatch using table
 * ```
 *
 * Error Handling:
 * - Max 16 entries enforced
 * - Overflow triggers debug printf
 * - Error message at 0x34ce88 (likely "Table full" or similar)
 * - No crash on overflow (debug warning only)
 *
 * Table Clear Operations:
 * - FUN_0025b6d0 resets counter to 0
 * - Called during scene transitions
 * - Old entries remain in memory but count=0 prevents access
 * - No explicit entry deletion (bulk clear only)
 *
 * Memory Layout Example:
 * DAT_00571d00:
 *   [entry 0] +0x00: ID=0x0A, type=0x01, data=0x64
 *   [entry 1] +0x0C: ID=0x15, type=0x02, data=0xC8
 *   [entry 2] +0x18: ID=0x20, type=0x03, data=0x12C
 *   ...
 *   [entry 15] +0xB4: Last entry
 *
 * Performance Notes:
 * - No sorting (insertion order preserved)
 * - Linear search in 0x51 (16 entries max, acceptable)
 * - No duplicate checking (script must avoid)
 * - Stack-like push (no random access modification)
 *
 * Related Opcodes:
 * - 0x51: set_pw_all_dispatch (uses this table for lookups)
 * - 0x4D: process_resource_id_list (different table system)
 * - 0x58: select_pw_slot_by_index (direct pool access)
 *
 * Cross-References:
 * - analyzed/ops/0x51_set_pw_all_dispatch.c: Table lookup implementation
 * - src/FUN_0025b6d0.c: Table clear function
 * - src/FUN_0025c1d0.c: Immediate byte reader
 * - src/FUN_0026bfc0.c: Debug printf implementation
 *
 * Debug String Analysis:
 * - 0x34ce88: Error message address
 * - Likely contains: "Lookup table full" or "Max entries exceeded"
 * - Only output in debug builds (not in retail)
 * - FUN_0026bfc0 is variadic debug printf wrapper
 *
 * Entry Type Values (value3):
 * - Inferred from 0x51 usage:
 *   - 0x00: Default spawn mode
 *   - 0x01: Special mode 1
 *   - 0x02: Special mode 2
 *   - 0x03: Skip/ignore entry
 * - Exact semantics determined by 0x51 handler
 *
 * ID Matching (value1):
 * - Compared against runtime entity IDs
 * - Used for conditional spawning
 * - Matches entity pool slot IDs
 * - Range typically 0-255 (pool indices)
 */

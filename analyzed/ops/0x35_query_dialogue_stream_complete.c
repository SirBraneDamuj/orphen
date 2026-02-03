// analyzed/ops/0x35_query_dialogue_stream_complete.c
// Original: FUN_0025d748
// Opcode: 0x35
// Handler: Query dialogue stream completion status

// Behavior:
// - No parameters evaluated (void opcode).
// - Calls FUN_00237c70 to check if current dialogue stream has completed.
// - Returns completion status (true if dialogue finished, false otherwise).
// - Does not modify any state, pure query operation.

// Related:
// - FUN_00237c70: Dialogue completion checker (returns true when stream finishes).
// - iGpffffbcf4: Dialogue system state (value 8 = active dialogue mode).
// - pcGpffffaec0: Current dialogue stream pointer (set by FUN_00237b38).
// - Completion condition: iGpffffbcf4==8 AND pcGpffffaec0 points to '\0' (null terminator).

// PS2 Architecture:
// - Dialogue system uses state machine with pcGpffffaec0 tracking current position.
// - Stream parsing advances pointer via FUN_00237de8 and FUN_00237fc0.
// - Null byte marks end of dialogue text stream.

#include <stdint.h>
#include <stdbool.h>

// External declarations
extern bool FUN_00237c70(void); // Dialogue stream completion checker
extern int32_t iGpffffbcf4;     // Dialogue system state (8 = active dialogue)
extern char *pcGpffffaec0;      // Current dialogue stream pointer

void opcode_0x35_query_dialogue_stream_complete(void)
{
  // Query dialogue completion status
  // FUN_00237c70 returns true if:
  //   1. Dialogue system is in active mode (iGpffffbcf4 == 8)
  //   2. Stream pointer is valid (pcGpffffaec0 != NULL)
  //   3. Current character is null terminator (*pcGpffffaec0 == '\0')
  FUN_00237c70();

  return;
}

/*
 * Function Call Hierarchy:
 *
 * opcode_0x35_query_dialogue_stream_complete()
 *   └─> FUN_00237c70()                    [Check dialogue completion]
 *         └─> Checks: (iGpffffbcf4 == 8) && (pcGpffffaec0 != NULL) && (*pcGpffffaec0 == '\0')
 *
 * Dialogue System State:
 * - iGpffffbcf4: Dialogue mode/state indicator
 *   - Value 8: Active dialogue processing mode
 *   - Other values: Dialogue system inactive/unavailable
 * - pcGpffffaec0: Stream pointer
 *   - Set by FUN_00237b38 when dialogue starts
 *   - Advanced by FUN_00237de8 (per-frame tick) and FUN_00237fc0 (opcode parser)
 *   - NULL when no dialogue active
 *   - Points to '\0' when stream finished
 *
 * Dialogue Stream Flow:
 * 1. Start: FUN_00237b38(iGpffffb0e8 + offset) sets pcGpffffaec0
 * 2. Advance: FUN_00237de8 parses opcodes using PTR_FUN_0031c640 dispatch table
 * 3. Parse: FUN_00237fc0 processes text and control codes
 * 4. Completion: pcGpffffaec0 reaches '\0' terminator
 * 5. Query: FUN_00237c70 (via opcode 0x35) checks completion
 *
 * Related Functions:
 * - FUN_00237b38: Start dialogue stream (sets pcGpffffaec0)
 * - FUN_00237de8: Per-frame dialogue tick/advance
 * - FUN_00237fc0: Dialogue opcode parser (processes text stream)
 * - FUN_00239848: Page/chunk handler (reads dialogue block headers)
 * - FUN_0025ce30: Scheduler that triggers dialogue via offsets
 *
 * Usage Patterns:
 * - Typical script wait-for-dialogue sequence:
 *   ```
 *   0x[dialogue_start_opcode]  # Start dialogue stream
 *   0x35                        # Query completion status
 *   0x0D [offset]               # Branch if not complete (loop back)
 *   # Continue after dialogue finishes
 *   ```
 * - Used in cutscenes and NPC conversations to synchronize events
 * - Enables script to wait for player to advance through text
 *
 * Completion Logic:
 * - Returns true only when ALL conditions met:
 *   1. System in dialogue mode (iGpffffbcf4 == 8)
 *   2. Valid stream pointer (pcGpffffaec0 != NULL)
 *   3. Stream at null terminator (*pcGpffffaec0 == '\0')
 * - Returns false otherwise (no dialogue, still processing, or system inactive)
 *
 * Related Opcodes:
 * - 0x33: advance_ip_and_sync_frame (frame sync, often used with 0x35 in loops)
 * - 0x34: probe_system_busy (similar query pattern for system state)
 * - Dialogue control opcodes (start/stop dialogue, embedded in streams)
 *
 * Script Example (Wait for Dialogue):
 * ```
 * [start_dialogue]           # Begin dialogue stream
 * :wait_loop
 * 0x35                       # Check if dialogue complete
 * 0x0D :wait_loop            # Loop back if not complete
 * 0x33                       # Sync frame (maintain timing)
 * # Dialogue complete, continue
 * ```
 *
 * Cross-References:
 * - docs/scr2_offset_tables_dialogue_voice_flow.md: Complete dialogue system overview
 * - analyzed/dialogue_start_stream.c: FUN_00237b38 analysis (dialogue starter)
 * - analyzed/dialogue_text_advance_tick.c: FUN_00237de8 analysis (frame advance)
 * - analyzed/dialogue_stream_recursive_parser.c: Stream parsing details
 */

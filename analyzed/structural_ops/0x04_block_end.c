/*
 * Structural opcode 0x04 — block end (handled inline in script_block_structure_interpreter)
 *
 * Summary:
 *   Structural delimiter marking the end of a nested block. Pops return address from
 *   stack and continues execution, or terminates interpreter if at base depth.
 *
 * Behavior:
 *   1. Increment depth counter
 *   2. If at base depth (was 0x10 before increment):
 *      - Advance pointer past 0x04 byte
 *      - Return from interpreter (script execution complete)
 *   3. Else (in nested block):
 *      - Pop return address from stack
 *      - Set pbGpffffbd60 to popped address
 *      - Continue execution at that location
 *
 * Subproc Chaining Pattern:
 *   In SCR files, sequences like `9E 0C 01 1E 0B 04 <id16-le>` are common:
 *   - 9E finish_process_slot: clears slot entry
 *   - 0C 01 1E 0B: evaluates to -1 (use current slot)
 *   - 04: ends current subproc block
 *   - <id16-le>: 16-bit little-endian ID of next subproc to chain
 *
 *   The 0x04 acts as a visual delimiter in bytecode between ending one subproc
 *   and beginning the next via the following 16-bit ID.
 *
 * Stack Management:
 *   Works in tandem with 0x32 (block begin) which pushes continuation addresses.
 *   Return stack grows downward from stack0xffffff60.
 *
 * Use Case:
 *   Critical structural primitive for nested script blocks, subproc sequencing,
 *   and script termination. NOT an arithmetic/VM opcode despite low value.
 *
 * Globals Accessed:
 *   - pbGpffffbd60: Script pointer (advanced or restored from stack)
 *   - depth_counter: Tracks nesting level (starts at 0x10)
 *   - return_stack: Manual stack of continuation addresses
 *
 * Side Effects:
 *   - Modifies control flow (return/pop)
 *   - May terminate interpreter execution
 *   - Following 16-bit value in stream may be consumed by next scheduled subproc
 *
 * Original: Handled inline in FUN_0025bc68 (no separate function)
 */

/*
 * NOTE: This opcode is NOT dispatched through the function pointer table.
 * It's handled specially in the main interpreter loop before table lookup.
 *
 * The logic is integrated directly into script_block_structure_interpreter
 * in analyzed/script_block_structure_interpreter.c
 *
 * Pseudo-code (from interpreter):
 *
 *   if (opcode == 0x04) {
 *     bool at_base = (depth_counter == BASE_DEPTH);
 *     depth_counter++;
 *     if (at_base) {
 *       pbGpffffbd60 = next;  // consume terminating 0x04
 *       return;                // exit interpreter
 *     }
 *     if (sp_index < 32) {
 *       pbGpffffbd60 = return_stack[sp_index];
 *       sp_index++;  // pop
 *     }
 *   }
 */

// No callable function - inline handling only

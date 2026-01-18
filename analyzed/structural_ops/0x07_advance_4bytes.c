/*
 * Structural opcode 0x07 — advance 4 bytes (LAB_0025bea8)
 *
 * Summary:
 *   Advances the instruction stream pointer by 4 bytes, skipping over embedded data.
 *
 * Behavior:
 *   Simply adds 4 to iGpffffbd60, moving the pointer forward in the instruction stream.
 *   Does not call any functions or modify other state.
 *
 * Use Case:
 *   Skip over embedded 32-bit data values (offsets, addresses, constants) that are
 *   stored inline in the script bytecode but not meant to be executed at this point.
 *
 * Relationship to Other Opcodes:
 *   - Opcode 0x09 is an alias (same implementation)
 *   - Similar to skip behavior in 0x01's false branch
 *
 * Globals Accessed:
 *   - iGpffffbd60: Instruction stream pointer (advanced by 4)
 *
 * Side Effects:
 *   - iGpffffbd60 += 4
 *
 * Original: LAB_0025bea8 (inline label in FUN_0025bc68)
 * Also used by: structural opcode 0x09
 */

#include <stdint.h>

extern int iGpffffbd60; // Instruction stream pointer

void structural_op_0x07_advance_4bytes(void)
{
  iGpffffbd60 += 4;
}

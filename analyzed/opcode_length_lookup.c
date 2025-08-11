// Decompiled function analysis for FUN_00238e50
// Original signature: undefined1 FUN_00238e50(uint param_1)
//
// Purpose:
//   Simple accessor returning the per-opcode length (byte) from the length table at PTR_DAT_0031c518.
//
// Behavior:
//   * Masks input opcode to 0xFF and returns table[opcode].
//
// Return:
//   Unsigned 8-bit value (length in bytes) for the provided opcode index.
//
// Notes:
//   - Table length presumed >= 256 (callers mask index). Actual known region used appears limited (< 0x80 currently observed).
//   - Zero lengths correspond to variable-length or recursively handled opcodes (e.g., 0x13, 0x15, 0x1E) per analysis.
//
// Future naming (tentative): get_dialogue_opcode_length
// Original name kept for traceability.

#include "orphen_globals.h"

extern unsigned char PTR_DAT_0031c518[]; // opcode length table

unsigned char get_dialogue_opcode_length(unsigned int opcode)
{
  return PTR_DAT_0031c518[opcode & 0xFF];
}

unsigned char FUN_00238e50(unsigned int param_1)
{ // wrapper preserving original symbol
  return get_dialogue_opcode_length(param_1);
}

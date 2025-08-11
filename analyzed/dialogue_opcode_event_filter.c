// Decompiled function analysis for FUN_00238c08
// Original signature: int FUN_00238c08(byte *param_1, uint param_2)
//
// Purpose:
//   Scans a dialogue/control byte stream collecting all opcodes >= 0x1F (non-control / data bytes
//   treated as raw text elsewhere) into a circular (?) buffer starting at &DAT_005716d8 + param_2.
//   Returns the number of such opcodes stored.
//
// Behavior details:
//   * param_1 points to start of an opcode stream terminated by a 0x00 byte.
//   * For each opcode:
//       - If opcode < 0x1F: treat as control; advance pointer by fixed length from PTR_DAT_0031c518[opcode].
//       - Else (opcode >= 0x1F): store byte at (&DAT_005716d8)[param_2]; increment param_2 modulo 256; advance one byte.
//   * Loop ends when a 0x00 byte is encountered (bVar1 == 0 check).
//   * param_2 masked to 8 bits at entry and each increment.
//
// Return:
//   Count of stored high opcodes (iVar3).
//
// Notes / Hypothesis:
//   - (&DAT_005716d8)[param_2] suggests a global ring / staging buffer for extended opcode capture.
//   - High opcodes (>=0x1F) may represent parameterized tokens, embedded control markers for another layer,
//     or raw text bytes if a different encoding is used; however original parser treated >0x1E as text pairs.
//   - This routine ignores nested variable-length constructs (0x13 recursion, 0x15 blocks); caller likely handles
//     them separately or the stream passed here excludes those.
//
// Future naming (tentative): collect_dialogue_extended_opcodes
// Original name retained for cross-reference.

#include "orphen_globals.h"

extern unsigned char PTR_DAT_0031c518[]; // opcode length table
extern unsigned char DAT_005716d8[];     // global buffer for captured opcodes (size unknown here)

int collect_dialogue_extended_opcodes(unsigned char *stream, unsigned int start_index)
{
  start_index &= 0xFF; // keep within 0-255 space
  int stored = 0;
  unsigned char op = *stream;
  while (op != 0)
  {
    if (op < 0x1F)
    {
      stream += (signed char)PTR_DAT_0031c518[op];
    }
    else
    {
      DAT_005716d8[start_index] = op;
      start_index = (start_index + 1) & 0xFF;
      stream += 1;
      stored++;
    }
    op = *stream;
  }
  return stored;
}

int FUN_00238c08(unsigned char *param_1, unsigned int param_2)
{ // wrapper with original name
  return collect_dialogue_extended_opcodes(param_1, param_2);
}

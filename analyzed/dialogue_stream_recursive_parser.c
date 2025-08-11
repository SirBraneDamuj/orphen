// Decompiled function analysis for FUN_00237ca0
// Original signature: undefined4 FUN_00237ca0(undefined4 *param_1, char param_2)
//
// Purpose:
//   This function walks a pointer through a dialogue / cutscene byte stream whose opcodes
//   are governed by the per-opcode length table at PTR_DAT_0031c518. It advances *param_1
//   until it encounters a target opcode (param_2) or reaches a terminal condition.
//
// High-level behavior:
//   * param_1 points to a mutable pointer (cursor) into a byte stream. The function copies it to a local
//     working pointer (apbStack_50[0]) and then iteratively advances.
//   * Bytes > 0x1E are treated as text payload (likely UTF-16LE pairs char+0x00) and skipped in 2-byte steps
//     until a control opcode (<=0x1E) is found.
//   * When a control opcode equals param_2 the function stops, writes the current cursor back to *param_1
//     and returns 1. (Effectively: "found requested opcode")
//   * Special handling:
//       - 0x15 : A complex variable-length block. Layout inferred from pattern: control byte (0x15), then
//                two bytes (likely parameters), then a count byte at offset +3. After advancing past the 4-byte
//                header it loops count times. For each iteration it consumes a (zero-terminated UTF-16LE-like)
//                string: loop increments pointer by 2 while first byte of pair is non-zero, then consumes the
//                terminating 0x00 (single byte). Continues until count exhausted.
//       - 0x13 : Recursive speaker/name block marker. The function calls itself with param_2 = -1 (0xFF...FF)
//                meaning it will parse through the nested block without early termination (since opcode values
//                are masked to signed char when compared) and then resume.
//       - Other opcodes (except <2) advance by the fixed length taken from PTR_DAT_0031c518[index].
//       - If the control opcode value is < 2 (i.e., 0x00 or 0x01) the function considers this an end condition,
//         writes current cursor and returns 0 (not-found / stream ended early).
//
// Return values:
//   1 -> target opcode encountered (cursor left pointing at that opcode)
//   0 -> terminated due to reaching opcode <2 without encountering target
//
// Side effects:
//   Updates *param_1 to final cursor location.
//   Performs recursive descent for nested 0x13 blocks.
//
// Notes:
//   - Length table access: *(char*)((int)&PTR_DAT_0031c518 + opcode) yields length in bytes for fixed-length opcodes.
//   - The recursion for 0x13 suggests nested speaker segments or grouped dialog events.
//   - The handling of 0x15 implies a selectable / branching text group (count followed by that many strings).
//   - param_2 comparison casts opcode to signed char; callers often pass literal small opcodes.
//
// Future naming (tentative): parse_dialogue_until(opcode_target)
// Keep original name in comment for traceability.

#include "orphen_globals.h" // expected to declare extern PTR_DAT_0031c518 if already analyzed

extern unsigned char PTR_DAT_0031c518[]; // length table (byte per opcode)

// Preserve original prototype semantics.
int dialogue_stream_parse_until(unsigned char **cursor_ptr, signed char target_opcode)
{
  unsigned char *p = *cursor_ptr;
  while (1)
  {
    // Skip textual payload pairs (>0x1E => treat as char + 0x00) until control encountered
    while (*p > 0x1E)
    {
      p += 2; // assumed UTF-16LE-like spacing (char,0x00)
    }
    signed char opcode = (signed char)(*p);
    if (opcode == target_opcode)
    {
      *cursor_ptr = p;
      return 1; // found target
    }
    if (opcode == 0x15)
    {
      // Complex variable block: header 4 bytes, 4th byte = count
      unsigned char count = p[3];
      p += 4; // skip header
      while (count-- != 0xFF)
      { // loop count times; decompiler showed decrement then compare != 0xFF
        // Consume UTF-16LE-like string until 0x00 "first byte" encountered
        while (*p != 0)
        {
          p += 2; // char + zero?
        }
        p += 1; // consume terminator byte
      }
      continue;
    }
    if ((unsigned char)opcode < 2)
    { // 0x00 or 0x01 terminator / sentinel
      *cursor_ptr = p;
      return 0; // target not found before end
    }
    if (opcode == 0x13)
    {
      // Recurse ignoring early termination (target -1 won't occur naturally in unsigned stream)
      dialogue_stream_parse_until(&p, (signed char)0xFF);
      continue;
    }
    // Generic fixed-length advance using length table
    p += (signed char)PTR_DAT_0031c518[(unsigned char)opcode];
  }
}

// Wrapper preserving original FUN_* name in a comment.
// Original: undefined4 FUN_00237ca0(undefined4 *param_1,char param_2)
int FUN_00237ca0(unsigned char **param_1, char param_2)
{ // keep callable by unanalyzed code
  return dialogue_stream_parse_until(param_1, (signed char)param_2);
}

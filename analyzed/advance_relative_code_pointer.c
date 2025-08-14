// Analyzed version of FUN_0025c220
// Purpose: Adjust the main VM code pointer (DAT_00355cd0) via a self-relative jump.
//
// Original decompiled body:
//   void FUN_0025c220(void) {
//       DAT_00355cd0 = (int *)((int)DAT_00355cd0 + *DAT_00355cd0);
//       return;
//   }
//
// Semantics (inferred):
//   DAT_00355cd0 points at a 32-bit cell (aligned) whose value encodes a relative delta (in bytes) to the next
//   instruction / data cell. The function advances the pointer by adding that delta to its current address.
//   This is a classic pattern for walking a table of self-relative length/pointer nodes (e.g., compressed /
//   packed instruction stream where each cell starts with a signed or unsigned byte count / address stride).
//
// Relationship to structural interpreter (script_block_structure_interpreter / FUN_0025bc68):
//   When a structural open opcode (0x32) is encountered, the structural interpreter:
//     * Stores a continuation return address (pb + 5) on its manual stack (skipping 4 header bytes following 0x32)
//     * Advances its own byte-stream pointer only by 1 (past the 0x32)
//     * Immediately calls FUN_0025c220()
//
//   Because the structural interpreter does NOT consume those 4 header bytes itself (it saved continuation = pb+5),
//   they are left in place for the main VM layer addressed by DAT_00355cd0. The call to FUN_0025c220 then uses the
//   current cell value (*DAT_00355cd0) to relocate the VM pointer—most likely landing inside or at the start of the
//   code body associated with the just-opened block. This strongly supports the header interpretation as a
//   self-relative offset cell, not arbitrary flags/ID bytes.
//
// Observed ladder chain (headers: 5d 00 00 00 → 48 00 00 00 → 33 00 00 00 → 1e 00 00 00 → 09 00 00 00):
//   These 32-bit little-endian values monotonically decrease. If each cell encodes how far to jump forward, a typical
//   pattern for nested packing is: outermost cell has largest skip (to skip over all nested bodies), inner cells have
//   progressively smaller skips. The structural interpreter's continuation stack ensures that after inner bodies finish
//   (terminated by 0x04), execution resumes exactly at the stored continuation (pb + 5 of the opener), meaning the
//   inner relative cell will be at the DAT_00355cd0 location at the moment of the FUN_0025c220 call for that level.
//
//   Thus, each nested 0x32 likely triggers:
//     1. Push (pb + 5) onto structural stack.
//     2. Call FUN_0025c220 to make DAT_00355cd0 jump into the code body whose length equals the header's value or
//        at least is bounded by it.
//     3. When a terminating 0x04 unwinds to the same structural depth, the structural interpreter pops back and
//        continues scanning after the 4-byte header region of that block—encountering the next (smaller) relative cell
//        for the next nested level and performing another FUN_0025c220 jump, etc.
//
// Open questions / pending validation:
//   * Signed vs unsigned: Decompiler treats *DAT_00355cd0 as int; need to inspect for negative deltas in live data.
//   * Unit: Byte deltas assumed (pointer arithmetic on raw address). Confirm by comparing known ladder region
//           physical offsets with cumulative delta sums (script dump parsing TODO).
//   * Whether the cell delta includes the size of the cell itself (i.e., is it a length field or a forward skip to next cell).
//   * Interaction with extended 0xFF opcodes: Those dispatchers also advance DAT_00355cd0; ordering with relative
//     jumps may impose constraints on cell layout.
//
// Practical next steps to cement interpretation:
//   1. Extract for each 0x32 start: the 4-byte little-endian header and compute target = header_base + header_value.
//      Verify target aligns to plausible code boundary (e.g., matches where another *DAT_00355cd0-based operation begins).
//   2. Check if every block's end (first 0x04) occurs before or at target; if strictly inside, header may be a total span
//      length including nested payload / postamble.
//   3. Validate no overlapping target windows violating nesting constraints.
//
// Naming suggestion: FUN_0025c220 => advance_relative_code_pointer (kept separate file to avoid modifying src/).
//
// NOTE: We refrain from renaming globals here; full naming deferred until corroborated across more call sites.
//
// Original signature preserved in comment:
// void FUN_0025c220(void);
//
// We do not re-implement logic; reference only.
//
// *No executable code here; documentation only.*

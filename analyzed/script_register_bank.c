// Analysis of FUN_0025c8f8 and FUN_0025c548: script register write/read bank
// Original names preserved in comments; this file assigns provisional semantic naming.
// DO NOT modify raw src/ versions.

/*
Overview:
  FUN_0025c8f8(param_id, value) acts as a register write dispatcher.
  FUN_0025c548(param_id) returns a (possibly transformed) value for the same register id.

  The register set mixes:
    - Raw 16-bit storage (short/ushort indices in puGpffffb0d4[])
    - 32-bit integers / pointers (cases 2,9,10,0xB,0xC, etc.)
    - Scaled floats written via division constants fGpffff8b** and quantized via FUN_00216690 / FUN_0030bd20.

  Many register IDs (0x17,0x1A..0x1E,0x28..0x2B,0x34..0x36) map to float scalars with distinct normalization divisors; likely world / camera / timing parameters.
  IDs 0x37..0x3F form a contiguous block writing int slots (array base + 0xCC stride) -> indexed set of additional parameters or extended vector components.

  IDs present in post-dialogue 0x5A0C record column3 set: 0x17,0x2A,0x31,0x34,0x35 (match actual implemented cases) supporting hypothesis that 5A0C records are scripted parameter loads into this bank.

Data structure clues:
  puGpffffb0d4: base pointer to active register frame (short * / mixed). Mirror read uses DAT_00355044. Likely double-buffer or staging vs active frame.
  Cases using FUN_00216690 wrap (float -> scaled int) for timing or angle quantization.

Partial mapping table (write side):
  ID 0x00..0x05 : short/int fundamental fields (entity/core indices?)
  ID 0x06       : left shift +1 before store (bit-packed half / scaling)
  ID 0x0D       : scaled float -> 0x2E offset (timing?)
  ID 0x0E       : raw float/time at offset 0x24
  ID 0x13       : scaled float -> offset 0x26
  ID 0x17       : scaled float -> offset 0x62 (matches frequent script usage)
  ID 0x1A..0x1E : series of floats at offsets 0x18..0x22 (vector / position / orientation?)
  ID 0x1F..0x20 : scaled floats at 0xAA / 0xAC (extended metrics)
  ID 0x21       : float at 0x3E (maybe speed or progress) + broadcast in special case when base == DAT_0058beb0
  ID 0x28..0x2B : float series 0x2A..0x90 (second vector block?)
  ID 0x34..0x36 : float series 0xA0..0xA4
  ID 0x37       : byte at +0x133 (flag/state)
  ID 0x38..0x3F : ints at array starting 0xCC (eight contiguous extended registers)
  ID 0x40       : byte at 0x4B (another flag)

Read side parallels same layout.

Implication for 5A0C record:
  Byte3 (B) is very likely the register id param_id passed to FUN_0025c8f8.
  Byte8 (0x0E constant) could be the small raw integer value that becomes a scaled float when written to registers expecting scaling. For registers expecting a float, script probably expands the small value before calling write.
  Byte7 (verb) may decide whether to write raw, OR, AND, or apply an arithmetic op via intermediate usage of FUN_00260360 (which performs AND/OR/XOR/add/sub patterns for opcodes 0x77..0x7C). Distinct sets (0x37 vs 0x79) correspond to different arithmetic categories or immediate vs deferred.

Next focus: locate parser that:
  - pulls 0x5A tag
  - reads 0x0C bytes
  - dispatches FUN_0025c8f8 (direct or via FUN_00260360).

Heuristic search path (pending): grep for loops calling FUN_0025c8f8 repeatedly; count frequency of immediate constants 0x0C around those loops.

*/

// No executable code here; purely documentation / mapping aid.

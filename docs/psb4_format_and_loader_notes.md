# PSB4 format and loader notes (code-derived)

Source: `src/FUN_0022ce60.c` (magic check `0x34425350`), with auxiliary calls to
`FUN_002f3118`, `FUN_002256d0`, `FUN_002256f0`, and `FUN_00211b80`.

This document records only what the code proves; unknowns remain marked as TBD.

## Header (relative offsets)

- +0x00: u32 magic 'PSB4' = 0x34425350
- +0x04: u32 offs_sectionA
- +0x08: u32 offs_sectionB
- +0x0C: u32 offs_sectionC

All offsets are relative to the start of the decoded blob at 0x01849A00.

## Section A (offs_sectionA)

- +0x00: s16 countA
- Per entry (loop countA):
  - Reads u32 at entry+0x04, stores
  - Reads u32 at entry+0x0C, stores
  - Reads u32 from a parallel table at (base + offs_sectionA + 0x08) stepping 3 dwords per entry; stores the first of those
- Destination: a per-slot table starting at `&DAT_00345a20[slot]` (aligned), 0x10-byte stride, with some zeroed fields.

## Section B (offs_sectionB)

- +0x00: s16 countB
- Per record (~24 bytes input each):
  - u16 flags0 (bit 0x800 changes subsequent byte packing)
  - 4 x u16 (8 bytes) copied into the output record
  - 4 x blocks of 3 bytes (12 bytes total). If 0x800 is clear, these 12 bytes go into a byte quartet near +0x0B with alpha forced to 0x80; if set, they split across two regions (+0x19 and a byte near +0x08).
- Destination: same arena region, 0x24-byte stride per record, with a companion u16 near +0x20 receiving `flags0`.

## Section C (offs_sectionC)

- +0x00: s16 groupCount
- For each group g in [0..groupCount):
  - s16 n (count)
  - n \* 3 s16 values copied sequentially to the output
- Output pointer saved to `&DAT_00345a28[slot]`; an aligned scratch/output pointer saved to `&DAT_00345a2C[slot]`.
- The code then calls `FUN_002256d0(&DAT_00345a28[slot])` to determine a byte count, advances the arena, and calls `FUN_002256f0` to populate that aligned region.

## Mesh relevance

- No vertex coordinate table is read here (no s16→float scaling, no float4 vectors).
- Section C’s `(n * 3)` pattern is consistent with triangle index lists, but there’s no evidence of positions within PSB4. If PSB4 encodes geometry, vertices are external and must be resolved elsewhere.

## Runtime notes

- Uses a global arena cursor `psGpffffb7bc` with 16B then 4B alignment steps.
- On slot 0, sets a byte at `DAT_00345a38` to 0x80; other slots set a corresponding offset to 0.
- Final handoff via `FUN_00211b80(&DAT_00345a18[slot])` after table construction.

## TBD / Open questions

- Exact meaning of Section A’s triple of u32 sources and their roles in the output 0x10-byte record.
- Full mapping of Section B output record layout; byte fields appear color-like but could be LUTs or packed parameters.
- What `FUN_002256d0/002256f0` generate for Section C (likely acceleration structures or remaps).
- The consumer(s) of these per-slot tables; finding cross-references to `&DAT_00345a18/20/24/28/2C` should clarify how indices are used.

## Cross-references

- Magic check: `src/FUN_0022ce60.c`
- PSM2 reference for contrast: `src/FUN_0022b5a8.c` (uses the same decoder then a very different pipeline)

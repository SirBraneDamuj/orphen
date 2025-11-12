// Analyzed from: src/FUN_0022ce60.c
// Original symbol: FUN_0022ce60
//
// Purpose
//   Loader/processor for PSB4 chunks. Validates magic in the global staging buffer
//   (0x01849A00), then interprets three header-referenced sections (+0x04, +0x08, +0x0C)
//   and writes parsed tables into a per-slot workspace (arrays at DAT_00345a**).
//
// Observed behavior (code-driven)
// - Calls the global decoder FUN_002f3118() first (no explicit args in this variant).
//   After return, the decoded blob resides at the global KSEG1 region 0x01849A00.
// - Checks magic at [0x01849A00] == 'PSB4' (0x34425350). On mismatch, logs error.
// - Uses header u32 fields at offsets +0x04, +0x08, +0x0C as base-relative offsets to
//   three sections (A, B, C) within the PSB4 payload.
// - Consumes a caller-supplied slot index (param_2), and populates a family of tables
//   located under globals DAT_00345a18/20/24/28/2C for that slot.
// - Uses psGpffffb7bc as a moving scratch/arena pointer, keeping 16-byte and 4-byte alignment.
// - Invokes FUN_002256d0 / FUN_002256f0 on the Section C output to build auxiliary data.
// - Finalizes with FUN_00211b80(&DAT_00345a18[slot]) to hand off the built structures.
//
// Header fields (from global access pattern):
//   base+0x00: u32 magic 'PSB4' (0x34425350)
//   base+0x04: u32 offs_sectionA  (aka DAT_01849a04)
//   base+0x08: u32 offs_sectionB  (aka DAT_01849a08)
//   base+0x0C: u32 offs_sectionC  (aka DAT_01849a0C)
//   (Other header fields may exist; this function only uses these three and possibly
//    writes additional tables via FUN_002256d0/f0.)
//
// Section A layout (at base + offs_sectionA):
//   +0x00: s16 countA
//   Followed by countA entries; for each entry, code copies two u32 values from:
//     - *(u32*)(entry + 0x04)
//     - *(u32*)(entry + 0x0C)
//     and one u32 from a parallel table at (base + offs_sectionA + 0x08) stepping by 3 u32s per entry.
//   Destination is &DAT_00345a20[slot] (aligned), with a 0x10-byte stride per entry and some
//   zero-initialized fields.
//
// Section B layout (at base + offs_sectionB):
//   +0x00: s16 countB
//   Then countB records, each consuming 24 input bytes:
//     - u16 flags0 (bit 0x800 changes encoding of the next 12 bytes)
//     - four u16 values (8 bytes) copied into the destination record
//     - four blocks of 3 bytes (12 bytes total). If (flags0 & 0x800) == 0, bytes go to one set of
//       positions (interpretable as color RGBA-like with a forced 0x80 alpha). Else, they get split
//       across two byte regions, and one source byte is reused in the other region.
//   Destination stride advances by 0x24 bytes per record within the same arena region seeded by
//   &DAT_00345a24[slot].
//
// Section C layout (at base + offs_sectionC):
//   +0x00: s16 groupCount
//   For each group g in [0..groupCount):
//     - s16 n (count)
//     - then n * 3 consecutive s16s are copied to the output buffer.
//   This pattern is a strong indicator of triangle index lists (n triplets). The function does
//   not pair these with vertex positions here; positions are not read from PSB4 in this path.
//   The output pointer for this section is stored in &DAT_00345a28[slot], and a secondary pointer
//   (aligned) in &DAT_00345a2C[slot].
//
// Mesh assessment (based on code):
// - No reads of s16 XYZ scaled by 1/2048.0 (the PSC3 vertex pattern) are present here.
// - No reads from a float4 vector table appear here either.
// - The only geometry-shaped structure is Section C’s (n * 3) s16 sequences per group, i.e.
//   triangle index lists. Therefore, PSB4 does not embed vertex coordinate tables; any faces
//   here would index vertices defined elsewhere, or the structure serves a non-geometry purpose
//   (e.g., logical/trigger triangles or sprite quads decomposed into triangles).
//
// PS2-specific notes
// - psGpffffb7bc appears to be a global bump allocator cursor used across loaders; this function
//   aligns it to 16B and 4B boundaries as needed.
// - The auxiliary builders FUN_002256d0 / FUN_002256f0 likely compute per-group lookup tables
//   or compressed representations; they take (&DAT_00345a28[slot], alignedOut) as inputs.
//
// Implementation sketch (comment-only):
//   void process_psb4_loader(int slot) { /* mirror control flow of FUN_0022ce60, see above */ }
//
// Keep original FUN_* names in comments until those callees are analyzed separately.

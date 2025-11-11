## PSC3 format and loader notes

This document distills the code-backed behavior of the PSC3 loader/relocator/renderer, derived from the following decompiled functions:

- FUN_00222498 — top-level PSC3 loader and command buffer builder
- FUN_00223268 — resource reader (staging read path)
- FUN_002f3118 — custom LZ-like decompressor
- FUN_00221f60 — relocate PSC3 internal pointers after copy/decode
- FUN_00221e70 — optional extended setup that appends 4 tables to the arena
- FUN_002256d0 / FUN_002256f0 — helper size calculator and table initializer
- FUN_00212058 — renderer/command buffer builder consuming PSC3 sections

Where relevant, strings.json/globals.json addresses are referenced; raw decompiled code remains in `src/` and must be treated as source of truth.

### Memory flow

- Staging buffer: 0x1849A00
- Decoded arena cursor: DAT_0035572c (bumped as allocations occur; always aligned to 16 bytes)
- Last decoded size: DAT_00355720 (bytes written by FUN_002f3118)

### PSC3 loader (FUN_00222498)

1. Reads requested segment into 0x1849A00 via FUN_00223268.
2. Decodes into arena via FUN_002f3118 (or acts as a degenerate copy), which sets DAT_00355720.
3. If request flag bit0 set, asserts PSC3 magic 0x33435350 at the decoded base.
4. Stores the decoded base pointer into the request at +0x28.
5. Aligns DAT_00355720 to 16 bytes and marks request[+5] “loaded”.
6. Arena advance and relocation:
   - If not PSC3-flagged, advance by DAT_00355720.
   - If PSC3-flagged, advance by header dword[7] (reserved size) and call FUN_00221f60 to copy and add base delta to header fields at +0x1C/+0x20/+0x24/+0x28.
7. If PSC3-flagged and header+0x40 != 0, call FUN_00221e70 to append four tables; update arena pointer.
8. Call FUN_00212058 to build the render/engine command buffer into the arena; update request +0x28 to point to it.
9. Align arena to 16; overflow check against 0x18499FF; write total size consumed to request +0x20.

### PSC3 header fields (partial, from consumer code)

- +0x00: Magic ‘PSC3’ (0x33435350)
- +0x04: u16 submesh_count
- +0x06: u16 reserved
- +0x08: u32 offs_submesh_list
  - N entries x 0x14 bytes; entry[+6] (u16) participates in computing max stream counts in FUN_00212058.
- +0x14: u32 offs_vertex_record_table
  - Per-vertex records, stride 10 bytes. Layout (inferred from FUN_002129b8):
    - +0: x (s16) / 2048.0
    - +2: y (s16) / 2048.0
    - +4: z (s16) / 2048.0
    - +6: u16 index into float4 table at +0x28 (normal/tangent/weights)
    - +8: u16 TBD (likely UV or secondary attribute; not yet consumed in traced code)
- +0x18: u32 offs_vertex_byte_table
  - One byte per vertex (indexed by vertex id) used to build a per-vertex scalar (byte*4 + 0x20) in FUN_002129b8; semantics TBD (alpha? weight?).
- +0x1C: u32 offs_draw_desc_table
  - Entries x 0x18 bytes; renderer iterates these per submesh.
  - Offsets within entry used by renderer:
    - +0x04 / +0x06: u16s whose equality toggles a state (3 vs 4 in code).
    - +0x08: u16 flags — bits 0x20, 0x100, 0x200, 0x400, 0x800 influence state and command stream.
    - +0x0E..+0x16: 4 x i16 “stream indices”; each -1 if unused. Renderer picks the last non -1.
- +0x20: u32 offs_color_table
  - Color entries packed as 3 bytes per vertex (accessed via helpers FUN_00212cf0 / FUN_00212d28 inside FUN_002129b8). Combined with state bits to form draw color words.
- +0x24: u32 offs_resource_table
  - Entries of 10 bytes; renderer indexes this by the chosen stream index to fetch a u16 field used as a compact format/flag descriptor.
  - The top bits (0xC000) and low 7 bits participate in deriving counts and flags.
- +0x28: u32 offs_float4_table
  - Array of float4 (16 bytes each). FUN_002129b8 indexes with the u16 at vertex record +6; xyz used, w currently ignored (set to 0.0 when copying).
- +0x40: u32 offs_subheader (optional)
  - If present and request bit0 set, FUN_00221e70 constructs four initialized tables appended into the arena. The size calculator is `((count-1)*10 + 3 aligned to 4) + 0x10`, where `count` is the 16-bit value at the subheader base.
- dword[7] (offset +0x1C in the header dwords array): reserved size used to advance the arena in the compressed path (the copy/relocation covers the difference `DAT_00355720 - reservedSize`).

### Relocation details (FUN_00221f60)

After decoding/copying, the engine copies bytes from the old base to the new arena address, then adds the base delta to four header fields:

- header +0x1C
- header +0x20
- header +0x24
- header +0x28

Treat these fields as base-relative offsets when parsing offline; adjust to absolute pointers or rebase as needed.

### Extended setup (FUN_00221e70, FUN_002256d0, FUN_002256f0)

If request bit0 is set and header+0x40 != 0:

1. Rebase param_2 to (psc3Base + \*(u32)(psc3Base+0x40)).
2. OR request flags with 0x04.
3. For 4 iterations: compute block size from the 16-bit count at the subheader, emit a block with:
   - u32 pointer back to the subheader base (same for all four blocks)
   - count records of 10 bytes initialized to defaults {0,0,0,0xFF,1,0,0}
   - block size formula: `((count-1)*10 + 3 & ~3) + 0x10`
4. Store the addresses of the 4 blocks back into the request at +0x18,+0x1C,+0x20,+0x24.
5. Return the next 16-byte aligned arena pointer.

Note: The decompiled code does not advance the subheader pointer per block, implying all four tables share the same count/source base.

### Renderer/command buffer consumption (FUN_00212058)

Inputs:

- param_2: PSC3 base pointer
  - Uses (param_2 + \*(u32)(+0x08)) as submesh list base (N x 0x14 entries)
  - Uses (param_2 + \*(u32)(+0x1C)) as draw descriptor table (entry size 0x18)
  - Uses (param_2 + \*(u32)(+0x24)) as 10-byte resource records table

Behavior (high level):

- Computes the maximum stream count across submeshes from a short at submesh[+6].
- For each entry in the draw descriptor table, it selects a stream index from the last non -1 of the four shorts at +0x0E..+0x16.
- Fetches a u16 from the resource table at index\*10 + 0x08, parses top bits and low bits for state (1–3, 0x40 flag, etc.).
- Builds a set of engine/GPU state and draw commands into an output buffer, writing the pointer back to request at +0x28.
- Enforces alignment and a size bound (<= 1MB-ish, see code path) and sets a leading header word with a “packet length - 1” field (0x6… prefix) for the command buffer.

### Practical offline parsing guidance

To mirror in-game behavior for an offline parser:

1. Treat +0x1C/+0x20/+0x24/+0x28 as base-relative offsets. Convert to absolute pointers using the decoded base.
2. For compressed PSC3s: the header’s dword[7] indicates reserved size, but the decoded byte count can differ. For an offline parser reading already-decoded bytes, you can ignore the reserved-size dance and operate on the absolute pointers.
3. Use +0x08 to locate the submesh list (N x 0x14). The short at +6 (per entry) influences max stream counts; it’s not strictly needed to extract geometry but is used by the renderer for command layout.
4. Use +0x1C to iterate draw descriptors (0x18 each) and read:
   - flags at +8
   - stream indices at +0x0E..+0x16 (pick last non -1)
   - the pair of u16s at +4/+6 (used by the renderer to choose between 3 or 4 in a local state variable)
5. Use +0x24 to fetch 10-byte resource records; the u16 at +8 packs flags/format (top two bits, low 7 bits observed in code).
6. Extended subheader at +0x40 is optional; if present, you can emulate FUN_00221e70 to build and attach the four initialized tables for parity with runtime.

Open items (TBD through further code reading):
- Meaning of vertex record field at +8 (potential UV / secondary index).
- Detailed palette / CLUT logic for color table (+0x20) beyond per-vertex triple.
- Full index/face reconstruction workflow (draw call batching, primitive type) — current plan uses sequential triangles for offline visualization.
- Definitive meanings for the 10-byte resource record fields; presently only the 16-bit at +8 is clearly used.
- Exact geometry/indices location and format — likely referenced in other renderer code paths or via VIF unpack routines.

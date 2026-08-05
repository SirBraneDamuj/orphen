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

### PSC3 header fields

Revised 2026-08-02 after reading `FUN_00212058`, `FUN_002129b8` and
`FUN_0020c810` line by line, and validating the result against the nine models
in `s01_e024` plus the chest model `grp_0172`. The earlier version of this
section had three fields wrong or missing; those are called out inline. The
port's parser is `port/src/ported/model/psc3_model.{h,cpp}`.

- +0x00: Magic ‘PSC3’ (0x33435350)
- +0x04: u16 submesh_count. **This is also the bone count** — `FUN_0020eec0`
  uploads exactly one 64-byte matrix per submesh to VU1.
- +0x06: u16 reserved
- +0x08: u32 offs_submesh_list
  - N entries x 0x14 bytes.
    - +0x00 / +0x02: u16 vertex stream window
    - +0x04 / +0x06: u16 primitive range. `FUN_00212058` loops to `max(+0x06)`
      across all submeshes; there is no stored primitive count.
    - +0x08: u16 byte length of this submesh's slab
    - +0x0A: u16. Its low byte holds the parent bone index for every non-root
      bone, but the game never reads it and root bones carry junk there. Build
      the hierarchy from the child lists instead (see +0x10).
    - +0x0C: u16 byte offset, from the model base, of this bone's **child bone
      list**. Zero means a leaf. `FUN_0020d618` recurses over it.
    - +0x10: u32 byte offset into the section at +0x10's region
- +0x0C: **u32 offs_anim_table** — missing from the earlier revision. This is
  what `FUN_00229c40` stores into entity `+0x9C`; entity `+0xA0` indexes it.
- +0x10: **u32 offs_root_bone_list** — missing from the earlier revision. Not a
  single list but a region of bone-index byte lists, each terminated by a byte
  with bit 7 set (`FUN_0020c810` tests `< 0x80`). The list at this offset holds
  the roots; each bone's own child list is found through its submesh +0x0C.
  Walking roots → children reaches every bone in all ten models checked, with
  none left over.
- +0x14: u32 offs_vertex_record_table
  - Per-vertex records, stride 10 bytes:
    - +0: x (s16) / 2048.0
    - +2: y (s16) / 2048.0
    - +4: z (s16) / 2048.0
    - +6: u16 index into the float4 table at +0x28 (the vertex normal, used when
      the primitive's flag 0x8 is set)
    - +8: u16. Read by nothing in the traced code and zero in every vertex of
      every `s01_e024` model.
  - Positions are **bone-local**, so a model's raw bounds are much smaller than
    the character it draws; they only mean anything after the pose is applied.
- +0x18: u32 offs_vertex_bone_table
  - One byte per vertex. `FUN_002129b8` line 56 writes `byte * 4 + 0x20` into
    the w component of the vertex stream: that is a VU1 *address* — palette base
    0x20, four quadwords per 4x4 matrix — so the byte is the vertex's **bone
    index**. Skinning is therefore one rigid bone per vertex with no weights.
    The earlier revision listed this as "semantics TBD (alpha? weight?)".
    Checked against `grp_0091`: 81 bytes in the range 2..14 for 81 vertices with
    16 submeshes, then padding.
- +0x1C: u32 offs_primitive_table
  - Entries x 0x18 bytes:
    - +0x00..+0x06: 4 x u16 vertex indices. **v2 == v3 means a triangle**,
      otherwise a quad (`FUN_00212058` line 108).
    - +0x08: u16 flags — 0x20 skip, 0x8 per-vertex normal *and* colour, 0x100
      suppresses the +0x0C byte, 0x200 selects the untextured colour path,
      0x400 clears the blend bit, 0x800 affects the texture mode.
    - +0x0A: u16 base index into the colour table; corner *i* reads entry
      `+0x0A + i`.
    - +0x0C: u8 fog/detail byte, only applied on the last active pass.
    - +0x0D: u8 alpha.
    - +0x0E..+0x14: 4 x i16 subdraw indices, one per pass. -1 means unused.
      A **negative value other than -1 is not a subdraw index**: `FUN_002129b8`
      masks off bit 15 and uses the rest as a colour index, drawing the pass
      untextured.
    - +0x16: u16 flat normal index, used when flag 0x8 is clear.
- +0x20: u32 offs_color_table
  - Three bytes per entry, indexed as described under primitive +0x0A. Read
    through `FUN_00212cf0` (untextured path) or `FUN_00212d28` (textured path).
- +0x24: u32 offs_subdraw_table — the earlier revision called this a "resource
  table". Stride 10:
  - +0x00..+0x06: 4 x u16, one per corner, each packing `(U << 8) | V` as 8-bit
    texel coordinates over a 256x256 page.
  - +0x08: u16 texture flags, split by `FUN_00212058` as
    bits 15..14 blend mode, 13..11 texture bank (+7 when non-zero),
    10..7 texture slot (0xF = none), 6..0 alpha (0x7F meaning 0x80).
    `tools/resource_extract/v2/psc3_full.py` reads bits 14..8 as a single
    "atlas slot"; that grouping does not match the code.
- +0x28: u32 offs_normal_table
  - Array of float4 (16 bytes each). `FUN_002129b8` copies xyz and forces w to 0.
- +0x2C: **u32 offs_keyframe_pool** — missing from the earlier revision. The s16
  quaternion and translation keys `FUN_0020d378` samples. Quantisation:
  `quat.xyz = s16 / 2048`, `quat.w = s16 / 4096`, `translation = s16 /
  DAT_00352060`. That divisor reads **10430.380859** in both available EE dumps,
  so it is a constant; `psc3_full.py` guesses 2048 for it and is wrong.
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

Resolved since the original revision (see the header section above):

- Geometry, indices and primitive type — the primitive table at +0x1C carries
  four vertex indices per entry with `v2 == v3` marking a triangle. No sequential
  guessing is needed.
- UVs — per-corner, in the subdraw table at +0x24.
- The vertex byte table at +0x18 — per-vertex bone index.
- The keyframe pool, animation table and bone hierarchy — +0x2C, +0x0C, +0x10.

Still open:

- Meaning of vertex record field at +8. Zero in every model checked.
- The low byte of the subdraw texture flags (bits 6..0) is read as an alpha
  value by `FUN_00212058`, but how the 4-bit slot and 3-bit bank at 10..7 and
  13..11 index the runtime texture-slot cache (`DAT_00315a98`) is not confirmed.
- `FUN_002103d0`, the GS BITBLT packet builder that uploads a decoded texture to
  VRAM. Not needed by the GL port, which binds a texture object per slot.
- Section A, the region each submesh's +0x10 points into. Not touched by the
  rasteriser.

## Entity type 0x272 and the MAP.BIN model archive

`s01_e024` pool slot 10 is a type `0x272` "map-streamed prop" — a second chest,
larger and lit differently than the type `0x3A` ones. The port draws its debug
box but no geometry. Two separate reasons, both established from `s01_e24.bin`.

**Its model record is not ELF data.** Slot 11 (a normal chest) has
`entity+0x160 = 0x0031F814`, inside the ELF's static descriptor table. Slot 10
has `0x00F4668C` — heap. That address sits in a table of `0x2C`-byte records for
consecutive mesh ids:

```
0x00F4668C  mesh=0x009F tex=0x012F flags=0x41 loaded=1 slot=18 psc3=0x011F09E0
0x00F466B8  mesh=0x00A0 tex=0x012F flags=0x41 loaded=0
0x00F466E4  mesh=0x00A1 tex=0x0287 flags=0x41 loaded=0
0x00F46710  mesh=0x00A2 tex=0x0133 flags=0x41 loaded=0
0x00F4673C  mesh=0x00A3 tex=0x0268 flags=0x41 loaded=0
```

Flags `0x41` is bit 0 (PSC3 path) plus bit 6 (texture id stored negated), and the
assigned slot 18 is exactly where the dump's cache half holds `-303` — `0x012F`
negated. That is an independent confirmation that the record is read correctly.
Where the table itself is loaded from is **not** identified.

**The model is in none of the bundles the port opens.** `grp_009F` is absent
from all 190 populated MCB scene bundles and from the `s00_e000` boot bundle,
whose ids in that range run `009a 009b 009c 009d 00a0 00a1 00a3` — genuinely
sparse, missing `009e`, `009f` and `00a2`.

**`MAP.BIN` is the home, and the port never reads it.** The decompilation does
reference it. `PTR_s_GRP_BIN_00315a58` is a nine-entry table of archive names,
and `MAP.BIN` is index 2:

```
0 GRP.BIN   1 SCR.BIN   2 MAP.BIN   3 TEX.BIN   4 ITM.BIN
5 (0x34bb78) 6 SND.BIN  7 MCB0.BIN  8 MCB1.BIN
```

`FUN_00223268(archiveIndex, resourceId, dest)` is the generic loader. It
switches on the archive index to choose a per-archive offset lookup; case 2 is
`FUN_00221b48`, a flat u32 table:

```c
return *(undefined4 *)(param_1 * 4 + iGpffffbc28);   // iGpffffbc28 == 0x00355B98
```

The caller seeks with `(entry >> 17) << 11` — sector index x 2048
(`FUN_00223268:85`, and `FUN_00223038` on the non-debug path).

That table is the head of `MAP.BIN` itself, entry `[id]` at byte offset
`id * 4`. Decoding it reproduces the archive:

```
id 0x01 -> 0x001000   PSM2
id 0x02 -> 0x019800   PSM2
id 0x9F -> 0xCC2800   PSC3    <- grp_009F
```

`0xCC2800` is the address reached independently by searching for the loaded
model's header signature. Two routes agreeing settles what a header match alone
could not: the model there **is** `grp_009F`. The archive holds 500 `PSC3`
models and 165 `PSM2` maps, uncompressed.

**Still open, and worth settling before trusting a parser:**

- Records begin slightly *before* their magic — one byte for the `PSM2`
  entries, two for this `PSC3` one — so the per-record framing is not pinned
  down. The two bytes at `0xCC2800` are `20 2E`; `0x2E20` = 11808 would be a
  plausible size for this model, but that is a guess, not a reading.
- Past the header the on-disc bytes diverge from the loaded copy far more than
  the `+0x1C..+0x2B` pointer relocation accounts for: 3582 of the first 4096
  differ, in scattered runs. Something else is rewritten at load.
- Where the `0x2C` record table at `0x00F466xx` comes from is still unknown.
  Locating the model does not by itself give the port the mesh/tex pairing for
  type `0x272`, so both halves are needed to draw it.

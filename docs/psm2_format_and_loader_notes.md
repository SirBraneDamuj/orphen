# PSM2 format and loader notes (code-derived)

Source: `src/FUN_0022b5a8.c` (magic 0x324D5350). This document summarizes only what the
code does: offsets read, counts, strides, and copy patterns. No speculative semantics.

## Top-level

- Decode via `FUN_002f3118()`; verify magic at 0x01849A00.
- Use many header-relative offsets (stored in globals named `DAT_01849aXX`) to locate sections.
- Allocate and align buffers using the arena cursor `DAT_0035572c`.
- Finish with `FUN_0022c3d8`, `FUN_0022c6e8`, `FUN_0022d258`, `FUN_00211230`.

## Header fields consumed

- +0x04 → Section A
- +0x08 → Section C
- +0x0C → Section D
- +0x10 → Section K
- +0x14 → Section E
- +0x18 → Section F
- +0x1C → Section J
- +0x2C → Section G
- +0x30 → Section B
- +0x34 → Section H
- +0x38 → Section I

## Sections (letter labels only for reference)

A) +0x04: 0x20-byte records

- Reads s16 countA.
- For each entry: copies 6 dwords into offsets +0x00..+0x14; zeroes +0x18,+0x1C; writes 0xFFFF at +0x1A.
- Dest base: `DAT_003556d8` (4B aligned). Count stored in `DAT_003556d4`. `DAT_003556a4` marks the aligned end.

B) +0x30: two adjacent tables of 0x10-byte rows

- Reads a count via `FUN_0022b4e0` → `DAT_0035568c`.
- Each row: three ints written at +0x00,+0x04,+0x08; +0x0C zero.
- Allocates two copies back-to-back: first at `DAT_003556a4`, second at `DAT_003556a8`.

C) +0x08: mixed arrays derived via `FUN_0022b4e0`

- Reads s16 countC.
- Lays out:
  - `DAT_00355694`: u8[countC]
  - `DAT_00355698`: u16[countC]
  - `DAT_0035569c`: countC records, stride 0x10: dword[0], dword[1], dword[3]=0
  - `DAT_003556a0`: duplicate copy of those records
- For each i: reads two ints → 5569c[0], 5569c[1]; reads a u16 → 55698[i]; reads a following byte → 55694[i].

D) +0x0C: paired arrays of 0x80-byte and 0x78-byte records

- Reads s16 countD.
- For each i:
  - Copies four u16s into both arrays (different offsets).
  - Writes two u32s into the 0x78-record head and two u32s into the 0x80-record (+0x10 and +0x70).
  - Writes three spaced u16s to the 0x80-record at +0x30/+0x3C/+0x48.
  - Writes a u16 and a dword (+0x10/+0x04) and two bytes (+0x12/+0x13) into the 0x78-record.
  - Reads a u16 index k; copies a 0x10-byte record from `DAT_003556a4 + k*0x10` into the front of the 0x80-record and stores k.
- Bases: `DAT_003556ac` (0x80 stride), `DAT_003556b0` (0x78 stride).

E) +0x14: compact 0x10-byte rows (12 meaningful bytes)

- Reads count via `FUN_0022b520` → `DAT_00355690`.
- For each row, reads 6 pairs of bytes (12 bytes) and stores into a 0x10-byte stride at `DAT_003556b4`, blanking the remainder.

F) +0x18: 0x1000-short table and trailing list

- If absent: fill 0x1000 shorts at `&DAT_00345a18` with 0xFFFF; `DAT_003556ec=0`.
- Else: copy 0x1000 shorts from file into `&DAT_00343a18` then read a count and copy that many shorts into `DAT_003556f0`.

G) +0x2C: grouped triangle index lists

- Reads s16 groupCount; for each: s16 n then n\*3 s16 values packed.
- Stores base pointer to `DAT_003556f4`, aligns aux buffer `DAT_003556f8`, and invokes `FUN_002256d0/002256f0` on them.

H) +0x34: 0x10-byte rows of 4 ints

- Reads count via `FUN_0022b4e0` → `DAT_003556e4`.
- Per row: 3 ints then a 4th int into `DAT_003556e8`.

I) +0x38: 0x20-byte rows of 2x2 ints (0x100-byte base zero-initialized)

- Reads count via `FUN_0022b4e0` → `DAT_003556b8`.
- Stores 2x2 integers per row at `DAT_003556c8 + i*0x20`.

J) +0x1C: 0x74-byte records and per-record float buffers

- Reads s16 countJ → `DAT_003556dc`; base `DAT_003556e0`.
- For each record:
  - Zero 0x74 bytes; copy 13 shorts (0x1C bytes); zero 0x56 bytes; set byte at +0x5A to 3.
  - Allocate aligned float buffer; store ptr at +0x1C.
  - Using record[+0] as an index, update the u16 at +0x1A of the corresponding Section A record with the current record index.
  - Load a source float pointer derived from Section C; store at +0x20; write sequential float triplets to the buffer as differences from the record’s base floats.
  - Append one more difference triplet from the Section A record’s +0x08/+0x0C/+0x10 values.
- If countJ < 0x80: reserve padding regions for both the per-record float buffers and an additional area sized at 0x18 bytes per (up to 0x80) records.

K) +0x10: 3-byte rows

- Reads s16 countK → `DAT_00355be0`.
- Copies 3 bytes per row into `DAT_00355bdc`.

## Cross-references

- Decoder: `FUN_002f3118`
- Variable readers: `FUN_0022b4e0`, `FUN_0022b520`
- Tri index helper: `FUN_002256d0`, `FUN_002256f0`
- Final passes: `FUN_0022c3d8`, `FUN_0022c6e8`, `FUN_0022d258`, `FUN_00211230`

## Mesh-related signals (code-level)

- Section G mirrors the grouped (n\*3) s16 index pattern (triangles).
- Section J allocates per-record float buffers and writes triplets computed from an existing float array, with an extra closing triplet; this is consistent with per-record vector lists. The code does not reveal more without following consumers of these globals.

This doc intentionally avoids assigning semantic names beyond what the code demonstrates. Further meaning should be established by analyzing consumers of `DAT_003556**/00355***` tables.

---

## Post-load passes and how the data is used (code-derived)

This section summarizes what the follow-up functions do with the structures created above. It uses only
behaviors visible in the decompiled code paths, keeping original FUN\_\* identifiers for traceability.

### FUN_0022c3d8

- Iterates D-records (`DAT_003556ac` base, 0x80 stride).
- Decodes 3-byte rows from K (`DAT_00355bdc`) into u32s and writes them into the 0x80-record at +0x10,+0x1C.. (flags/parameters).
- For each of three slots per record (loop of 4 with per-slot stream at +0x30/+0x3C/+0x48):
  - If the slot id is negative: handles special cases (-1 → pointer to `&DAT_00404040`; otherwise uses 3 bytes from K).
  - Else: copies 12 bytes (6 pairs) per slot from E (`DAT_003556b4`) into the 0x80-record (+0x30..+0x3B etc.).
  - Applies small fixups (e.g., cap 0x0F → 0x09; halve 0xFF → 0x80), and sets a flag at +0x70.
- Writes a byte 0x80 at +0x2E for each D-record.

Interpretation: This stage resolves compact rows (K, E) into expanded per-D-record parameter blocks. These
appear texture/state-related and do not alter vertex indices.

### FUN_0022c6e8

- Allocates a small scratch area (stack-like array at `DAT_70000000`).
- For each D-record (using the paired 0x78/0x80 records at `DAT_003556b0`/`DAT_003556ac`):
  - Loads the four u16s from 0x78 +{0x08,0x0A,0x0C,0x0E} (duplicates exist at 0x80 +{0x24,0x26,0x28,0x2A}).
  - For each of those 4 indices, copies 3 dwords from C-derived table (`DAT_0035569c`) into scratch.
  - Computes per-axis min/max across 3 components; writes AABB to 0x78 +0x18/+0x1C per axis.
  - Determines triangle vs quad: if the 3rd and 4th indices are equal, it’s a triangle; otherwise a quad.
  - Computes plane/normal twice via:
    - `FUN_0022caf8(record, tri_ptr)` which forms edge vectors from three points and cross-products them.
    - `FUN_0022cbd8(record, ptr_out, ptr_in)` which recomputes normalized plane components and an angle.
    - Ordering for quads (two triangles) is explicitly set to (3,0,1) then (1,2,3) via scratch indices 0x24..0x26.
  - Computes a per-record center at 0x80 +0x60 (average of vertices) and a radius at +0x74 using
    a distance function (`FUN_00216598`), i.e., a bounding sphere.

Evidence: Cross-products, min/max bounds, and distances strongly indicate the 3-tuples referenced by these
indices are used as 3D positions in this pipeline.

### FUN_00211230

- Iterates D-records again, prepares GPU packets. Key points:
  - Reads the same 4 u16 indices from the 0x80-record and emits that many vertices in order (S0,S1,S2[,S3]).
  - Sets primitive type bits (tri vs quad) and state flags based on record bytes.
  - For each vertex, packs 3 components (scaled by ~126.0f) and related per-vertex/color data into packets.
  - When a flag is set, emits an additional packet (0x6600C039) and writes u16 pairs likely tied to the per-slot
    data at +0x30/+0x3C/+0x48 (populated by FUN_0022c3d8).

Interpretation: This confirms the same index quadruplet is treated as an ordered vertex list for rendering.

---

## D-record details we can assert from code

Although the original on-disk D-stream is decoded through `FUN_0022b4e0`, the in-memory paired records are
populated in a fixed pattern:

- 0x78-record (base `DAT_003556b0`, stride 0x78):

  - +0x00: u32
  - +0x04: u32
  - +0x08/+0x0A/+0x0C/+0x0E: four u16 indices (the vertex index set)
  - +0x10: u16 selector; +0x12/+0x13: two bytes; +0x14: u8 copied from [A]+0x1A
  - +0x18..+0x23: per-axis mins/maxs written by FUN_0022c6e8

- 0x80-record (base `DAT_003556ac`, stride 0x80):
  - +0x00..+0x0C: 0x10 bytes copied from `DAT_003556a4 + k*0x10` (k read from D-stream)
  - +0x10: u32; +0x70: u32 (copied from 0x78-record +0)
  - +0x24/+0x26/+0x28/+0x2A: duplicate of the four u16 indices
  - +0x2C/+0x2D: two small bytes copied from D-stream
  - +0x30/+0x3C/+0x48: three 12-byte slots populated by FUN_0022c3d8 (from Section E or K)
  - +0x60..+0x68: center (computed in FUN_0022c6e8)
  - +0x6C: 1.0f constant; +0x74: radius (FUN_0022c6e8)
  - +0x78: u32 state set in FUN_0022c3d8

The “four u16 indices” are the only values used in position fetch, normal computation, bounds, and packet
emission. They are treated as indices into the C-derived float triplets table (`DAT_0035569c`).

---

## Indexing, faces, and the J-record vertex buffers

- J-record construction (in FUN_0022b5a8) builds a per-record contiguous buffer of 3-floats, derived from C
  starting at an index determined by the owning A-entry, and appends one extra triplet from that A-entry.
- D-record indices, however, are used directly to fetch triplets from the C-derived table, not from a J-local
  buffer in this code path. The linkage between a specific D-record and a specific J-record is indirect through
  shared A/B references and subsequent consumers.

What we can say with confidence:

- The four shorts in each D-record are vertex indices into the same 3-float domain as that used to build J buffers.
- Tri vs quad and split order are explicit in FUN_0022c6e8: triangle if S2==S3; otherwise quads split as
  (3,0,1) then (1,2,3).

Open questions (intentionally left unresolved here):

- Exact rule that binds a given D-record’s indices to a particular J-record’s local buffer. The J-buffers are built
  from contiguous ranges in C with an extra appended triplet; the renderer can use flags to choose sources.
- The meaning of index value 0 seen in some D streams (observed during tooling). It could be a sentinel or refer to
  a specific element (e.g., an appended triplet) depending on flags. The code shown here does not special-case 0.

---

## Current exporter status and caveats (tooling notes)

These notes are for offline tools and are not claims about the game code itself:

- Vertex export: J-record buffers are exported as the contiguous triplet list the code builds.
- Face export: Initial attempts to bind D indices to J-local indices by simple range containment often produce
  inconsistent meshes. This indicates that the runtime source for per-vertex fetch during rendering is more nuanced
  (flags/selection) than a fixed “J-local only” assumption.
- A follow-up attempt mapped D index 0 to the “extra appended” J vertex to accommodate mixed (0, in-range) tuples;
  this produced faces in some cases but also yielded repeated jagged shapes elsewhere, which suggests the binding is
  not globally correct.

Recommendation: Treat the four D indices as authoritative, but defer exporting faces bound to J-buffers until the
flag-controlled source selection in `FUN_00211230` is fully resolved. Continue tracing how `DAT_00355694/98/9c` and
`DAT_003556a4/a8` interact with the per-vertex fetch under different bits in the 0x78/0x80 records.

---

## Evidence map (where to look in code)

- Loader and section construction: `src/FUN_0022b5a8.c`
- Per-D-record parameter expansion: `src/FUN_0022c3d8.c`
- Bounds, normals, tri/quad split and ordering: `src/FUN_0022c6e8.c`, `src/FUN_0022caf8.c`, `src/FUN_0022cbd8.c`
- Render packet assembly and vertex emission order: `src/FUN_00211230.c`

---

## Next steps (analysis plan)

1. Trace the precise per-vertex source in `FUN_00211230` under all flag combinations (bits set in 0x1C/0x70/0x78
   of the 0x80-record), confirming when it samples from `DAT_0035569c` vs other tables.
2. Correlate D-records to A entries (and thus J indices) via the u16 selector path used to copy a 0x10-byte record
   into the 0x80 head; confirm whether that selector is the same field later read as 0x78-record +0x10.
3. Validate whether the “index 0” observed in offline data is ever special-cased at runtime; add probes to see if
   0 maps to a fixed location or is gated by a flag.
4. Once the binding is proven, update the exporter to emit faces referencing the correct local vertex domain; add a
   verification pass that recomputes normals and AABB and compares with 0x78-record outputs.

---

## Tooling / Scripts Report (current state)

This section inventories the Python scripts related to PSM2, their purpose, usage, and current limitations. All scripts
are code-driven (no heuristics unless explicitly noted) and live under `scripts/`.

### `psm2_parser.py`

Purpose:

- Parse a buffer containing a PSM2 payload starting at a given offset.
- Extract Sections A, C, D (minimal), J exactly as the loader does (magic, header offsets, counts, dword/short copying).
- Produce a JSON summary (printed to stdout) of parsed records including per-J-record vertex arrays.

Key outputs:

- `offs`: dictionary of header offsets consumed.
- `a_records`: list of raw 6-dword A entries plus convenience `short0` (start index), `short1` (count), `extra_xyz`.
- `c_records`: float triplets plus low packed metadata bits.
- `d_records` (minimal): quadruplets of u16 indices and a packed selector stub (currently treated as opaque).
- `j_records`: vertex lists derived from contiguous C slice + appended A triple.

Run examples:

```bash
python scripts/psm2_parser.py out/maps/map_0009.psm2 --pretty
python scripts/psm2_parser.py out/maps/map_0009.psm2 --offset 0x100 --pretty
```

Limitations:

- Does not fully decode non-exported sections (E,F,G,H,I,K) beyond what is needed for structural mapping.
- D-record parsing is minimal (only first four indices + a-index surrogate); remaining fields are left out until face binding is confirmed.
- Vertex lists in `j_records` are relative differences in code; current parser emits their absolute values as read (base-subtraction side effects not preserved yet for replay).

### `export_psm2_to_obj.py`

Purpose:

- Locate PSM2 magics in input files, parse each payload via `psm2_parser.py`, and export per-J-record vertex lists to OBJ.
- (Experimental) Emit faces by attempting to map D-record indices into each J-record's C slice.

Invocation:

```bash
python scripts/export_psm2_to_obj.py --src out/maps --dst out/obj_psm2 --limit 10 --verbose
```

Generated files:

- `map_XXXX_ofsOFFSET_recNNN.obj` for each J-record with vertices.
- OBJ header comments include A-index, C.start, count, and vertex total.

Face emission logic (current):

- Treat a D-record's four indices as global C indices; map any index equal to 0 to the appended A-triplet (local last vertex).
- Include a face only if all referenced indices can be mapped (triangle if S2==S3; else two split tris (3,0,1) and (1,2,3)).

Limitations / Known issues:

- Jagged / repetitive prisms indicate incorrect domain binding: runtime may fetch from multiple sources depending on flags that we have not reproduced. Faces should be considered provisional.
- Index value 0 mapping to appended triplet is an unverified assumption—added purely to surface faces for inspection.
- Does not output normals or material groups; no attempt to reconstruct AABB/radius for validation yet.

### (Related, outside PSM2) Other format exporters (PSC3, PSB4)

- PSC3 exporter (not documented here) uses draw descriptors directly for face correctness (verified against code path).
- PSB4 parser currently identifies triangle index groups but defers vertex export (no vertex buffer read in analyzed loader path).

### Suggested improvements (tooling backlog)

1. Add a verification pass comparing computed normals from exported faces to the cross products created in FUN_0022c6e8.
2. Emit a sidecar JSON per OBJ with counts of accepted/rejected faces and the distribution of unmapped indices.
3. Extend D-record parsing to include all u16/u32 fields, enabling a precise reconstruction of selector-based vertex source switching.
4. Provide a `--faces=off` flag to disable experimental face emission cleanly.
5. Implement a diagnostic script `psm2_face_diagnostics.py` to print per-D-record AABB, radius, and the raw 4 indices for quick cross-reference.

### Quick troubleshooting guide

- No OBJ written: Ensure the file contains the `PSM2` magic at the expected offset (little-endian 0x324D5350).
- Vertex count appears huge/small: Check A-record `short1` (count) inside the JSON; parser uses it directly to build the J slice.
- All faces look degenerate: Likely indices fall outside the chosen J slice or source selection logic differs; disable faces and collect diagnostics.

### Safety / Reversibility

- Scripts do not modify source binaries; all output goes under `out/obj_psm2/` or console JSON.
- Adjustments to face emission are isolated; deleting generated OBJs reverts analysis state.

---

## Summary

The PSM2 pipeline constructs multiple structured tables (A,C,D,J, etc.) and post-processes them to compute geometry
metrics (normals, bounds, radius) and packet payloads. The four u16s per D-record are conclusively treated as ordered
vertex indices for these computations. Exporting faces requires a still-unresolved mapping between runtime vertex
fetch and offline J-record slices; current tooling provides provisional output strictly for inspection and should not
be treated as definitive geometry reconstruction.

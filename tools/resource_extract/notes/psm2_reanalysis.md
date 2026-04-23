# PSM2 re-analysis — summary

Status: **PSM2 extraction working**. Implementation lives in
[tools/resource_extract/v2/psm2.py](../v2/psm2.py).

## Verified facts

### Helpers
- `FUN_0022b4e0` is `*dst = *(u32*)src; return src + 4` — read u32, advance.
- `FUN_0022b520` is `*dst = (u32)*(u16*)src; return src + 2` — read u16, advance.

### Section layout (header at offset 0; magic 'PSM2' = `0x324D5350`)

| Header | Section | Purpose                                                                        |
|--------|---------|--------------------------------------------------------------------------------|
| +0x04  | A       | Per-mesh records (0x20 stride). Used for J construction; not needed offline.   |
| +0x08  | C       | **Per-vertex positions**. Stride 16: `f32 x,y,z`, `u16 b_index`, `u8 style`, pad. Count = `s16` at +0. |
| +0x0C  | D       | **Primitives**. 32 bytes / record (16 × u16). Only `u16[0..3]` are C-indices. Count = `s16` at +0; **records start at +4 (not +2)** — the 2 bytes after the count are reserved/unused header. |
| +0x10  | K       | 3-byte rows feeding per-D material slots. Not needed for OBJ.                  |
| +0x14  | E       | Compact 0x10-byte rows (same role as K). Not needed for OBJ.                   |
| +0x18  | F       | 0x1000-short LUT + trailing list. Not relevant to mesh geometry.               |
| +0x1C  | J       | Per-mesh differential vertex buffers. Redundant with D's global C-indices.     |
| +0x2C  | G       | Grouped triangle index lists (collision?). Not the primary draw stream.        |
| +0x30  | B       | **Per-vertex normals**. Stride 12 on disk (3 × f32); widened to 16 in memory by zeroing a 4th dword. Count = u32 truncated to s16 at +0. |
| +0x34  | H       | 0x10-byte rows of 4 ints. Not relevant to mesh export.                         |
| +0x38  | I       | 0x20-byte rows of 2×2 ints. Not relevant to mesh export.                       |

### Why "Section B = normals"

- Renderer (`FUN_00211230` lines 240-260) writes per-vertex bytes as
  `round(component * 126.0)`. Round-trip only makes sense for `[-1, 1]`.
- Real values are unit-length and frequently axis-aligned `(0,1,0)` etc.
- The C→B map (`u16` at C-record +0x0C) maps each position to its normal.

The baseline's `--alt-b` / `--dual-sources` modes treated B as positions →
garbage. v2 omits those modes.

### D-record on-disk stride is 32 bytes (16 × u16)

| u16 idx | Use                                                                  |
|---------|----------------------------------------------------------------------|
| 0..3    | Four C-indices (vertices of the primitive)                           |
| 4..12   | Auxiliary fields fed into 0x80/0x78 in-memory records                |
| 13      | Index `k` into Section A 0x10-stride table (material/state slot)     |
| 14..15  | Consumed by loader's pointer advance; not directly used for geometry |

### Triangle vs quad and winding

From `FUN_0022c6e8` (AABB / face-normal pass), confirmed by `FUN_0022caf8`
(cross product `(p1 - p0) × (p2 - p0)`):

```
if (s2 == s3)  → triangle (s0, s1, s2)
else           → quad → tris (s3, s0, s1) and (s1, s2, s3)
```

## What changed vs the baseline

1. ✗ `--faces slice` required all four D indices inside one J-slice. D is
   global; that mode dropped almost everything.
2. ✗ `--alt-b` / `--dual-sources` swapped B in as positions. B is normals.
3. ✗ Stale `_global_stats.json` files from filtered runs gave the impression
   that even `--faces global` was broken. It wasn't.
4. ✗ v2's first cut had Section B stride wrong (16 instead of 12) → 138/165
   files failed silently with EOF on the very last record. Fixed.
5. ✗ v2 also started reading D records 2 bytes too early (base+2 instead of
   base+4). The 16-bit slot after the count is a reserved header, not the
   first record. The 2-byte misalignment caused fields from adjacent records
   to overlap, producing fan-shaped artifacts where many faces appeared to
   share a few low-index vertices (e.g. vertex 9 used by 283 faces). After
   fixing, top vertex usage dropped to ~11 faces, consistent with normal
   manifold connectivity.

## Verification (current run)

- All **165** PSM2 chunks across `out/maps/` extract without error.
- Normals unit-length (`avg|n|` = 1.000 sampled across multiple maps).
- Face counts scale with mesh complexity (map_0000: 3624 v / 4418 f;
  map_0001: 5661 v / 7896 f; map_0005: 5349 v / 8204 f).

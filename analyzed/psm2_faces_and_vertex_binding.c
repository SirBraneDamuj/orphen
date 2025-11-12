// PSM2: Faces and vertex-domain binding
// Source functions analyzed:
//   - FUN_0022b5a8 (psm2_loader_and_pipeline.c): section construction
//   - FUN_0022c6e8: builds per-record derived data and decides tri vs quad
//   - FUN_00211230: emits GPU packets; reveals vertex order and per-vertex fetch
//
// Goal
//   Document, from code only, how triangle faces are determined and how their vertex indices
//   bind to the per-record vertex domain (built in Section [J]). This enables safe OBJ export
//   without heuristics.
//
// Section recap (symbols are globals allocated by FUN_0022b5a8):
//   [A] DAT_003556d8: base of 0x20-byte records (count=DAT_003556d4)
//   [C] DAT_0035569c: base of 0x10-byte records (two dwords at +0,+4; dword at +0x0C used later)
//   [D] 0x78-byte records at DAT_003556b0; paired 0x80-byte records at DAT_003556ac
//       - Four u16 indices are duplicated in both records:
//         * 0x78-record: +0x08,+0x0A,+0x0C,+0x0E
//         * 0x80-record: +0x24,+0x26,+0x28,+0x2A
//       - A u16 “selector” at 0x78-record +0x10 encodes a link to an [A]-entry
//   [J] DAT_003556e0: 0x74-byte records; each owns a float buffer (ptr at +0x1C) constructed
//       from a contiguous slice of [C] plus one extra triplet sourced from the owning [A]-entry.
//       During build, the owning [A]-entry’s +0x1A is set to the J-record index.
//   [G] DAT_003556f4 (input) / DAT_003556f8 (derived): grouped triplet lists; not used as vertex
//       indices in the path below (these feed texture/attribute assembly, not face topology).
//
// 1) Triangle vs Quad (FUN_0022c6e8)
//   For each [D]-record (i-th instance; 0x78-record at b0+i*0x78, 0x80-record at ac+i*0x80):
//     - Load the four shorts S0..S3 from 0x78-record +{8,A,C,E} (also observed at 0x80-record +{24,26,28,2A}).
//     - If S2 == S3 → treat as triangle (3 vertices). Else → treat as quad (4 vertices), later split into two tris.
//     - Two triangle configurations are used for quads when computing plane data:
//         tri1 uses vertex order (3,0,1), tri2 uses (1,2,3).
//       This ordering shows up via piVar4[0x24..0x26] before calls to FUN_0022caf8/FUN_0022cbd8.
//
// 2) Vertex order and per-vertex fetch (FUN_00211230)
//   - Per-face submission reads the same four shorts in sequence from the 0x80-record at +0x24..+0x2A.
//     The loop emits either 3 or 4 vertices in that exact order.
//   - For each emitted vertex, it fetches a 3-dword tuple from [C] at index (short)*0x10: this is the
//     per-vertex attribute block used for positions (and possibly more), not a face index stream.
//   - Texture ST (or related attributes) are sourced separately; when the primitive is not type 2, it
//     emits a 0x6600C039 packet and copies u16 pairs from the 0x80-record’s +0x30/+0x3C/+0x48 regions,
//     which FUN_0022c3d8 populated from the [E]/[K] tables. These are not geometry indices.
//
// 3) Binding faces to a J-record (local vertex domain)
//   - Each [D]-record carries a u16 selector at +0x10 (in the 0x78-record) whose high bits select an
//     [A]-entry. Code reads: *(byte*)( ( (u16<<16) >> 11 ) + [A-base] + 0x1A ). At J-build time, the
//     owning [A]-entry’s +0x1A is set to the J-record index. Hence: D → A → J mapping is explicit.
//   - Each J-record’s vertex buffer is built from a contiguous slice of [C], starting at a base index
//     (obtained via [C]-record referenced by the owning [A]-entry) and spanning a known count. The
//     local vertex index for OBJ can be computed as: local = (C_index_from_D) - (C_base_for_this_J).
//     The final extra triplet appended from [A] is not referenced by the D-vertex shorts in this path.
//
// Practical export recipe (OBJ)
//   For each [D]-record i:
//     - Read S0..S3 from 0x80-record (DAT_003556ac + i*0x80 + {0x24,0x26,0x28,0x2A}).
//     - Read selector U from 0x78-record (DAT_003556b0 + i*0x78 + 0x10), derive A_index = (U<<16)>>11.
//     - Let J_index = *(u16*)(DAT_003556d8 + A_index*0x20 + 0x1A).
//     - Let C_base = first-index for this J (from the [C]-record referenced by the owning [A]-entry;
//       in FUN_0022b5a8 this is the pointer pfVar32 = (float*)(DAT_0035569c + A.first*2)).
//     - Emit faces using local indices:
//         if (S2 == S3): f (S0-C_base, S1-C_base, S2-C_base)
//         else:          f (S3-C_base, S0-C_base, S1-C_base) and f (S1-C_base, S2-C_base, S3-C_base)
//     - Vertices for this J-record come from the buffer constructed in [J] (relative floats), so OBJ
//       should reference that list by J_index and local.
//
// Notes
//   - Section [G] (grouped triplets) participates in attribute setup (UV/flags) via FUN_002256d0/…f0
//     and later per-face packet assembly, but not as a vertex-index stream in this path.
//   - Orientation: The (3,0,1) and (1,2,3) split is taken verbatim from the normal/plane setup in
//     FUN_0022c6e8 and is the safest triangle split to mirror in exports.
//   - The pipeline never reads a vertex index outside [C]; the extra [A] triplet appended to J-buffers
//     appears used for bounds/continuity, not for direct face assembly.
//
// Keep original FUN_* names in comments for traceability.

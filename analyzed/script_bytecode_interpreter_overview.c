// Overview: High‑level entry points and structure for the script bytecode interpreter system.
// This is NOT a rewrite of the 2K+ line decompiled function yet; it documents an initial
// segmentation strategy and concrete next investigative steps leveraging recently identified
// post‑pointer region structures (Pattern A, Pattern B, generic opcode stream, script IDs).
//
// Primary raw functions (original FUN_* names retained):
//   FUN_0030c8d0 : Large routine that (a) tokenizes dialogue text on '%'/0x25 markers, and
//                  (b) embeds an internal printf / numeric formatting sub‑engine. It appears
//                  to operate over the dialogue text region (header[0]..header[1]) but may be
//                  re‑used for later formatted outputs.
//   FUN_002f6e60 : Smaller formatter also parsing '%' sequences (subset of specifiers) — likely
//                  a lightweight variant used in debugging / UI overlays.
//   FUN_0025b9e8 : Pointer table entry accessor (already analyzed).
//   FUN_00267f90 : Script base resolver (already partially understood; returns relocated base).
//
// Newly observed byte sequences ("script IDs") coming from runtime HUD: decimal IDs printed as
//   4927, 4947, 4519, 1194, 1204
// Interpreted as 16‑bit little endian values present exactly once each in scr2.out at offsets:
//   0x4a28: 0x04AA (1194)
//   0x4a30: 0x04B4 (1204)
//   0xab78: 0x11A7 (4519)
//   0xb378: 0x133F (4927)
//   0xb438: 0x1343 (4947)
// (Offsets relative to file start; discovered via pattern grep around clusters shown in terminal dump).
// These offsets are beyond pointer table end (header[6] = 0x3930) placing them firmly in the
// post‑pointer structured region (Pattern A / Pattern B / general opcode stream).
//
// Local structural context (5–16 preceding/following bytes) shows each 16‑bit ID embedded in a
// sequence beginning with 0x04. Example (from 0x4a20 region):
//   00 9e 0c 01 1e 0b 04 aa 04 00 00 01 91 0c 00 0b
// Alignment:        ^ start of local record? ^ ID ^ probable u16/size/flags ...
// Tentative interpretation:
//   0x04 <ID low> <ID high> 0x04 0x00 0x00 0x01 ...
//   The duplicated 0x04 may delimit a small fixed structure; the trailing 0x000001 suggests a
//   little-endian 24‑bit or 32‑bit field with value 0x000001. More samples required for schema.
//
// Hypothesis: The 16‑bit IDs index (or are keys of) a sub‑procedure / sub‑process descriptor table
// that the runtime overlay labels as "SCR SUBPROC DISP" (displaying active script sub‑process).
// Each descriptor record likely contains: (a) ID, (b) entry pointer or script fragment pointer,
// (c) flags/state bytes, (d) maybe timing or resource references. These are likely consumed by a
// dispatcher loop separate from the plain dialogue text interpreter.
//
// Planned staged decomp / renaming approach:
// 1. Augment Python tooling to carve the post‑pointer region into distinct logical records:
//    - Already existing Pattern A (25 37 0e ...)
//    - Existing Pattern B (25 01 52 0e ... / 01 52 0e ...)
//    - New: generic records beginning with small opcode bytes (e.g., 0x04) followed by 16‑bit ID.
// 2. Map each discovered 16‑bit ID occurrence to nearest preceding structural delimiter (0x0b or 0x25
//    in prior patterns) to bracket a candidate record.
// 3. Cross‑script validation: run the new analyzer on scr3.out / scr4.out to see if IDs repeat,
//    shift, or correlate with content differences.
// 4. Static code search: after cataloging record start byte(s), grep decompiled sources for switch
//    statements over matching opcode values (e.g., case 4: / case 0x04:) near loops walking memory
//    ranges between pointer table end and footer base (header[6]..header[7]). This should narrow
//    potential interpreter functions.
// 5. Instrument & annotate first confirmed interpreter: replicate small excerpt into a dedicated
//    analyzed C file with renamed locals (maintaining original FUN_* name in a banner comment),
//    focusing initially on record fetch stride & termination logic (e.g., sentinel pattern or byte
//    count management) before assigning semantics to fields.
//
// Immediate next actionable artifact: see analyze_script_ids.py added alongside this file.
// It enumerates 16‑bit target IDs, finds occurrences in a given scr*.out, and emits JSON with a
// context window to accelerate manual schema inference.
//
// NOTE: We intentionally defer any renaming of involved global state until a consumer function is
// tied to these records (e.g., via loads using base + offset pattern matching file offsets above).
//
// Future refinement milestones:
//   - Build a generalized post_pointer_region parser producing a timeline / ordered list of mixed
//     Pattern A, Pattern B, and other record types with offset + raw field slices.
//   - Introduce preliminary enum definitions for opcodes once stable sets observed across scripts.
//   - Validate run‑time linkage by hooking the HUD draw routine that formats the decimal IDs and
//     locating its upstream caller providing the raw 16‑bit value (trace back to memory address).
//
// (End of overview initial draft.)
// This file intentionally contains only commentary at this stage; no compiled symbols added yet.

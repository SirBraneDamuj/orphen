# SCR File Reverse Engineering Summary

Status: Working draft (2025-08-10)

This document consolidates current knowledge about the PS2 `scr` (decompressed `scr*.out`) file format in Orphen: Scion of Sorcery, associated runtime loader behavior, identified structured regions, analysis tooling, and planned next steps.

---

## 1. High-Level Layout (Decompressed `scr?.out`)

Order as observed in multiple files (`scr2.out`, `scr3.out`, `scr4.out`):

1. Header (0x2C bytes = 11 × uint32 little-endian)
2. Dialogue text region (variable; interleaved control markers `%` aka 0x25)
3. Pointer table (relative offsets into file; terminates with a 0 entry)
4. Post-pointer-table structured data cluster:
   - Pattern A: `25 37 0e` records (duration / timing table) immediately after sentinel
   - Pattern B: `25 01 52 0e` then subsequent `01 52 0e` blocks (hierarchical blocks with `79 0e` subrecords)
   - Return to mixed opcode / record stream including known 12-byte `5A 0C` records
5. Footer / secondary pointer chain & additional control structures referenced by loader

### 1.1 Header Fields (indices 0–10)

Eleven dwords relocated in-memory by loader `FUN_00228e28` (documented in `analyzed/script_header_loader.c`). Current inferred roles:
| Index | Description (Inferred) |
|-------|------------------------|
| 0 | Start of dialogue text region |
| 1 | End of dialogue text / start of next segment |
| 2–4 | Offsets to other data blocks (descriptor + arrays) |
| 5 | Pointer table start (file offset) |
| 6 | Pointer table end (first byte after last 4-byte entry) |
| 7 | Footer / secondary relocation chain base |
| 8–10 | Three related array starts (triple-array structure) |

Relocation: Loader copies header to runtime buffer, then iterates + relocates nested pointer chains starting from header[7].

### 1.2 Pointer Table

Read via accessor `FUN_0025b9e8(index)` (documented in `analyzed/script_pointer_table_accessor.c`).
Characteristics:

- Dense ascending relative offsets until a sentinel `0x00000000` entry.
- Table region defined strictly by `[header[5], header[6])`; sentinel effectively terminates logical indices earlier than physical span.
- Data bytes following the sentinel are NOT pointer entries — they begin Pattern A.

### 1.3 Dialogue Region

Parsed by a large runtime function (`FUN_0030c8d0`) that scans for control introducer `0x25` (ASCII `%`). Control sequences embed opcodes (e.g., previously mapped dialogue VM opcodes). Pattern A reuses the same introducer style but is outside the main dialogue block (after pointer table) and thus likely parsed by a distinct phase.

---

## 2. Structured Post-Pointer Patterns

### 2.1 Pattern A (Timing / Duration Table Hypothesis)

Signature: `25 37 0e <ID> ... 0b <param_bytes> 0b`

Record anatomy (per analyzer):

```
25 37 0e  ID  [zero/reserved bytes...] 0b  PARAM_BYTES 0b
```

Observed IDs: `0x0E, 0x10–0x1E` (continuous span except 0x0F used internally as a param constant, not an ID).

Parameter forms:

- Short form: `0c 01 1e` (fixed 3 bytes) — occurs for some IDs (e.g., 0x0E, 0x18)
- Long form: `0e XX 00 00 00` (5 bytes) where `XX` ∈ {FF,C8,78,64,50,4B,4B,3C,32,1E,0F,05}

Interpretation:

- The `XX` set maps to human-friendly timing / speed / duration constants: 255, 200, 120, 100, 80, 75, 75, 60, 50, 30, 15, 5.
- Hypothesis: Pattern A initializes a lookup of per‑ID timing values (text speed, animation pacing, fade durations, or menu/UI behavior).
- Distinct short-form entries may encode composite triple parameters (0x0c,0x01,0x1e) versus single duration entries.

Tool: `analyzed/analyze_pattern_25370e.py` produces structured JSON (`offset`, `id`, raw fields, decoded parameter lengths).

### 2.2 Pattern B (Block / Subrecord Table)

Initial signature: `25 01 52 0e <u32 value>` followed by delimiters; subsequent blocks: `01 52 0e <u32 value>` (no leading 0x25).

Within a block:

- Optional subheader: `0b 0b 34 00 00 00 37 0e <index32> 0b 59 0b` (variant also with `44 00 00 00`).
- Repeated clusters: optional `25`, then `79 0e <value32> 0b` plus a sequence of parameter opcodes of the form `0e <valByte> 00 00 00 0b`.
- Observed constant subrecord value: `0x100` (may indicate fixed resource or channel).

Tool: `analyzed/analyze_pattern_b_2501520e.py` enumerates blocks with extracted `block_value`, optional `subheader_index`, and per-subrecord params.

### 2.3 Return to General Opcode Stream

After Pattern B, stream resumes mixed control including 12-byte `5A 0C` records (previously identified structured records) and other opcodes (`77 0e`, `11 c8 00 1e`, etc.).

---

## 3. Known Functions & Their Roles

| Function (original)         | Analyzed Doc                               | Inferred Role                                                                     |
| --------------------------- | ------------------------------------------ | --------------------------------------------------------------------------------- |
| FUN_00228e28                | `analyzed/script_header_loader.c`          | Header loader: relocates 11 dwords and chained pointers                           |
| FUN_0025b9e8                | `analyzed/script_pointer_table_accessor.c` | Returns absolute pointer table entry given index                                  |
| FUN_0030c8d0                | (not yet analyzed)                         | Large parser scanning for 0x25 control markers (dialogue/token segmentation)      |
| FUN_0025c548 / FUN_0025c8f8 | (pending)                                  | Getter/setter for state fields indexed by small IDs (overlaps Pattern A ID range) |

Pending detailed annotation for: Pattern A interpreter (likely near control parsing and state setters), Pattern B interpreter.

---

## 4. Tooling Inventory

| Script / File                              | Purpose                                                                                     |
| ------------------------------------------ | ------------------------------------------------------------------------------------------- |
| `analyzed/analyze_pre_dialogue_header.py`  | Parse 11-dword header; derive sizes & counts                                                |
| `analyzed/script_header_loader.c`          | Annotated reconstruction of loader (FUN_00228e28)                                           |
| `analyzed/script_pointer_table_accessor.c` | Annotated pointer accessor (FUN_0025b9e8)                                                   |
| `analyzed/analyze_pattern_25370e.py`       | Extract non-overlapping Pattern A records                                                   |
| `analyzed/analyze_pattern_b_2501520e.py`   | Parse heuristic Pattern B blocks and subrecords                                             |
| `analyzed/*` (numerous)                    | Prior reverse-engineered systems (menu, text width, audio, etc.) supporting cross-reference |
| `globals.json`                             | Export of global variables (addresses, xrefs)                                               |
| `strings.json`                             | Export of string literals with xrefs                                                        |

Utilities _not authoritative_ (scripts/ folder) are treated as exploratory only.

---

## 5. Cross-Structure Relationships (Hypotheses)

| Structure                   | Links To                                                              | Rationale                                                          |
| --------------------------- | --------------------------------------------------------------------- | ------------------------------------------------------------------ |
| Pattern A ID set            | State fields accessed via FUN_0025c548 / FUN_0025c8f8 cases 0x0e–0x1e | Matching ID span & semantic timing normalization                   |
| Pattern B subheader indices | Possibly indices into secondary arrays (header[8–10])                 | Numeric pattern & occasional absence suggest optional typed blocks |
| `79 0e` subrecords          | Resource or channel table (constant 0x100)                            | Repeated identical base value across blocks                        |
| `5A 0C` 12-byte records     | Script action instructions                                            | Previously documented pattern; reappears after Pattern B           |

---

## 6. Outstanding Unknowns

| Topic                                                   | Open Question                                                             |
| ------------------------------------------------------- | ------------------------------------------------------------------------- |
| Pattern A semantics                                     | Exact meaning of duration constants; mapping to UI / animation categories |
| Pattern B `34` vs `44` tag                              | Size specifier vs type code?                                              |
| Pattern B `block_value` sequence gaps (e.g., missing 7) | Intentional omission vs file-specific absence                             |
| Parameter opcodes `0e <val>` inside Pattern B           | Mode flags vs weighting factors?                                          |
| Integration point                                       | Which function decodes Pattern A into runtime fields?                     |
| Sentinel logic                                          | Confirm runtime treats first 0 entry as hard end of pointer table         |

---

## 7. Proposed Next Steps

Priority (near-term):

1. Annotate FUN_0030c8d0 (or earlier helper it calls) focusing on 0x25-prefixed control parsing to see if it branches into Pattern A regions post-pointer-table.
2. Locate a loop that reads bytes `25 37 0e` and writes via indexed setter (likely calling FUN_0025c8f8 with case IDs 0x0e–0x1e).
3. Add consolidated analyzer script to: (a) auto-detect pointer table end, (b) extract Pattern A, Pattern B, (c) list 5A0C records, (d) output unified JSON for cross-file diff.
4. Run analyzer on `scr3.out` / `scr4.out` to verify stability of ID→constant mapping across scripts.
5. Cross-reference Pattern A ID count with number of state fields using float normalization constants (cases setting float\*(1/factor)).
6. Diff Pattern B blocks across scripts to see which indices vary (identify dynamic vs static entries).
7. Instrument runtime fields: correlate writes to memory offsets touched by FUN_0025c8f8 during script load vs during gameplay updates.

Secondary:

- Document `5A0C` record schema formally (field names & meaning) in separate analyzed file.
- Identify any relocation that touches Pattern A / B regions after initial load (double-initialization? caching?).
- Investigate whether `79 0e` value (0x100) changes in other scripts; if not, treat as constant sentinel.

Stretch:

- Build a visualizer: timeline chart of durations (Pattern A) vs triggered actions referencing those IDs.
- Attempt reconstruction to editable JSON → regenerate binary Pattern A / B blocks (for modding experiments).

---

## 8. Validation Hooks / Sanity Checks

Suggested simple checks to maintain correctness as tooling evolves:

- Assert pointer table scan stops at first zero entry; warn if non-zero entries appear after.
- Assert all Pattern A records obey delimiter format `<...> 0b <params> 0b` and no overlapping offsets.
- Flag unexpected bytes between Pattern A tail and first Pattern B header (`25 01 52 0e`).
- For Pattern B, ensure every parsed subrecord’s param opcode sequence matches `0e <val> 00 00 00 0b` pattern.
- Warn if any `5A 0C` record overlaps previously parsed Pattern B region.

---

## 9. Naming Conventions & Analysis Discipline

- Raw decompiled files under `src/` remain untouched (per project procedure).
- Analyzed, renamed, and documented C reconstructions are added under `analyzed/` with original `FUN_*` signature preserved in comments.
- Global variable renames deferred until confidently identified via multi-site usage.
- Keep original FUN/DAT names in comments to facilitate cross-reference with Ghidra exports & globals.json.

---

## 10. Quick Reference (Byte Signatures)

```
Header size:         2C 00 00 00 (fixed length; not stored explicitly)
Pointer sentinel:    00 00 00 00 (first zero dword in pointer table)
Pattern A record:    25 37 0e ID .... 0b PARAMS 0b
Pattern B first blk: 25 01 52 0e <val32> 0b ...
Pattern B blk:       01 52 0e <val32> 0b ...
Subheader (variant): 0b 0b 34 00 00 00 37 0e <index32> 0b 59 0b
Alt subheader tag:   0b 0b 44 00 00 00 ... (same pattern)
Subrecord start:     [25] 79 0e <val32> 0b
Param opcode:        0e <byte> 00 00 00 0b
Action record:       5a 0c <10 more bytes> (12-byte structure)
```

---

## 11. Open Questions Tracker (To Fill In)

Add rows here as new uncertainties arise:
| ID | Question | Hypothesis | Evidence Needed |
|----|----------|-----------|-----------------|
| 01 | Pattern A short-form triple meaning | (base, multiplier, duration) | Trace consumer arithmetic |
| 02 | Pattern B tag 0x34 vs 0x44 | Record type vs size | Compare across scripts |
| 03 | Meaning of param bytes 4 vs 3 vs 1 sequences | Mode/state toggles | Observe runtime branches |

---

## 12. Next Action (Immediate)

Annotate `FUN_0030c8d0` focusing on 0x25 processing path; extract sub-dispatch for `0x37` and map read pattern to Pattern A grammar.

---

End of summary.


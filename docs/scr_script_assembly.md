# Orphen PS2 SCR Script Assembly — Current Understanding

This document summarizes the working model of the game’s script “assembly” as reconstructed from decompiled code and empirical scans. It is meant to orient further reverse-engineering and disassembly/emulation efforts.

## High-level model

The game uses two cooperating interpreters over byte streams embedded in the SCR files:

- Structural interpreter (FUN_0025bc68)

  - Program counter: `pbGpffffbd60` (byte pointer into the script region)
  - Recognizes block open/close, a small set of low opcodes (< 0x0b), and routes higher opcodes (>= 0x32) via a shared dispatch table.
  - Key opcodes:
    - 0x32 — BLOCK_OPEN: pushes a continuation pointer of `(pb + 5)` and immediately executes the next four bytes as a “prologue” (see below), then calls `FUN_0025c220` to advance the main VM.
    - 0x04 — BLOCK_END: pops the saved continuation pointer and resumes at the original `(start + 5)`, skipping the already-consumed prologue.
    - 0xff — extended structural opcodes (dispatch via `PTR_LAB_0031e538`).
    - < 0x0b — low structural opcodes (dispatch via `PTR_LAB_0031e1f8`), including immediate loaders 0x0c..0x11 and tuple loaders 0x30/0x31 (see FUN_0025bf70 cases).

- Main VM interpreter (FUN_0025c258)
  - Program counter: `DAT_00355cd0` (int\* into a separate code stream).
  - Implements arithmetic/logic/compare (0x12..0x24) inline and dispatches high/ext opcodes via tables shared with the structural layer.
  - `FUN_0025c220` (called by structural on 0x32) performs a self-relative jump: `DAT_00355cd0 += *DAT_00355cd0`.

Shared dispatch tables (from decompilation):

- `PTR_LAB_0031e1f8` — low structural opcode table (index = opcode)
- `PTR_LAB_0031e228` — high opcode table (index = opcode - 0x32)
- `PTR_LAB_0031e538` — extended (0xff prefix) table (index = next byte)

## The “prologue” after 0x32

- The 4 bytes following 0x32 are executed immediately as four ordinary structural opcodes (a mini prologue program), not a packed numeric header.
- After the block’s body runs and hits 0x04, control resumes at `(start + 5)`, ensuring those four bytes are not re-executed.
- Practically, the first prologue opcode is a good candidate for a “block type” discriminator in listings.

## Subproc IDs (debug-related pattern)

- Observed in the game’s debug context as: `0x0B 0x04 <ID16 little-endian>`.
- We treat this as a single SUBPROC marker in listings. Known IDs (decimal): 4927, 4947, 4519, 1194, 1204.
- Important: These SUBPROC IDs are distinct from the two bytes that may appear after a 0x04 BLOCK_END in our block dataset. The latter were just raw trailing bytes at close sites and not the debug “subproc” IDs.

Scripts/tools:

- `analyzed/scan_prefixed_ids.py` — finds `0x0B 0x04 <ID16>` occurrences and emits context; can mark pre/post pointer table using the 0x2C-byte file header.

## Structural block extraction and disassembly

- Canonical block set: `blocks_scr2_first_v2.json` (first 0x04 heuristic; non-overlapping, sane nesting).
- Prologues: `analyzed/extract_block_prologues.py` emits per-block prologue bytes and basic stats.
- Disassembly: `analyzed/disassemble_structural.py` walks bytes starting at each block start and prints a readable listing with:
  - `BLOCK_OPEN` at 0x32 with pushed continuation (`start + 5`).
  - Four prologue opcodes printed inline (with SUBPROC detection if present).
  - Selected immediate decoding for low opcodes 0x0c..0x11; tuple note for 0x30/0x31.
  - `BLOCK_END` at 0x04; optional display of two following bytes.
  - Options to compress long 0x00 runs as `PAD_00 xN` and highlight specific SUBPROC IDs.

Outputs/artifacts:

- `scr2_structural_disasm.txt` — full structural listing for scr2 using the canonical blocks, zero-compression enabled, and provided SUBPROC highlight set.

## File/header and pointer tables

- SCR files begin with an 0x2C-byte header of 11 little-endian u32s. We currently use indices 5 and 6 as pointer table start/end (per earlier notes) to mark pre/post regions in scans.
- We have not yet reproduced full relocation/loader behavior; current analysis treats script addresses as file offsets for structural decoding.

## What we are not doing yet (and why)

- Full emulation of the main VM: requires trusted entrypoints and/or loader relocation to ensure `DAT_00355cd0` points at valid self-relative cells before `FUN_0025c220`.
- Semantic decoding for most handler functions: we print opcodes/mnemonics and decode immediates where the decomp spells it out. Full naming follows handler-by-handler analysis.
- Cross-interpreter data flow: beyond recognizing the `VM_ADVANCE (FUN_0025c220)` call site, we are not following the VM stream within the structural disassembly.

## Practical guidance

- Treat the 4 bytes after 0x32 as code, not a header.
- Use the first prologue opcode to group block families.
- Use `scan_prefixed_ids.py` to find SUBPROC markers of interest and inspect their local structural context.
- Prefer `PAD_00` compression in listings to avoid confusing padding with logic.

## Next steps

- Add mnemonic tables for low/high/ext opcodes and begin replacing placeholders with descriptive names as handler behavior is documented.
- Provide a “disassemble around address” mode that starts from SUBPROC hits to quickly correlate debug IDs with surrounding structure.
- Prototype a minimal loader that seeds `DAT_00355cd0` and demonstrates `FUN_0025c220` landing on valid VM opcodes, then incrementally implement a VM-side disassembler.
- Document individual high-frequency prologue opcodes by mapping them to their dispatch targets and annotating their side effects.

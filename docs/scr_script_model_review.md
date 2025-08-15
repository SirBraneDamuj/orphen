# SCR script model — source-validated review

This note rechecks the “two-interpreter + block prologue” model strictly against the decompiled sources in `src/` and marks what’s confirmed, what needs correction, and what remains hypothesis.

## Confirmed from src

- Two cooperating interpreters exist:
  - Structural interpreter: `FUN_0025bc68` (see `src/FUN_0025bc68.c`). It drives a byte-wise PC `pbGpffffbd60` and dispatches:
    - Low structural opcodes: `< 0x0b` (except `0x04` handled specially) via `PTR_LAB_0031e1f8`.
    - Extended structural opcodes: `0xff <next>` via `PTR_LAB_0031e538`.
    - High structural opcodes: `>= 0x32` via `PTR_LAB_0031e228`.
    - Block open `0x32`: pushes continuation `(pb + 5)`, sets `pb = pb + 1`, then calls `FUN_0025c220`.
    - Block end `0x04`: pops to the saved continuation; if this closes the top-most frame, the function returns.
  - Main VM interpreter: `FUN_0025c258` (see `src/FUN_0025c258.c`). It uses `DAT_00355cd0` (int\*) as PC. It:
    - Dispatches high/ext VM ops (>= 0x32, 0xff) via `PTR_LAB_0031e228` / `PTR_LAB_0031e538`.
    - Implements inline ALU/logic ops for `0x12..0x24` and returns a value for `0x0b`.
    - Pulls immediates for certain ops from the structural byte stream via `FUN_0025bf70`.
- `FUN_0025c220` does a self-relative jump: `DAT_00355cd0 = DAT_00355cd0 + *DAT_00355cd0` (see `src/FUN_0025c220.c`).
- Structural-to-VM immediates are decoded in `FUN_0025bf70` using `pbGpffffbd60` for opcodes `0x0c..0x11` and tuple forms `0x30/0x31`.

## Corrections/nuances

- Order at `BLOCK_OPEN (0x32)`: The structural interpreter calls `FUN_0025c220` immediately after setting `pb` to `(start + 1)` and pushing continuation `(start + 5)`. The bytes after `0x32` are then consumed by the VM (via `FUN_0025bf70`) during that step; they are not executed as structural opcodes.
- The continuation set to `(start + 5)` implies those four payload bytes following `0x32` are intended to be consumed before the block resumes at `(start + 5)` (typical immediate form uses 1-byte tag + 4-byte payload).
- Structural-opcode space: the structural loop only treats `< 0x0b`, `0x32`, and `0xff` specially. Values `0x0c..0x11` and `0x30/0x31` are immediate encodings consumed when the VM requests them; they are not dispatched via `PTR_LAB_0031e1f8`.

## SUBPROC in practice (bridging evidence)

- Runtime/UI evidence exists: format strings at 0x0034ca60 ("Subproc:%3d [%5d]\n") and 0x0034ca78 ("%02d:%d(%X)") are used in FUN_0025b778 to display active scene object index and an associated value read from `scene_object_ptr + -4` and scene work data. A menu toggle string "ON :SCR SUBPROC DISP" also exists. This confirms a real “subproc” concept in engine UI/debug, independent of wire format.
- Wire-format marker: pattern `0x0B 0x04 <ID16>` appears in files, and some payloads after 0x32 look like this when treated as a 4-byte prologue. While not named in code, we can surface these as plausible SUBPROC tags in disassembly output for correlation.
- Disassembler policy: annotate 0x32 prologue payloads; if 4 bytes match `0x0B 0x04 <ID16>`, emit `SUBPROC_ID16=<id>`. This keeps it clearly labeled as derived from data, not asserted semantics.
- File header indices and exact pointer table semantics: the interpreters operate on already-resolved pointers. The loader/pointer-table mapping logic isn’t fully established in the decompilation we reviewed here.

## Practical disassembler guidance (provable)

- Structural layer:
  - Start at a known block offset. On `0x32`, push continuation `(pc + 5)`, set `pc = pc + 1`, emit a `VM_ADVANCE` marker, record the next four bytes as VM prologue payload, then continue from `(pc_start + 5)`. On `0x04`, pop continuation or, if at top-most frame, stop.
  - Dispatch annotations can be limited to: LOW `< 0x0b` (except `0x04`), EXT `0xff <byte>`, HIGH `>= 0x32`.
- VM layer:
  - Without emulating the VM, annotate that a VM step occurs at block open and that it will consume immediates from the structural stream per `FUN_0025bf70`.
  - If/when you emulate or disassemble the VM stream, use `FUN_0025bf70` as the ground truth for immediate sizes and formats.

In short: the high-level model is largely sound; correct the `0x32` ordering and the nature of the “prologue” (VM immediate bytes, not four structural opcodes). Treat SUBPROC as a real engine/debug concept; annotate likely on-disk subproc tags (`0x0B 0x04 <ID16>`) in disassembly while keeping their exact semantics provisional until tied to handlers.

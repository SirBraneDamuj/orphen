# Bytecode Opcode Analysis Summary

This document summarizes the opcode work completed during this analysis pass, including the VM model, analyzed opcodes, noteworthy globals/strings, and the procedures we follow when naming and documenting handlers.

## VM model and parsing rules

- Two-stream model:
  - Main instruction stream (`DAT_00355cd0`) holds opcodes and expression operators. Expressions terminate with byte `0x0B` (handled inside `FUN_0025c258`).
  - Param/atom stream (`pbGpffffbd60`) supplies literal tokens loaded by `FUN_0025bf70` (e.g., `0x0C` imm8, `0x0E` imm32). The evaluator advances both streams as needed.
- Dispatch tables:
  - Standard opcodes 0x32–0xF5 via `PTR_LAB_0031e228`.
  - Extended opcodes via `0xFF` prefix: value = `0x100 + nextByte`, dispatched through `PTR_LAB_0031e538`.
- Low-op range 0x00–0x31 handled inline at the end of the interpreter with small helpers (e.g., `0x03` advance).

## Analyzed opcodes (handlers and behavior)

- 0x36 / 0x38 — `script_read_flag_or_work_memory` (orig `FUN_0025d768`)

  - Reads one expression. If `DAT_00355cd8 == 0x36`: returns u32 from work array at `DAT_00355060[index]` (index ≤ 0x7F; else logs "script work over").
  - Else (used as 0x38): returns the 8-bit "bucket" at `DAT_00342b70[index >> 3]`. Validates `(index ≤ 0x47F8) && ((index & 7) == 0)`; else logs "scenario flag work error".
  - Files: `analyzed/script_read_flag_or_work_memory.c`, `analyzed/ops/0x36_*.c`, `analyzed/ops/0x38_*.c`.

- 0x51 — `set_pw_all_dispatch` (orig `FUN_0025eb48`)

  - Reads a mode byte from the param stream; iterates a table, allocates and configures entries, seeds per-slot fields (+0x4C/+0x98/+0xCC), and builds param blocks for non-mode-3.
  - On invalid id in mode 3, logs "tbox param error [set_pw_all]".
  - File: `analyzed/ops/0x51_set_pw_all_dispatch.c`.

- 0x58 — `select_pw_slot_by_index` (orig `FUN_0025f0d8`)

  - Reads one expression as `idx`; if `idx < 0x100`, sets `DAT_00355044 = &DAT_0058beb0 + idx*0xEC`.
  - File: `analyzed/ops/0x58_select_pw_slot_by_index.c`.

- 0x59 — `get_pw_slot_index` (orig `FUN_0025f120`)

  - Returns the current pool index from `DAT_00355044`, or `0x100` if none.
  - File: `analyzed/ops/0x59_get_pw_slot_index.c`.

- 0x5A — `select_pw_by_index` (orig `FUN_0025f150`)

  - Reads one expression; scans pool/status arrays for an active slot whose `slot[+0x4C] == target`; sets `DAT_00355044`; returns 1/0.
  - File: `analyzed/ops/0x5A_select_pw_by_index.c`.

- 0x7F / 0x80 — `submit_param_from_model_axis` (orig `FUN_00260880`)

  - Alias pair; reads a model index (expr) and an axis (imm), extracts a 3-component field, scales, and submits via `FUN_0030bd20`.
  - Notes recorded in dispatch table.

- 0x91 — `param_ramp_current_toward_target` (orig `FUN_002611b8`)

  - Steps `DAT_00571de0[idx].current` toward target by `step * (DAT_003555bc / 32)`.
  - Notes recorded in dispatch table; file present under `analyzed/ops/`.

- 0x92 — `audio_submit_current_param` (orig `FUN_00261258`)

  - Submits `DAT_00571de0[idx].current * DAT_00352c30` via `FUN_0030bd20`.
  - Notes recorded in dispatch table.

- 0x96 — `set_global_rgb_color` (orig `FUN_002618c0`)

  - Reads three evaluator values, packs low bytes into `uGpffffb6fc` (0xRRGGBB).
  - File: `analyzed/ops/0x96_set_global_rgb_color.c`.

- 0xBE — `call_function_table_entry` (orig `FUN_00263ee0`)

  - Dispatches `PTR_FUN_0031e730[index](arg)`; name and notes recorded in dispatch table.

- 0x10D (extended) — `submit_scaled_param_block` (orig `FUN_002629c0`)

  - Parses 9 args, scales the first four by `fGpffff8d0c`, calls `FUN_0021e088`.
  - File: `analyzed/ops/0x10D_submit_scaled_param_block.c`.

- Low-op 0x03 — `vm_advance` (shim to `FUN_0025c220`)
  - Advances the main stream by a self-relative amount; no param-stream consumption.
  - File: `analyzed/low_ops/0x03_vm_advance.c`.

## Noteworthy globals and helpers

- `DAT_00355cd8` — current opcode; used by shared handlers (e.g., 0x36/0x38) to branch mode.
- `DAT_00355060` — base pointer to work memory array (u32 elements).
- `DAT_00342b70` — global scenario flag bitmap (~0x900 bytes).
- Pool mechanics: `DAT_0058beb0` (base), stride `0xEC`, current selection `DAT_00355044`; alternates: `DAT_0058d120`, `DAT_005a96ba`.
- Evaluator: `FUN_0025c258` (coordinates both streams, stops on 0x0B). Atom fetch: `FUN_0025bf70`.
- Utility: `FUN_0026bfc0` (debug printf/formatter), `FUN_00265e28` (alloc), `FUN_00266240` (spawn/configure), `FUN_00216690` (float→fixed).
- Strings: 0x0034cdf0 "script work over", 0x0034ce08 "scenario flag work error", and the text-box error for 0x51.

## Procedures and conventions

- Do not edit raw decompiled files under `src/` for naming/cleanup.
- For analyzed handlers:
  - Create human-authored versions under `analyzed/` (or `analyzed/ops/`), keeping the original signature/name in comments.
  - Rename locals and known globals for clarity; leave unknown callees as their `FUN_*` names (extern).
  - Document byte consumption, side effects (global writes), and PS2/EE nuances.
  - Reference `globals.json` and `strings.json` for addresses, cross-refs, and string meanings.
- Keep `opcode_dispatch_tables.md` updated with semantic names and file references. Use alias wrappers (`analyzed/ops/0xNN_*.c`) when multiple opcodes share the same underlying function.

## Analysis tips used here

- Mark expression boundaries: count until the first `0x0B` on the main stream; param tokens come from the atom stream independently.
- When tracing pool ops (0x51/0x58/0x59/0x5A), account for base/stride math and how +0x4C is used as a selection tag.
- Use error strings to infer semantics and validate bounds.
- Validate with quick runs of focused scripts/tools to confirm byte counts and stream advances when needed.

## Next candidates

- Fill out the 0x56/0x57 cluster near the PW system.
- Confirm 0x64/0x65 semantics and refine names.
- Extended 0x129–0x132 cluster appears to be contiguous parameter-block builders.

# Dialogue/Text Control Opcode Dispatch Table (PTR_FUN_0031c640)

Source pointer table address: `PTR_FUN_0031c640` (referenced in `FUN_00237de8` / `dialogue_text_advance_tick`).

Indexing rule (current working hypothesis):

- Control bytes are values `< 0x1F` in the dialogue stream.
- For each control byte `op`, the engine indexes into this table as `table[op]` (Ghidra showed 0x40 spacing arithmetic but resulting pointer list is contiguous 4-byte pointers here). We treat the first entry as opcode 0x00, second as 0x01, etc.
- Table presently enumerated up to entry 0x1E (31 entries) before padding / unrelated data.

Opcode entries (hex):

- 00: FUN_00239178 — Load next text parameter/palette block (up to 8 entries) else set state bit 0x2000 (see text_op_00_load_palette_entry_or_advance)
- 01: FUN_002391d0 — Spawn dialogue overlay/banner slot (alloc special slot type 0x42A with fixed geometry & color; gated by flags 0x509/0x50A) (see text_op_01_spawn_banner_or_overlay)
- 02: LAB_00239328 — Terminate current dialogue stream (writes 0 to gp-0x5140, calls FUN_00237b38(0)) (implemented in text_op_02_terminate_stream)
- 03: FUN_002391d0 — Alias of 0x01 (no differing parameters observed)
- 04: FUN_002391d0 — Alias of 0x01 (reserved/unused variant)
- 05: FUN_002391d0 — Alias of 0x01 (reserved/unused variant)
- 06: LAB_00239338 — TODO
- 07: FUN_00239368 — Advance active glyph/banner timers then increment script cursor (calls FUN_00238f98; +1 to DAT_00354e30) (see text_op_07_advance_glyph_timers_and_cursor)
- 08: LAB_00239390 — TODO
- 09: LAB_00239c60 — TODO
- 0A: LAB_002393e0 — TODO
- 0B: LAB_00239408 — TODO
- 0C: LAB_00239428 — Set scaled parameter (consume 2 bytes; store (second<<5) to gp-0x4304) (see text_op_0C_set_shifted_parameter)
- 0D: LAB_00239450 — TODO
- 0E: LAB_00239478 — TODO
- 0F: LAB_002394f8 — TODO
- 10: LAB_00239548 — TODO
- 11: FUN_002395c0 — TODO
- 12: LAB_00239750 — TODO
- 13: FUN_00239760 — TODO
- 14: FUN_002397f0 — TODO
- 15: FUN_00239848 — TODO
- 16: LAB_00239990 — Trigger voice/audio load (parse 7-byte param block: channel, wait flag, 32-bit id -> FUN_00206ae0) (see text_op_16_trigger_voice_or_audio_playback)
- 17: LAB_00239a00 — Wait/poll for async audio/resource load; advance cursor by 1 only when FUN_00206c28 returns 1 (see text_op_17_wait_for_audio_load_step)
- 18: LAB_00239a30 — Conditional control byte set (consume 1 or 2 bytes; if first==0x19 store next to gp-0x4999) (see text_op_18_set_control_byte_conditional)
- 19: LAB_00239a30 — Alias of 0x18 (same conditional control byte behavior)
- 1A: LAB_00239a70 — Wait-until-clear on audio/system flag (advance cursor +1 only when FUN_00206a90 returns 0) (see text_op_1A_wait_on_audio_system_flag)
- 1B: LAB_00239aa0 — Read 3 payload bytes; if first==0x1B set flag id formed from next two (LE 16-bit) via FUN_002663a0; always advance cursor +3 (see text_op_1B_set_flag_from_two_byte_id_if_prefixed)
- 1C: LAB_00239aa0 — Alias of 1B (payload first byte likely sub-op selector; when 0x1C path causes no flag set)
- 1D: FUN_00239b00 — TODO
- 1E: LAB_00239c78 — TODO (last populated entry)

## Conventions for Analysis

- Create one analyzed C file per opcode under `analyzed/text_ops/` named `text_op_XX_<short_purpose>.c` keeping the original FUN*/LAB* reference in a top comment.
- For LAB* entries (no standalone function symbol), replicate the decompiled block into a wrapper function `text_op_xx*\*` and annotate control flow.
- Document side effects (globals touched, glyph slot modifications, state machine transitions, flag changes).
- Avoid speculative naming beyond observed behavior; refine iteratively.

## Open Questions

- Which ops manipulate line breaks, page breaks, color changes, speed modifiers?
- Are any ops conditional on flags 0x509 / 0x50A (render enable) directly?
- Do any ops change `cGpffffaec4` (layer/tag) cycling or timing accumulators `iGpffffbcdc / iGpffffbcd8`?
- Are ops 18/19 and 1B/1C simple aliases or versioned behavior (parameterized via preceding data)?

## Next Steps

1. Extract raw decompiled code for FUN_00239178 and FUN_002391d0; start with opcodes 00 and 01 (shared core?) to identify baseline control semantics.
2. Triage which ops write to glyph slot fields (+0x34 countdown, +0x3A active flag) versus those that mutate global positioning (uGpffffbcc8 / uGpffffbccc, etc.).
3. Flag any ops that directly clear or skip rendering enabling flags; these are prime candidates for regional disabling.

(Extend the table with a Description column as behaviors become known.)

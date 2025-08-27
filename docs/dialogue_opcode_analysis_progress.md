# Dialogue Opcode Analysis Progress Summary

This document summarizes the current state of reverse engineering work on the dialogue/text control opcode table (`PTR_FUN_0031c640`) and related subsystems, based on recent analysis sessions.

## Scope & Goals

Primary focus:

1. Systematically analyze control opcodes (< 0x1F) driving dialogue rendering, audio sync, and state flag mutations.
2. Replace anonymous `FUN_*` / `LAB_*` handlers with descriptive analyzed wrappers under `analyzed/text_ops/` without modifying raw decompiled `src/` output.
3. Track cursor (`gp-0x5140`) behavior, parameter consumption, and side effects (flag sets, slot allocation, audio triggers).
4. Identify shared helper functions and global state patterns (flag bitfields, overlay slot arrays, pacing counters).

## Global Elements Understood So Far

- `gp-0x5140` (referenced via placeholder symbol `GP_NEG_0x5140` in analyzed files) acts as a dialogue/control script cursor pointer. Many opcodes load it, read parameter bytes, then increment.
- Flag bitfield setter `FUN_002663a0(id)` sets a bit: `flags[(id >> 3)] |= (1 << (id & 7))`. Used by opcode 0x1B (conditional) and various non-text systems.
- Audio / voice request & wait pair:
  - Opcode 0x16 calls `FUN_00206ae0` (audio/voice load/play request) parsing a 7‑byte block (channel, wait flag, 32‑bit id).
  - Opcode 0x17 polls `FUN_00206c28` (returns 1 when idle/complete) and advances only when ready.
  - Opcode 0x1A polls `FUN_00206a90` (returns status byte `DAT_00356788`) and advances only when that value becomes 0 (complementary wait condition vs 0x17).
- Overlay / banner slot allocation (opcode 0x01 + aliases 0x03–0x05) gated by flags 0x509 / 0x50A.
- Glyph / overlay timers advanced via helper `FUN_00238f98` (invoked directly by opcode 0x07). It decrements active slot countdowns and adjusts a positional accumulator.
- Parameter/palette block loader (opcode 0x00) cycles up to 8 entries into `DAT_00354e30`, then sets state bit 0x2000 when exhausted.
- Control byte conditional setter (opcodes 0x18 / 0x19) optionally stores a payload byte to `gp-0x4999` depending on sentinel first byte (0x19 alias path).
- Shifted parameter setter (opcode 0x0C) consumes 2 bytes; stores `(second_byte << 5)` into `gp-0x4304` (exact semantic still pending).
- Flag-setting conditional (opcode 0x1B / 0x1C alias) consumes three bytes; sets bit only when first payload byte == 0x1B.

## Opcode Table Status

Implemented / documented analyzed handlers (files under `analyzed/text_ops/`):

- 0x00: `text_op_00_load_palette_entry_or_advance.c`
- 0x01 / 03 / 04 / 05: `text_op_01_spawn_banner_or_overlay.c` (aliases)
- 0x02: `text_op_02_terminate_stream.c`
- 0x07: `text_op_07_advance_glyph_timers_and_cursor.c`
- 0x0C: `text_op_0C_set_shifted_parameter.c`
- 0x16: `text_op_16_trigger_voice_or_audio_playback.c`
- 0x17: `text_op_17_wait_for_audio_load_step.c`
- 0x18 / 0x19: `text_op_18_set_control_byte_conditional.c` (0x19 alias)
- 0x1A: `text_op_1A_wait_on_audio_system_flag.c`
- 0x1B / 0x1C: `text_op_1B_set_flag_from_two_byte_id_if_prefixed.c` (0x1C alias)

Remaining TODO opcodes:

- 0x06, 0x08, 0x09, 0x0A, 0x0B, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x1D, 0x1E

## Distinct Wait / Synchronization Patterns Identified

- Wait-until-complete (advance on return == 1): Opcode 0x17 → `FUN_00206c28`.
- Wait-until-clear (advance on return == 0): Opcode 0x1A → `FUN_00206a90`.
- Immediate conditional set without waiting: Opcode 0x18 / 0x19.
- Parameterized asynchronous start: Opcode 0x16.

These indicate script-level fine control over pacing audio playback and overlay evolution.

## Shared Helper Functions Not Yet Renamed

- `FUN_00238f98`: Advances active overlay/glyph timers (candidate name: `advance_active_overlay_timers`).
- `FUN_00206ae0`: Voice/audio load/play request (needs verification of channel semantics & blocking parameter).
- `FUN_00206c28`: Audio/loader poll (returns 1 when idle/complete).
- `FUN_00206a90`: Returns audio/system status flag byte.

## Globals of Interest (Pending Definitive Naming)

- `gp-0x5140` (cursor) → used per opcode for script advancement.
- `gp-0x4304` (scaled parameter target of opcode 0x0C).
- `gp-0x4999` (1-byte control set via opcodes 0x18/0x19).
- Overlay slot array base (`iGpffffaed4` / `puGpffffaed4` in raw) traversed by timer helper.
- Layer tag byte (`cGpffffaec4`) for matching overlay/glyph updates.
- Countdown / positioning accumulators: `iGpffffbcdc`, `iGpffffbcd8`, `iGpffffbce0`, etc.

## Methodology Recap

1. Preserve raw decompiled functions under `src/` untouched.
2. For each analyzed opcode: create a new file under `analyzed/text_ops/` with a descriptive name and detailed header comment (behavior, side effects, open questions).
3. Update `text_ops_dispatch_table.md` entry once analyzed.
4. Maintain conservative naming—avoid speculative semantics until corroborated by cross-references (flags, globals, strings).
5. Leverage `FUN_00266368` (flag read) and `FUN_002663a0` (flag set) patterns to classify gating behaviors.

## Notable Findings / Patterns

- Multiple opcodes manipulate or conditionally advance the shared script cursor; waiting opcodes leave it untouched to achieve script-level stalls.
- Audio subsystem integrated tightly with dialogue scripting through a triad: trigger (0x16), wait-for-ready (0x17), wait-for-idle/clear (0x1A).
- Flag-setting via 0x1B suggests embedding dynamic state toggles inside dialogue streams (possibly to unlock subsequent conditional rendering or branching).
- Timer advancement isolated into its own opcode (0x07) rather than occurring implicitly each tick—implies script-expressed pacing for overlay animation.

## Open Questions Tracking

Carried from `text_ops_dispatch_table.md` plus new:

- Which remaining TODO opcodes handle line/page breaks, color changes, speed modifiers, or text positioning?
- Full semantic roles for `gp-0x4304` and `gp-0x4999` targets.
- Are 0x06 / 0x08 involved in spatial or style metadata (need disassembly review)?
- Confirm whether `gp-0x5140` stores a pointer (address) vs an index offset into a contiguous dialogue buffer (naming will follow).
- Validate multi-byte parameter endianness across yet-unseen opcodes (so far little-endian pattern consistent).

## Immediate Next Steps

1. Disassemble/analyze opcode 0x06 (LAB_00239338) and 0x08 (LAB_00239390) to see if they participate in the timing/overlay group.
2. Identify any opcode manipulating text color or selection (search for constants like 0x80808080 / palette writes inside remaining handlers).
3. Cross-reference `globals.json` for addresses `0x4304` and `0x4999` to attempt early safe naming (document if patterns emerge).
4. Continue refining audio subsystem naming once usage frequency and parameter meaning confirmed.

## File Inventory (Text Opcodes)

Analyzed handler files currently present:

```
analyzed/text_ops/
  text_op_00_load_palette_entry_or_advance.c
  text_op_01_spawn_banner_or_overlay.c
  text_op_02_terminate_stream.c
  text_op_07_advance_glyph_timers_and_cursor.c
  text_op_0C_set_shifted_parameter.c
  text_op_16_trigger_voice_or_audio_playback.c
  text_op_17_wait_for_audio_load_step.c
  text_op_18_set_control_byte_conditional.c
  text_op_1A_wait_on_audio_system_flag.c
  text_op_1B_set_flag_from_two_byte_id_if_prefixed.c
```

## Change Log (Recent Sessions)

- Added analysis for opcodes: 0x0C, 0x07, 0x18/0x19, 0x16, 0x17, 0x1A, 0x1B/0x1C.
- Updated dispatch table with corresponding descriptions.
- Established complementary wait patterns (0x17 vs 0x1A) and conditional flag set (0x1B/0x1C).
- Documented timer advancement separation (0x07) and parameter scaling (0x0C).

---

This file will be updated as additional opcode handlers are reversed and named.

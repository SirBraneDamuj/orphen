# SCR2 Decompiler Benchmark

This note captures the current benchmark target for script decompiler work. It focuses on `scr/scr2.out`, the unpacked/exported form of `scr/scr2.bin`.

## File choice

- `scr/scr2.bin` is 20,918 bytes and appears to be a packed outer form.
- `scr/scr2.out` is 55,574 bytes and begins with the expected 11-word SCR header.
- The first static decompiler target should consume `.out` files. Explaining or decoding `.bin` can be a separate loader/container task.

## `scr/scr2.out` header

Little-endian u32 words at file offset `0x00`:

| Index |    Value | Working label                 |
| ----- | -------: | ----------------------------- |
| 0     | `0x17d4` | pointer table end             |
| 1     | `0x18e4` | block1 start                  |
| 2     | `0x24be` | block2 start                  |
| 3     | `0x24d4` | block3 start                  |
| 4     | `0x2540` | block4 start                  |
| 5     | `0x1680` | pointer table start           |
| 6     | `0x3930` | descriptor/block region start |
| 7     | `0xd8e4` | footer start                  |
| 8     | `0x3964` | array A start                 |
| 9     | `0x3968` | array B start                 |
| 10    | `0x396c` | array C start                 |

The pointer table range `0x1680..0x17d4` contains 85 words: 84 non-zero entry offsets followed by a zero sentinel at index 84. The early entries point back into the dialogue/name region and preview as records containing names such as Orphen, Dortin, and Volcan.

## Prototype tool

Tool: `tools/scr_decompile.py`

Useful commands:

```bash
python tools/scr_decompile.py summary scr/scr2.out --entries 12
python tools/scr_decompile.py entries scr/scr2.out --start 80 --count 8
python tools/scr_decompile.py disasm scr/scr2.out --start 0x18e4 --max-lines 80
python tools/scr_decompile.py emit-python scr/scr2.out --start 0x18e4 --name subproc_18e4 --max-ops 80
python tools/scr_decompile.py emit-file scr/scr2.out --max-ops-per-function 500 > scr2_all.py
python tools/scr_decompile.py scan-subprocs scr/scr2.out --limit 20
python tools/scr_decompile.py scan-rel32 scr/scr2.out --start 0x17d4 --end 0x3930 --limit 20
```

Do not start whole-file decompilation at offset `0`: the first 0x2C bytes are the SCR header, not bytecode. `emit-file` currently discovers and emits the five VM entrypoints that the engine calls from header words 0..4:

| Header word | Generated function           | Runtime caller |
| ----------- | ---------------------------- | -------------- |
| 0           | `scene_init_17d4`            | `FUN_0025b6d0` |
| 1           | `scene_start_18e4`           | `FUN_0025b728` |
| 2           | `scene_tick_24be`            | `FUN_0025b778` |
| 3           | `actor_state_primary_24d4`   | `FUN_0025b978` |
| 4           | `actor_state_secondary_2540` | `FUN_0025b9a8` |

`emit-file` now emits a structural whole-file map. For `scr2.out`, the generated `scr2_all.py` contains:

- `DIALOGUE_POINTER_TABLE`: all 84 dialogue offsets plus the zero sentinel.
- `DIALOGUE_RECORDS`: all 84 dialogue records, preserving speaker/name controls, text spans, voice/audio controls, wait/control bytes, and raw bytes.
- `SUBPROC_MARKERS`: 298 non-zero `0x0B 0x04 <id16>` marker candidates with surrounding context bytes.
- `subproc_marker_<id>_at_<offset>(ctx)` stubs: one-line structural anchors for each marker. These are not yet proven callable script bodies.
- `CUTSCENE_STREAM_TABLE`: 12 full 32-bit stream offsets stored at `0xd8e4`, plus trailing bytes `80 d7`.
- `COROUTINE_STREAM_REFS`: 11 opcode `0xA1` sites that arm timed stream cursors. These include the `0xd780` stream referenced from opcode offset `0x4049`.
- `CUTSCENE_EVENT_STREAMS`: 13 timed scheduler streams containing 458 actionable records. Each record is parsed as `[u16 delay_frames][u16 flags][u32 target_offset]`, matching `FUN_0025ce30`.
- `CUTSCENE_REGION` / `CUTSCENE_CHUNKS`: the opaque `0x3930..0xd8e4` descriptor/cutscene-looking region split into raw chunks and annotated with any marker IDs inside each chunk.
- `FOOTER_REGION` / `FOOTER_CHUNKS`: the opaque tail beginning at `0xd8e4`.

The dynamic marker stubs remain structural anchors because the runtime slot/scheduler mapping still needs to be tied back to loader state.

The event stream model is source-backed by `FUN_0025ce30` (`analyzed/process_entity_queue_system.c`). The scheduler maintains four runtime cursors at `DAT_00571e40` and consumes 8-byte entries. `delay_frames` is compared to the per-channel timer accumulator, `flags` gates the entry through `FUN_00266368` or `uGpffffb0f4`, and `target_offset` is resolved relative to `iGpffffb0e8`. Targets inside `[uGpffffbd70,uGpffffbd74)` dispatch immediately through `FUN_00237b38`; other targets are queued into the subproc slot table (`iGpffffbd84`).

In `scr2.out`, many records target `0xab52`. The bytes immediately before that target are `0b 04 9c 11 00 00`, so the parsed target is the body of subproc `0x119c`. The body starts `9e 0c 01 1e 0b`, matching the self-removing/no-op signature observed in the debug HUD.

Current useful static result: disassembly from `0x18e4` produces recognizable VM/high-op names, including `VM_IMM_U32`, `set_global_color1_rgb`, `set_camera_distance`, `set_fade_radius_pair`, and `dispatch_indexed_event_with_dual_rgb`. This is not a full semantic decompile yet, but it is a workable listing format for iterating on handler arities and expression parsing.

The `emit-python` mode prints expression-aware pseudocode such as `def subproc_18e4(ctx): ...`. Known opcode signatures consume their VM expression arguments and emit readable calls, for example:

```python
ctx.set_global_rgb_color(r=10, g=30, b=50, addr=0x000018e4)
ctx.dispatch_indexed_event_with_dual_rgb(index=0, r1=120, g1=80, b1=60, r2=30, g2=15, b2=5, param=60, addr=0x00001954)
ctx.script_work_alu(mode='work', index=13, op='assign', value=0, addr=0x00001992)
ctx.branch_if_false(condition=ctx.spawn_entity_by_type(type_id=3, addr=0x00001a74), rel32=52, target=0x00001ab0, fallthrough=0x00001a80, addr=0x00001a73)
```

The expression parser handles the VM postfix arithmetic/comparison tokens (`+`, `-`, `*`, `//`, `%`, bitwise ops, comparisons, unary negation/not) so bytecode like `imm(1), imm(2), add, return` can print as `1 + 2`. Unknown opcodes still fall back to generic `ctx.op(...)` or `ctx.vm_token(...)` calls so the output stays honest while coverage grows.

## `0x32` rule

Use the source-grounded model from `docs/structural_block_prologue_analysis.md`:

```text
0x32 <rel32-le>
  push continuation = opcode_offset + 5
  target = opcode_offset + 1 + signed_rel32
  pc = target
```

Do not linearly decode the four bytes after `0x32` as structural opcodes. They are the self-relative cell consumed by `FUN_0025c220`.

## Runtime validation targets

Loader/table path:

- `FUN_0025b390`: confirm scene blob load, `DAT_00355058`, `DAT_00355cf4`, and workspace setup.
- `FUN_0025b778`: log entries read from `DAT_00355cf4` and the pointer passed to `FUN_0025bc68`.
- `FUN_00228e28`: for the resource-loader path, confirm the 11 header words are relocated by adding the loaded base pointer.

Interpreter path:

- `FUN_0025bc68`: break on structural opcode `0x32` and `0x04`.
- `FUN_0025c220`: step over and confirm `pc = old_pc + signed_rel32`.
- `FUN_0025c258`: log VM opcodes and returns while a known `scr2.out` entry is active.
- `FUN_0025bf70`: log immediate tokens `0x0c..0x11` and packed tokens `0x30/0x31` consumed by VM expressions.

Dialogue/voice correlation:

- `FUN_00237b38`: confirm dialogue stream starts from `iGpffffb0e8 + offset`.
- `FUN_00242a18`: break on voice-related cases `0x70..0x72` to correlate script records with voice playback/wait behavior.

## Next decompiler steps

1. Expand the opcode signature table from analyzed handlers so more calls consume typed expression arguments.
2. Tie dynamic subproc slots (`DAT_00355cf4` / `iGpffffbd84`) back to file offsets so `emit-file` can emit scheduled/registered subprocs, not just fixed header entrypoints.
3. Add a trace import mode so emulator logs of `pc`, opcode, and consumed bytes can annotate the static listing.
4. Revisit `.bin` only after `.out` control flow and parameter consumption are stable.

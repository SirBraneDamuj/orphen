# SCR2 offset tables, dialogue, and voice system map

This note consolidates where the SCR-derived offset arrays are loaded, how dialogue entry offsets are consumed, and which functions implement wait/poll semantics for dialogue and voice playback. All original FUN\_\* names are preserved for cross-referencing.

## Quick answers

- Loader of offset arrays (from scr2.out scene blob)
  - `FUN_0025b390`: Loads the scene/script blob; sets base `DAT_00355058` and pointer-table base `DAT_00355cf4` (computed as `DAT_00355058 + aligned_size`). Initializes workspace `DAT_00355060`.
- How dialogue entry offsets are used
  - Table consumer: `FUN_0025b778` iterates `DAT_00355cf4`; each entry is dispatched into the interpreter `FUN_0025bc68`.
  - Scheduler path: `FUN_0025ce30` turns offsets into absolute dialogue pointers via `FUN_00237b38(iGpffffb0e8 + offset)`, starting a dialogue stream.
- How cutscene/timed event streams are used
  - Opcode `0xA1` (`FUN_00261e30`) arms one of four scheduler cursors at `DAT_00571e40` with an inline stream offset.
  - `FUN_0025ce30` then consumes 8-byte records: `[u16 delay_frames][u16 flags][u32 target_offset]`.
  - In `scr2.out`, the footer table at `0xd8e4` contains 12 stream offsets, and an additional stream at `0xd780` is referenced directly by opcode `0xA1` at file offset `0x4049`.
- Wait on the dialogue system
  - Completion check: `FUN_00237c70` returns when the current dialogue stream is finished.
  - Per-frame advance: `FUN_00237de8` (uses `PTR_FUN_0031c640` and `PTR_DAT_0031c518` tables); page/chunk handling via `FUN_00239848`.
- Wait for voice lines
  - Query/stop: `FUN_002445c8` returns remaining time for a voice id (or stops on flag). Used to poll/wait until a line finishes.
  - Playback enqueue: `FUN_002443f8` (invoked via dispatcher case 0x70) starts a voice line using data at `DAT_00355058 + param_2`.

## Components and flow

- Scene/script loader
  - `FUN_0025b390` → sets:
    - `DAT_00355058` = base pointer to loaded scene/script blob
    - `DAT_00355cf4` = pointer table base (base + aligned size)
    - `DAT_00355060` = workspace pointer
  - Uses lower-level loader `FUN_00223268`.

- Sub-script interpreter
  - `FUN_0025bc68`: Bytecode VM with dispatch tables `PTR_LAB_0031e1f8`, `PTR_LAB_0031e228`, `PTR_LAB_0031e538`.
  - `FUN_0025b778`: Iterates the offset table at `DAT_00355cf4` and feeds entries to the interpreter; also handles a special pointer around `+0x100`.

- Scheduler → dialogue start via offsets
  - `FUN_0025ce30`: Time-based scheduler; when an entry elapses, if it targets a dialogue block, it calls `FUN_00237b38(iGpffffb0e8 + offset)` to kick the dialogue.
  - The same scheduler also queues non-dialogue targets into the subproc slot table. This is why cutscene movement/effect subprocs show in the debug HUD while dialogue advances.
  - Entry layout is source-backed by `src/FUN_0025ce30.c`: `entity_ptr[0]` is the delay/timer limit, `entity_ptr[1]` is the gate/condition flags word, and `*(u32 *)(entity_ptr + 2)` is the target offset.
  - `iGpffffb0e8`: "work memory" base used widely for offset→pointer resolution in this path.

- Dialogue engine
  - Start/stop: `FUN_00237b38` sets `pcGpffffaec0` (dialogue stream pointer) and UI/state; supports termination on null.
  - Tick/advance: `FUN_00237de8` parses per-opcode lengths (`PTR_DAT_0031c518`) and control dispatch (`PTR_FUN_0031c640`).
  - Page/chunk: `FUN_00239848` reads a block header and emits lines by repeatedly calling `FUN_00237de8`; finalizes page.
  - Completion: `FUN_00237c70` indicates the dialogue stream has finished.

- Voice subsystem
  - Dispatcher hub: `FUN_00242a18` routes sound ops.
    - Case 0x70 → `FUN_002443f8`: enqueue/play voice line (reads from `DAT_00355058 + param_2`).
    - Case 0x71 → `FUN_00244650`: adjust voice params (balance/pan/pitch/time).
    - Case 0x72 → `FUN_002445c8`: query remaining time or stop a voice id.
    - Case 0x73 → `FUN_00244210`: small related utility.
  - Asset loader: `FUN_00223698` streams VOICE.BIN (sets handles like `DAT_00355bb4`).

## Key globals

- `DAT_00355058`: Base pointer to the currently loaded scene/script blob.
- `DAT_00355cf4`: Pointer table base derived from the loaded blob; consumed by `FUN_0025b778`.
- `DAT_00355060`: Workspace pointer used by the loader flow.
- `iGpffffb0e8`: Work-memory base used to resolve offsets in scheduler/dialogue paths.
- `pcGpffffaec0`: Current dialogue stream pointer (set by `FUN_00237b38`).

## Relationship to scr2.out observations

- scr2.out begins with a u32 offset table. One entry (e.g., at 0x1680) points to a secondary offset array indexing dialogue script entries that embed voice directives.
- The lower cutscene section contains timed scheduler streams. For example, records targeting `0xab52` resolve to the body of subproc `0x119c` because the preceding marker bytes are `0b 04 9c 11 00 00`; that body starts with the self-removing no-op sequence `9e 0c 01 1e 0b`.
- Reverse trace example for the post-control line near `0x0b77`:
  - Dialogue record `0x0b76` (`"Argh!Take that!"`) is targeted by scheduler stream `0xd0c0` record 2.
  - Stream `0xd0c0` is armed by opcode `0xA1` at SCR2 offset `0x3d7c`.
  - The immediately preceding gate at `0x3d25` requires `flag[0x515] && !flag[0x516]`, then `test_controller_button_state(mask=0x10, selector=1)` while masks `0x20`, `0x40`, and `0x80` are clear.
  - On success it writes script work slot `13 = 22`, submits `0x45(0)`, and arms scheduler slot 0 with stream `0xd0c0`.
  - Predecessor stream `0xca30` is armed by `0xA1` at `0x2122` when `flag[0x515]` is still clear; near its end it targets subproc `0x0814` body `0x6526`, whose tail at `0x6809` sets `flag[0x515]`. This makes `0x515` look like the handoff/ready flag between the initial cutscene and the post-control trigger gate.
  - A similar later line starts at dialogue record `0x0d25` (`Magnus`: `"Hey!Somebody ransacked our room!"`). It is stream `0xd220` record 7, armed by `0xA1` at `0x3dd0`.
  - The `0xd220` gate at `0x3d84` requires `!flag[0x518]`, then `test_controller_button_state(mask=0x20, selector=1)` while masks `0x10`, `0x40`, and `0x80` are clear. On success it writes script work slot `13 = 24`, submits `0x45(0)`, and arms stream `0xd220`.
  - Stream `0xd220` later targets subproc `0x0b12` body `0x7e7e`; its tail at `0x7eef` sets `flag[0x518]`. This supports the model that these are position/direction-style trigger gates with per-cutscene one-shot latch flags.
  - Dialogue record `0x1116` (`Magnus`: `"Uh-oh."`) is a different-looking gate. It is stream `0xd430` record 13, armed by `0xA1` at `0x2537`.
  - The object tag is authored in SCR2 itself. The setup block at `0x180e` runs `select_pw_by_index(42)`, saves the selected pool slot to `work[15]`, writes `register[0x11] = 100`, ORs register `1` with `0x4000`, and writes register `8 = 4` on the selected object.
  - The `0xd430` gate at `0x24d4` requires `select_object_and_read_register(selector=0x100, register=0x11) == 100`, then `!flag[0x51c] && work[13] == 0`. Register `0x11` reads signed byte `current_object + 0x95`, an object/frame id/tag field, so this looks like a current-object-context trigger rather than a direction-mask trigger.
  - On success it ORs register 3 of the object selected by `work[15]` with `0x4000`, writes script work slot `13 = 28`, submits `0x45(0)`, sets flag `0x404`, and arms stream `0xd430`.
  - Stream `0xd430` later targets subproc `0x0d40` body `0x8b7a`; its tail at `0x8c2c` resets `work[13] = 0` and sets `flag[0x51c]`, giving this object-context trigger the same one-shot/latch shape as the position gates.
  - Runtime backing: `FUN_00252cc0` scans active pool objects and calls `FUN_00252828(player, candidate_object)`. When a candidate passes the interaction/active flag path, `FUN_00252828` stores that candidate in `psGpffffb79c` and calls `FUN_0025b978`; `FUN_0025b978` sets the script current object pointer to `psGpffffb79c` before executing header entry `0x24d4`. So the `0x100` selector in the gate means the interacted/candidate object context, not Orphen by default.
  - Final area transition trace: dialogue record `0x1647` (`Orphen`: `"Uh-oh! Looks like we're in for a fight."`) is stream `0xd780` record 40. This stream is armed at SCR2 offset `0x4049` after the gate at `0x3ffe` checks `!flag[0x523]` and the direction-mask input pattern.
  - After that dialogue, stream `0xd780` schedules subproc `0x1175` at body `0xaab4` after 120 frames; it writes register `8 = 9` on the entity stored in `work[10]` and then finishes its process slot.
  - The next record targets subproc `0x1187` at body `0xaafc`. It waits until opcode `0x86`/`FUN_0025d238` reports fullscreen fade completion, then runs the handoff cleanup: `0xE3(0)` clears `DAT_00355641`, `0xD5(1)` sets renderer byte `uGpffffb084`, `work[13] = 0`, `0x6D(1)` disables combat/alignment mode, `0x5C(work[10..12])` destroys the temporary entities, `0x45(0)` submits the scene point/state command, `set flag[0x523]`, `0x8E(1)` sets `DAT_003551ec = 0x20001` and `DAT_003551f8 = 1`, and `0x9E(-1)` clears the current process slot.
  - The actual scene reload is in the engine state path: `main_game_loop`/`FUN_002239c8` checks the same `DAT_003551ec` storage (Ghidra alias `uGpffffb27c`) and, once the fade gate passes, calls `FUN_0022a418`; that routine interprets `DAT_003551ec`/`DAT_003551f8`, selects the next scene entry, reloads scene assets through `FUN_0025b390`, and invokes `FUN_0025b6d0` for the new scene init script.
  - Raw source backing: `FUN_0025e560` implements `0x3d..0x40` flag query/set/clear/toggle; `FUN_0025f4b8` implements the `0x61` mask test; `FUN_00261e30` implements `0xA1`; `FUN_0025ce30` consumes the stream.
- In-engine, these offsets become absolute pointers either via:
  - Table iteration → interpreter (`DAT_00355cf4` → `FUN_0025b778` → `FUN_0025bc68`), or
  - Scheduler events → dialogue start (`FUN_0025ce30` → `FUN_00237b38(iGpffffb0e8 + offset)`).

## Verification breakpoints (debugger)

Set breakpoints and observe expected state changes:

- Loader/table init
  - `FUN_0025b390`: after load, check `DAT_00355058` and `DAT_00355cf4` are valid.
  - `FUN_0025b778`: confirm iteration over the pointer table; inspect the entry pointer passed to `FUN_0025bc68`.

- Dialogue lifecycle
  - `FUN_0025ce30`: log the offset chosen; verify `FUN_00237b38(iGpffffb0e8 + offset)` is called for dialogue entries.
  - `FUN_00237b38`: ensure `pcGpffffaec0` equals `iGpffffb0e8 + offset`.
  - `FUN_00237c70`: watch it return true at end of the stream.

- Voice playback/wait
  - `FUN_00242a18`: break on cases 0x70/0x72 to see play and wait calls originate.
  - `FUN_002443f8`: verify it reads voice metadata from `DAT_00355058 + param_2`.
  - `FUN_002445c8`: poll remaining time; confirm callers use this to gate progression.

## Cross-references (analyzed sources)

- Dialogue: `analyzed/dialogue_start_stream.c`, `analyzed/dialogue_text_advance_tick.c`, `analyzed/dialogue_stream_recursive_parser.c`, `analyzed/dialogue_text_advance_tick.c`, `analyzed/dialogue_opcode_event_filter.c`.
- Scheduler/loader/VM: `analyzed/bytecode_interpreter.c`, `analyzed/mcb_data_processor.c`, `analyzed/dispatch_system_function.c`, `analyzed/game_system_manager.c`.
- Audio/voice: `analyzed/calculate_3d_positional_audio.c`, `analyzed/calculate_sound_envelope_fade.c`, `analyzed/debug_printf_variadic.c` (for logging helpers), plus the raw FUN\_\* files noted above in `src/`.

## Notes / next steps

- The precise dialogue opcode that triggers the voice dispatcher calls is routed within the interpreter/dispatcher; confirm at runtime by breaking at `FUN_00242a18` during active dialogue and inspecting the caller/opcode.
- Keep `src/` pristine; name/rename only within `analyzed/` and keep original FUN\_\* names in comments.

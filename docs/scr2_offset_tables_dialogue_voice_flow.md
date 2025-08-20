# SCR2 offset tables, dialogue, and voice system map

This note consolidates where the SCR-derived offset arrays are loaded, how dialogue entry offsets are consumed, and which functions implement wait/poll semantics for dialogue and voice playback. All original FUN\_\* names are preserved for cross-referencing.

## Quick answers

- Loader of offset arrays (from scr2.out scene blob)
  - `FUN_0025b390`: Loads the scene/script blob; sets base `DAT_00355058` and pointer-table base `DAT_00355cf4` (computed as `DAT_00355058 + aligned_size`). Initializes workspace `DAT_00355060`.
- How dialogue entry offsets are used
  - Table consumer: `FUN_0025b778` iterates `DAT_00355cf4`; each entry is dispatched into the interpreter `FUN_0025bc68`.
  - Scheduler path: `FUN_0025ce30` turns offsets into absolute dialogue pointers via `FUN_00237b38(iGpffffb0e8 + offset)`, starting a dialogue stream.
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

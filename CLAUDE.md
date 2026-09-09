# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Reverse engineering of the PS2 game "Orphen: Scion of Sorcery" using Ghidra decompilation. The long-term goal is a cutscene skip patch. Analysis involves converting raw Ghidra output into documented, meaningfully-named C functions.

## Git Safety

Never run `git checkout --`, `git reset --hard`, `git clean`, or `git stash`
without explicit user approval. The working tree frequently contains
uncommitted experimental work, and one `git checkout --` has already destroyed
some of it. If you need a clean tree, copy the files aside or make a branch.

## Build & Run

```bat
port\build-msvc.bat Release
port\build\msvc-Release\orphen_port.exe --disc-root . --scene s01_e024 --no-audio
```

Always pass `--no-audio` unless audio behaviour is what is being tested, and
never leave an audio-enabled process running in the background. Build `Release`
for anything involving frame rate — see `## Native Port` below for why, and for
the two traps when measuring.

## Repository Layout

- `analyzed/` — Human-authored analyzed functions (the primary working area)
  - `ops/`, `text_ops/`, `structural_ops/`, `low_ops/` — Bytecode VM opcodes by category
  - `object_methods/` — Object-specific methods
  - `opcode_dispatch_tables.md` — Master opcode-to-handler mapping
- `src/` — Raw Ghidra decompilation (gitignored, pristine reference only)
- `src-jp/` — Japanese version functions (gitignored, for cross-referencing only)
- `docs/` — Technical analysis documentation (memory map, debug system, scripting VM, etc.)
- `scripts/` — Python analysis tools (format parsers, extractors). **Not authoritative** — these are hypothesis-testing tools, not ground truth.
- `out/` — Extracted assets (gitignored)
- `scr/`, `scr_jp/` — Script data (binary and extracted)
- `globals.json`, `strings.json` — Ghidra-exported metadata (gitignored, generated via scripts)
- `port/` — Native C++ port harness (SDL2 + fixed-function OpenGL). See `port/README.md`.

## Analysis Workflow

1. Pick a raw function from `src/` (e.g., `FUN_00260738`)
2. Create a new file in `analyzed/` with a descriptive snake_case name
3. Add a header comment with: original `FUN_*` name/address, inferred behavior, parameter semantics, side effects (global writes), PS2-specific notes
4. Rename locals and known globals to descriptive names; leave unanalyzed callees as `FUN_*`
5. Reference `globals.json` for DAT_* variable context and `strings.json` for string literals at hex addresses
6. Do NOT modify files under `src/` — keep it pristine

## Ground Truth Sources

When porting behaviour, derive it from the original PS2 disassembly, a GS dump,
or a save-state / RAM-dump diff — not from prior analysis notes in this repo.
The notes have been wrong more than once: chains rendering was recorded
backwards, and a bandana analysis doc had sin and cos swapped, both of which
shipped into the port before anyone looked at the binary. `docs/` and
`analyzed/` are hypotheses with a good hit rate, not ground truth. If a note
disagrees with the binary, the binary wins — and fix the note in the same pass.

## Key Rules

- **Focus on `src/` (English version)**, not `src-jp/` or `*-jp.*` files, unless explicitly cross-referencing
- **Do NOT rename functions that haven't been analyzed** — leave unknown callees as `FUN_*`
- **Do NOT treat `scripts/` as source of truth** — they are experimental tools
- **Keep responses terse** — avoid sensationalizing discoveries

## Native Port (`port/`)

**Build Release for anything involving frame rate, and confirm which build is
running before investigating a performance complaint.** `port/build-msvc.bat`
defaults to `Debug`, and Debug is ~8x slower here: MSVC's debug runtime makes
every `std::vector` index a checked call and inlines nothing, which this
renderer — a tight `std::vector` loop doing per-vertex maths on the CPU — is
the worst case for. Measured on `s01_e024`: 40.5 ms/frame Debug vs 4.9 ms
Release. It presents worse than that, because `main()`'s fixed-timestep
accumulator runs catch-up simulation steps when a frame overruns 16.6 ms, so a
slow frame makes itself slower; Debug settles at ~4.4 steps/frame and ~12 fps.

(Invocation is in `## Build & Run` above.) Debug is still the right build for
debugging. Just never quote a frame time
from it.

Two traps when measuring (details and current numbers in `port/README.md`):

- **`--no-vsync` is not honoured.** The DWM paces windowed surfaces to the
  refresh regardless, and the wait surfaces in whichever GL call fills the
  driver queue rather than in `swapBuffers`. A total sitting on ~16.6 ms is
  measuring the compositor, and per-phase timings under it will appear to move
  between phases run to run. Use `--render-bench N` to clear that floor.
- **Verify draw-path changes with `--screenshot <path>[:<frame>]`**, which
  forces one simulation step per rendered frame so captures are reproducible.
  Two builds captured at the same frame diff byte for byte.

Behaviour must stay deterministic: `--frames N` with `--actor-report` and
`--scr-report` is the regression guard, and rendering work must not change it.

## Verifying Behaviour Claims

Do not claim a bug is fixed or absent on the strength of a scripted frame
capture or a short trace run. Most bugs here are state-dependent — bone-override
slot lifecycle, door opcodes, entity pool contents, scheduler cursor — and a
capture taken before the state is reached proves nothing. Before saying
something is verified, either run long enough to reach the state that shows it,
or ask for a save state or screenshot. "I could not reproduce it from the
evidence I have" is the honest report; "it is fixed" is not.

Fidelity is the goal, not a working scene: a divergence from the decompiled
code is a bug to fix, never a limitation to document around. If a change makes
the port look right by doing something the original does not do, say so instead
of shipping it.

## Technical Context

- **PS2 MIPS architecture** with custom hardware optimizations
- **Stack-based bytecode VM** with 271 instruction types — see `docs/scripting_system_analysis.md` and `analyzed/opcode_dispatch_tables.md`
- **Fixed-point math**: 4096.0 scaling factor for coordinates
- **Flag system**: ~18,424 game state flags via bit manipulation on `DAT_00342b70`
- **Key addresses**: debug output `0x003555dc`, scene work flags `0x0031e770`, script memory `0x01C40000–0x01C8FFFF`, work memory `DAT_00355060`
- **Graphics**: DMA packets and GPU command buffers
- **Controllers**: dual controller support, 64-entry input history

## Rendering & Data Layout Conventions

Extend this list as new constants are confirmed against the binary.

- **Entity pool stride is `0x1D8`, not `0xEC`** — `FUN_00229c40` allocates a
  slot with `FUN_00267e78(entity, 0x1d8)`. Verify strides in the disassembly
  before writing an ad-hoc dump script; a wrong stride here sent a whole
  thunder-cue investigation off the rails.
- **GS colour is modulate, `0x80` is x1.0** — `(Ct * Cv) >> 7`, so `0xFF` is
  x1.99 and the GS can brighten a texel. Do not read `0x80` as 0.5x and double
  the brightness to compensate. `GL_MODULATE` clamps at x1.0 and cannot reach
  the top of that range on its own.
- **Fades, letterboxing, and scissored overlays draw to the game viewport**,
  not the whole window.
- **Fixed-point coordinates scale by 4096.0**; the EE's FPU truncates toward
  zero, which is worth about one unit on overlay positions.

## Scripts (Python)

Run with `python scripts/<name>.py`. Key tools:
- `psm2_parser.py`, `psc3_parser.py`, `psb4_parser.py` — 3D model/audio format parsers
- `orphen_lz_headerless_decoder.py` — LZ decompression
- `gp_address_calculator.py` — GP-relative address resolution
- `export_funs.py`, `export_globals.py`, `export_strings.py` — Ghidra export scripts (run inside Ghidra)

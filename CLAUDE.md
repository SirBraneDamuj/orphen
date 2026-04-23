# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Reverse engineering of the PS2 game "Orphen: Scion of Sorcery" using Ghidra decompilation. The long-term goal is a cutscene skip patch. Analysis involves converting raw Ghidra output into documented, meaningfully-named C functions.

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

## Analysis Workflow

1. Pick a raw function from `src/` (e.g., `FUN_00260738`)
2. Create a new file in `analyzed/` with a descriptive snake_case name
3. Add a header comment with: original `FUN_*` name/address, inferred behavior, parameter semantics, side effects (global writes), PS2-specific notes
4. Rename locals and known globals to descriptive names; leave unanalyzed callees as `FUN_*`
5. Reference `globals.json` for DAT_* variable context and `strings.json` for string literals at hex addresses
6. Do NOT modify files under `src/` — keep it pristine

## Key Rules

- **Focus on `src/` (English version)**, not `src-jp/` or `*-jp.*` files, unless explicitly cross-referencing
- **Do NOT rename functions that haven't been analyzed** — leave unknown callees as `FUN_*`
- **Do NOT treat `scripts/` as source of truth** — they are experimental tools
- **Keep responses terse** — avoid sensationalizing discoveries

## Technical Context

- **PS2 MIPS architecture** with custom hardware optimizations
- **Stack-based bytecode VM** with 271 instruction types — see `docs/scripting_system_analysis.md` and `analyzed/opcode_dispatch_tables.md`
- **Fixed-point math**: 4096.0 scaling factor for coordinates
- **Flag system**: ~18,424 game state flags via bit manipulation on `DAT_00342b70`
- **Key addresses**: debug output `0x003555dc`, scene work flags `0x0031e770`, script memory `0x01C40000–0x01C8FFFF`, work memory `DAT_00355060`
- **Graphics**: DMA packets and GPU command buffers
- **Controllers**: dual controller support, 64-entry input history

## Scripts (Python)

Run with `python scripts/<name>.py`. Key tools:
- `psm2_parser.py`, `psc3_parser.py`, `psb4_parser.py` — 3D model/audio format parsers
- `orphen_lz_headerless_decoder.py` — LZ decompression
- `gp_address_calculator.py` — GP-relative address resolution
- `export_funs.py`, `export_globals.py`, `export_strings.py` — Ghidra export scripts (run inside Ghidra)

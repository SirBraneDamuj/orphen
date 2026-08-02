# Map selection → map load → entity setup

This traces what happens when you pick a map from the debug menu.

## Flow overview

- Comprehensive debug menu (analyzed) → `comprehensive_debug_menu_handler.c` (FUN_00269140)
  - Pressing the execute button calls `FUN_00205f98(category, 0)`.
- `FUN_00205f98` calls the semaphore wrapper then → `debug_system_core_dispatcher` (FUN_00304bf0)
  - See `analyzed/debug_system_core_dispatcher.c`.
  - MAP SELECT lands in the 0x6000-range branch and calls `FUN_002f4b10` with params that route to the file loader.
- File loader `FUN_00223268(archive_type, file_id, dst)` is invoked with `archive_type=2` (MAP.BIN) and `file_id` computed from the two-part selection:
  - `get_map_id_from_debug_selection(major, minor)` → `(major << 5) + minor`.
  - See `analyzed/get_map_id_from_debug_selection.c`.
- `FUN_00223268` reads the entry from MAP.BIN’s lookup table (documented in `analyzed/map_bin_format.h`) and DMA-copies the packed map file into `0x1849A00`.
  - On cache hits, `load_cached_data` (FUN_00222d68) can short-circuit I/O.
- The packed map file is then parsed by `parse_map_data` (FUN_0022b5a8) → `analyzed/parse_map_data.c`.
  - Validates magic `PSM2`.
  - Walks section offsets and materializes data into engine globals.

## Where entity lists come from

Within the loaded PSM2 map chunk, Section at offset 0x04 contains the static entity list for that map:

- Count is a 16-bit value at the start of the section; the structures follow.
- Each entity record is 32 bytes (0x20). The parser copies six u32 values and initializes the tail, setting an ID field to 0xFFFF.
- Globals populated by the parser:
  - `DAT_003556d8` → base address of the entity list in working memory
  - `DAT_003556d4` → number of entities

See the "Process Section 1" block in `analyzed/parse_map_data.c` for exact loops and alignment.

## After parse: who instantiates runtime entities?

`parse_map_data` ends by calling:

- `FUN_0022c3d8()`
- `FUN_0022c6e8()`
- `FUN_0022d258()`
- `FUN_00211230()` (commented as "Final map initialization")

These are **not** entity instantiation. All four are terrain/render setup: each loops `0 .. DAT_00355688` over the Section D records at `DAT_003556ac` (0x80 stride) and `DAT_003556b0` (0x78 stride). `FUN_00211230` in particular builds GIF/VIF packets for the terrain geometry and is already analyzed as `analyzed/packet_vertex_emitter.c`; it never reads `DAT_003556d8`. The earlier reading of this step as "static entity descriptors turned into runtime objects" was wrong and is corrected here.

Runtime objects come from the SCR script instead — see `analyzed/map_bootstrap_sequence.c` for the real load order and `analyzed/entity_pool_and_descriptors.c` for the pool the spawn opcodes allocate from.

Runtime entity memory comes from the entity allocator:

- `initialize_entity_memory_system` (analyzed) sets up the 0x01C49A00-based heaps and the 62-slot table.
- Entities are typically addressed via slot indices and processed by systems like physics (`process_entity_physics_and_collision.c`).
- Script opcodes work against a per-entity array (`DAT_003556e0` base, `DAT_003556dc` count), e.g. `update_entity_timed_parameter.c`.

## Data sources and IDs

- MAP.BIN layout: header + 4-byte lookup entries where
  - bits 31..17 = sector offset, bits 16..0 = size in 4-byte words. See `analyzed/map_bin_format.h`.
- Current map numbers displayed by the loop come from globals printed as `MAP>(MP%02d%02d)` in `analyzed/main_game_loop.c`.

## Open thread / next targets

- What the Section A 32-byte records at `DAT_003556d8` are actually *for*. `parse_map_data` copies six raw dwords per record and no consumer has been fully traced. The one lead found is `FUN_00208450:86`, which indexes the table by a halfword held in the per-tile Section J records at `DAT_003556e0` during per-frame floor processing — suggesting per-tile markers rather than spawnable actors. Unconfirmed.
- Tie-in with SCR.BIN scene loader: confirmed in `analyzed/map_bootstrap_sequence.c` — `FUN_0025b390` reads the SCR file index from the MCB entry's halfword at `+4` and loads it through `FUN_00223268(1, id, 0x1849a00)`. Note the archive-type table in `docs/disc_file_system_analysis.md` lists type 1 as unknown and type 6 as SCR.BIN; the call site plainly passes 1, so that table needs rechecking against `FUN_00223268` itself.

## Quick references

- Menu: `analyzed/comprehensive_debug_menu_handler.c`
- Dispatcher: `analyzed/debug_system_core_dispatcher.c`
- ID calc: `analyzed/get_map_id_from_debug_selection.c`
- File loader: `FUN_00223268` (extern; many call sites)
- MAP.BIN doc: `analyzed/map_bin_format.h`
- Map parser: `analyzed/parse_map_data.c`
- Entity allocator: `analyzed/entity_memory_allocator.c`, `analyzed/initialize_entity_memory_system.c`
- Physics loop (uses entity structures): `analyzed/process_entity_physics_and_collision.c`

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

These appear to be the post-parse setup where the static entity descriptors are turned into runtime objects.

Runtime entity memory comes from the entity allocator:

- `initialize_entity_memory_system` (analyzed) sets up the 0x01C49A00-based heaps and the 62-slot table.
- Entities are typically addressed via slot indices and processed by systems like physics (`process_entity_physics_and_collision.c`).
- Script opcodes work against a per-entity array (`DAT_003556e0` base, `DAT_003556dc` count), e.g. `update_entity_timed_parameter.c`.

## Data sources and IDs

- MAP.BIN layout: header + 4-byte lookup entries where
  - bits 31..17 = sector offset, bits 16..0 = size in 4-byte words. See `analyzed/map_bin_format.h`.
- Current map numbers displayed by the loop come from globals printed as `MAP>(MP%02d%02d)` in `analyzed/main_game_loop.c`.

## Open thread / next targets

- Map→entity instantiation: analyze `FUN_00211230` to document how entries at `DAT_003556d8` are mapped to runtime objects and which fields in the 32-byte records are type/pos/flags.
- Tie-in with SCR.BIN scene loader: `scene_loader_and_initializer.c` shows archive type 1 (likely SCR.BIN) loads per-scene object arrays; confirm how script-driven actors combine with map’s static entity list.

## Quick references

- Menu: `analyzed/comprehensive_debug_menu_handler.c`
- Dispatcher: `analyzed/debug_system_core_dispatcher.c`
- ID calc: `analyzed/get_map_id_from_debug_selection.c`
- File loader: `FUN_00223268` (extern; many call sites)
- MAP.BIN doc: `analyzed/map_bin_format.h`
- Map parser: `analyzed/parse_map_data.c`
- Entity allocator: `analyzed/entity_memory_allocator.c`, `analyzed/initialize_entity_memory_system.c`
- Physics loop (uses entity structures): `analyzed/process_entity_physics_and_collision.c`

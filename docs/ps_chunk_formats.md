# PS\* chunk formats in MAP.BIN (PSM2 / PSC3 / PSB4)

This summarizes header bytes and how the executable seems to use them.

## Executive summary

- The game checks the full 4-byte magic (not just the trailing digit) and then runs a format-specific parser.
- Confirmed in code:
  - PSM2: `analyzed/parse_map_data.c` checks `DAT_01849a00 == 0x324d5350` ("PSM2").
  - PSB4: `src/FUN_0022ce60.c` checks `DAT_01849a00 == 0x34425350` ("PSB4").
- All variants appear to use a small header followed by an offsets table into sections; the exact layout differs per magic.

## On-disk header bytes (examples)

- PSM2
  - dword[0] magic = 0x324d5350 ("PSM2")
  - dword[1] = 0x0000003C (header size)
  - dword[2..] = offsets to sections (monotonic increasing), 0 entries are absent sections
  - Matches `parse_map_data.c`, which references globals `DAT_01849a04`, `...08`, `...0C`, etc. as section offsets
- PSC3
  - dword[0] magic = 0x33435350 ("PSC3")
  - dword[1] often = 0x00030005 / 0x00030008 (looks like packed version/flags)
  - dword[2] = 0x000001D4 (first table/section offset in many samples)
  - dword[3] = 0x00000280, dword[4] = 0x00000044 (constant), then more offsets
  - Likely a different header schema; still an offsets table pattern
- PSB4
  - dword[0] magic = 0x34425350 ("PSB4")
  - dword[1] = 0x00000010 (header size)
  - dword[2], dword[3] are offsets
  - Following dwords often decode to IEEE-754 floats (e.g., 41a00000 = 20.0f), suggesting embedded vectors/AABB
  - Parser present at `src/FUN_0022ce60.c` reads `DAT_01849a04`, `...08`, etc., similar to PSM2 but with different section semantics

## Code references and dispatch

- Loader: `FUN_00223268` (extern in several analyzed files) reads the chosen MAP.BIN entry into `0x1849a00` and sets globals like `DAT_01849a00`, `...04`, `...08`, etc.
- PSM2 path: `analyzed/parse_map_data.c` validates `DAT_01849a00 == 0x324d5350` ("PSM2") then consumes sections at the offsets table.
- PSB4 path: `src/FUN_0022ce60.c` validates `DAT_01849a00 == 0x34425350` ("PSB4") and parses its own section schema into globals like `DAT_00345a18`, `DAT_00345a20`, etc., then calls downstream setup (`FUN_00211b80`, `FUN_002256d0/f0`).
- PSC3 path: `src/FUN_00222498.c` contains a guarded check for `0x33435350` ("PSC3") after calling the headerless LZ routine `FUN_002f3118`. If a flag bit indicates "compressed PSC3", it asserts the decoded buffer begins with PSC3 and then performs additional copy/relocation (`FUN_00221f60`, `FUN_00221e70`, `FUN_00212058`). This confirms PSC3 is handled via a distinct loader pipeline.

## Takeaways

- The surrounding bytes after the ASCII-looking magic are significant:
  - A header-size dword (e.g., 0x3C for PSM2, 0x10 for PSB4)
  - A set of section offsets
  - For PSC3, an apparent version/flags dword precedes the offsets
- The executable uses the exact 4-byte magic to select the parser path, and then the offsets table drives how subsequent data are interpreted.

## Suggested next analysis steps

1. Identify the PSC3 parser by searching for code that references `DAT_01849a00` followed by an error call using a nearby string (akin to `0x34c1b8` in `FUN_0022ce60`).
2. Cross-compare section counts and meanings across formats:
   - PSM2’s offsets at 0x04..0x38 mapped to entity, vertex, coords, etc. in `parse_map_data.c`.
   - PSB4’s parser (`FUN_0022ce60`) shows different record sizes and per-entry fields.
3. Generate a manifest (done: `out/map_manifest.csv`) and cluster by dword[1] to infer version/flags breakdown for PSC3.

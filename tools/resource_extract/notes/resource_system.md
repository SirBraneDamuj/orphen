# Orphen PS2 Resource Packing System

Reverse-engineered from the decompiled loader code. Everything below is
grounded in actual game code — cross-references are listed per section.

---

## 1. Disc layout

Nine files on the disc image, enumerated in a pointer table at `0x00315a58`
(see `src/FUN_00222e18.c` for the open loop):

| index | name      | purpose                             |
| :---: | :-------- | :---------------------------------- |
|   0   | GRP.BIN   | graphics (sprites/FMV stills)       |
|   1   | SCR.BIN   | script tables                       |
|   2   | MAP.BIN   | 3D meshes (PSM2 / PSC3 / PSB4)      |
|   3   | TEX.BIN   | textures (BMPA container)           |
|   4   | ITM.BIN   | item data                           |
|   5   | VOICE.BIN | voice clips                         |
|   6   | SND.BIN   | sound effects / music               |
|   7   | MCB0.BIN  | MCB1 index (12 000 bytes, 15×100×8) |
|   8   | MCB1.BIN  | scene script bundles                |

All nine are opened at boot; their `{base_sector, ???}` pairs are cached at
`DAT_00560170 + i*8` (see `src/FUN_00223038.c`).

---

## 2. Flat-TOC BINs (GRP / SCR / MAP / TEX / ITM / SND / VOICE)

Structure (all little-endian):

```
u32 entry_count           // at file offset 0
u32 entries[entry_count]  // followed immediately
    bits [ 0 : 17] = size in 32-bit words  (bytes = words * 4)
    bits [17 : 32] = sector offset         (sector = 2048 bytes)
```

- Accessor: `FUN_00221b18` / `b30` / `b48` / `b60` / `b78` / `c90` each reads
  `iGpffffbc{24,1c,28,2c,20,38} + id*4` — one TOC per BIN in RAM.
- Dispatcher: `FUN_00223268(category, id, dest)` selects the right TOC and
  calls `FUN_00223038` to DMA the sectors.
- All entries are **headerless-LZ compressed** (decoder `FUN_002f3118.c`,
  Python port: `tools/resource_extract/baseline/lz_decoder.py`).

### Verified magics after decompression

| BIN     | entries | magics found                             |
| :------ | :-----: | :--------------------------------------- |
| MAP.BIN |   697   | 500 × `PSC3`, 165 × `PSM2`, 32 × `PSB4`  |
| TEX.BIN |   714   | 712 × `BMPA`, 2 × raw u32-header blobs   |
| SCR.BIN |   227   | varied u32 prefixes (script data tables) |

(GRP.BIN, ITM.BIN, SND.BIN, VOICE.BIN absent from the working disc copy;
the extractor handles them when present.)

---

## 3. MCB0 / MCB1 (scene bundles)

Structure:

```
MCB0.BIN : 12 000 bytes = 15 sections × 100 entries × 8 bytes
           per entry (little-endian):
               u32 byte_offset     // absolute into MCB1.BIN
               u32 byte_size       // 0 == empty slot
           table index = section * 800 + entry * 8

MCB1.BIN : ~295 MB of raw bundle blobs, concatenated.
           NOT LZ-compressed at this level; FUN_00222898 reads bytes
           directly into DAT_01949a00 (3 MB scene-cache buffer).
```

- 191 non-empty bundles total across 15 sections.
- **Bundle internal format is a flat linked-list of records, starting at
  offset 0 of the bundle** (`FUN_00222c08` walker / `FUN_00222c50` appender):

  ```
  record := { u32 id, u32 size, u8 payload[size] }
  list   := record* then  u32 id=0xFFFFFFFF  (terminator)
  id     := (category << 16) | resource_id
  ```

  `category` matches the `FUN_00223268` dispatch table:
  `0 = GRP, 1 = SCR, 2 = MAP, 3 = TEX, 4 = ITM, 6 = SND`. Payloads are
  headerless-LZ compressed, identical format to the flat BIN entries.

- **Scene-private namespace**: a record's `resource_id` is NOT an index
  into the flat BIN of that category. MCB tex `0x0001` and TEX.BIN entry
  `0x0001` have identical sizes but different bytes — bundles carry their
  own copies of resources, scoped to the scene.
- All 6,935 records across the 191 bundles decode cleanly. Totals:
  grp=2,231 (mostly `PSC3`), scr=197, map=930 (mostly `PSM2`), tex=2,907
  (mostly `BMPA`), itm=3, snd=667.
- Embedded meshes parse with the existing `psm2.py` / `psc3.py` / `psb4.py`
  parsers unchanged (191/191 PSM2, 2785/2798 PSC3, 61/61 PSB4).

---

## 4. Mesh formats (MAP.BIN only)

- **PSM2** — main geometry, 165 files. Parser: `tools/resource_extract/v2/psm2.py`
  (header sections A/B/C/D at fixed offsets; see header comment for layout).
- **PSC3** — small colour-per-vertex models, 500 files. Parser: `psc3.py`.
- **PSB4** — effect / glow geometry (referenced from script opcode 0xE5),
  32 files. Parser: `psb4.py` (Sections A/B/C; no per-vertex normals).

All three parsers apply the world-axis remap `(x, y, z) → (x, z, -y)` when
they emit OBJs (Z-up source → Y-up output).

---

## 4a. BMPA texture format (TEX.BIN + MCB cat=3)

All observed BMPA files are exactly 66,564 bytes and hold a single 256×256
8-bit indexed image:

```
offset  size     field
------  ----     -----
  0       4      magic "BMPA"
  4       1024   palette: 256 entries of BGRA8888 (swap R/B to display as RGBA;
                 alpha already 0..255, no PS2 0x80=opaque scaling)
  1028    65536  image: 256*256 bytes, 1 palette index per pixel, row-major
                 BOTTOM-UP (PS2 GS convention; flip vertically on export)
```

- Palette channel order is **BGRA**, not RGBA — this matches the PS2 GS CLUT
  layout. Exporting without the swap turns skin tones blue and fire green.
- Image rows are bottom-up; flip vertically on write to PNG.
- No CLUT swizzle is needed — palettes are stored already de-swizzled.
- Parser + PNG exporter: `tools/resource_extract/v2/bmpa.py`.
- Validated on 712 TEX.BIN entries + 2,903 MCB-embedded tex records:
  all decode cleanly.

---

## 5. Tooling in `tools/resource_extract/v2/`

| file                              | purpose                                           |
| :-------------------------------- | :------------------------------------------------ |
| `bin_toc.py`                      | flat-TOC parser (works on any of the 7 flat BINs) |
| `extract_all.py`                  | top-level: walk a disc dir, dump every BIN        |
| `mcb.py`                          | MCB0/MCB1 bundle splitter                         |
| `mcb_bundle.py`                   | unpack each MCB1 bundle's linked-list records     |
| `mcb_scan_meshes.py`              | magic-scan embedded meshes inside MCB bundles     |
| `psm2.py` / `psc3.py` / `psb4.py` | mesh parsers (validated on MAP.BIN)               |
| `bmpa.py`                         | BMPA texture decoder + PNG exporter               |
| `dump_map_objs.py`                | convert every MAP.BIN mesh to OBJ                 |

### Quickstart

```bash
# Extract every flat BIN + MCB pair present in the project root.
python -m tools.resource_extract.v2.extract_all . out/all

# Render every MAP.BIN mesh as an OBJ.
python -m tools.resource_extract.v2.dump_map_objs out/all/map out/all/map_obj

# Unpack every MCB1 bundle into per-record LZ-decoded files.
python -m tools.resource_extract.v2.mcb_bundle --src out/all/mcb --dst out/all/mcb_unpacked

# Decode every BMPA texture (TEX.BIN) to PNG.
python -m tools.resource_extract.v2.bmpa --src out/all/tex --dst out/all/tex_png --flat

# Decode every MCB-embedded BMPA to PNG (preserves per-bundle subdirs).
python -m tools.resource_extract.v2.bmpa --src out/all/mcb_unpacked --dst out/all/mcb_tex_png
```

---

## 6. Known gaps

- **SCR.BIN** per-entry layouts: 227 distinct tables, each beginning with a
  little-endian u32 count/size; internal layout unknown.
- **SND / ITM / GRP(cat=1)** record internals: these payloads LZ-decode but
  the inner byte layout is not yet analysed (SND = 665×`03 00 00 00`-prefix,
  GRP cat=1 has no consistent magic — likely palette/sprite blobs).
- **GRP vs MAP category split**: cat=0 and cat=2 both contain `PSC3`
  meshes; cat=0 (GRP) meshes are probably UI/HUD/battle-logo assets, cat=2
  (MAP) are world geometry. Usage by caller not yet confirmed.

# Resource Extract

Tooling for extracting 3D models (and related data) from the Orphen: Scion of Sorcery PS2 disc.

## Layout

- `baseline/` — Ported from `scripts/` as the starting point. These work, but face
  reconstruction for PSM2 has historically been very low-yield (typical maps emit a
  handful of faces from thousands of D-records). Treat as a known-imperfect reference
  to compare against, not as ground truth.
- `v2/` — New implementations grounded in a fresh re-analysis of the PS2 loaders.
  Lives separate from `baseline/` so we can A/B compare outputs.
- `notes/` — Working analysis notes (per-format) accumulated during re-analysis.
  These are scratch documents; finalized findings should be promoted to top-level
  `docs/`.

## Formats covered

| Format | Magic    | Loader (Ghidra)    | Doc                                     |
|--------|----------|--------------------|-----------------------------------------|
| PSM2   | `PSM2`   | `FUN_0022b5a8`     | `docs/psm2_format_and_loader_notes.md`  |
| PSC3   | `PSC3`   | `FUN_00222498`     | `docs/psc3_format_and_loader_notes.md`  |
| PSB4   | `PSB4`   | `FUN_0022ce60`     | `docs/psb4_format_and_loader_notes.md`  |

`MAP.BIN` (sector table) carries PSM2 chunks for map geometry; many entries are
LZ-compressed with the headerless decoder in `lz_decoder.py`.

## Pipeline

1. `baseline/extract_map_bin.py` — split `MAP.BIN` into per-segment `.bin` and
   attempt LZ decode → `.psm2` (when magic appears) or `.dec.bin`.
2. `baseline/export_psm2_to_obj.py` — parse PSM2 chunks → `.obj` per J-record.
3. `baseline/export_psc3_to_obj.py` — parse PSC3 chunks → `.obj`.

## Known issues to address in v2

- **PSM2 D-record indexing**: the in-memory four-u16 indices reference
  `DAT_0035569c` (the C-derived float-triplet table), but the on-disk D-stream is
  decoded via `FUN_0022b4e0`, which the baseline parser approximates as a fixed
  32-byte stride. This is almost certainly wrong and is the root cause of the
  low face yield. Need to model the variable-length reader properly.
- **PSC3 face reconstruction**: baseline emits sequential triangles or
  draw-descriptor i0..i3 quads, but the renderer (`FUN_00212058` /
  `FUN_002129b8`) actually walks per-stream resource records to determine
  primitive layout. Need to follow that path.
- **PSB4**: per the docs, no positions live in PSB4 — only indices. Likely a
  companion to PSC3 / PSM2 vertex tables; leaving generation deferred until the
  consumer is identified.

## Running (from repo root)

```bash
python -m tools.resource_extract.baseline.extract_map_bin <MAP.BIN> out/maps
python -m tools.resource_extract.baseline.export_psm2_to_obj <psm2_or_dec.bin> out/obj_psm2
python -m tools.resource_extract.baseline.export_psc3_to_obj --src out/maps --dst out/obj_real
```

The standalone form (`python tools/resource_extract/baseline/extract_map_bin.py ...`)
also works because each script falls back to absolute imports when not loaded as
a package.

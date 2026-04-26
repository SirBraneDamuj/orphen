# Regenerating the viewer data

The map_viewer reads two output trees:

- `out/map_gltf_all/<scene>/<map>.gltf` — one PSM2-derived map per scene (191 scenes).
- `out/models/<grp>/aid<N>/<grp>.gltf` — deduplicated PSC3 character/prop models (281 unique models, ~1839 animation glTFs).

If both trees are deleted, regenerate them with the following pipeline. All commands are run from the repo root with the project venv active.

## Inputs (must exist at repo root)

- `MCB0.BIN`, `MCB1.BIN` — paired bundle archive (scene-keyed), source for both map geometry and character models.

The disc rip lives at the repo root alongside the other `*.BIN` files. Nothing in the pipeline writes back to these files.

## Step 1 — Split MCB pair into per-scene bundles

`MCB0.BIN` is the index; `MCB1.BIN` holds payloads. `mcb.py` dumps every populated slot as `s{section:02d}_e{entry:03d}.bin`.

```bash
python -m tools.resource_extract.v2.mcb \
    --mcb0 MCB0.BIN --mcb1 MCB1.BIN \
    --dst out/mcb
```

Result: `out/mcb/s01_e011.bin`, `s01_e012.bin`, … (191 scenes).

## Step 2 — Unpack each scene bundle into typed resources

`mcb_unpack_all.py` LZ-decodes every record in each bundle and routes by magic:

- `PSM2` → `map_<rid>.psm2` + `.obj`
- `PSC3` → `grp_<rid>.psc3` + `.obj` + `.mtl`
- `BMPA` → `tex_<rid>.png`
- everything else → `<cat>_<rid>.bin`

```bash
python -m tools.resource_extract.v2.mcb_unpack_all \
    --src out/mcb \
    --dst out/target_all
```

Result: `out/target_all/<scene>/{grp_*.psc3, map_*.psm2, tex_*.png, _manifest.txt, …}`.

## Step 3 — Emit per-scene map glTF

`psm2_gltf.py` converts a single PSM2 file (with sibling textures) to glTF + .bin + .png. Run it once per scene against that scene's `map_*.psm2` file (`--src` accepts a directory and grabs every `.psm2` in it; the textures must live in the same directory, which `mcb_unpack_all` guarantees).

```bash
for scene in out/target_all/*/; do
    name=$(basename "$scene")
    python -m tools.resource_extract.v2.psm2_gltf \
        --src "$scene" \
        --dst "out/map_gltf_all/$name"
done
```

Result: `out/map_gltf_all/<scene>/map_<rid>.gltf` + `.bin` + `tex_*.png` for each of the 191 scenes.

## Step 4 — Dedupe-and-export character/prop models

`psc3_export_all.py` walks every `out/target_all/<scene>/grp_*.psc3`, hashes each by SHA-256 of file bytes, keeps one canonical copy per `(basename, sha6)`, parses its animation table, and emits one glTF set per `aid` via `psc3_gltf_anim.emit_animated`. Each model also gets a `_scenes.json` manifest enumerating every scene that referenced it; a top-level `_index.json` mirrors this for the viewer.

```bash
python -m tools.resource_extract.v2.psc3_export_all \
    --src out/target_all \
    --dst out/models
```

Result:

```
out/models/
  _index.json                          # model_name -> {sha6, scenes[], scene_count, aid_count}
  <grp_name>/
    _scenes.json                       # full per-model manifest
    aid0/<grp_name>.gltf + .bin + tex_*.png
    aid1/...
    ...
```

If two PSC3s share a basename but differ in bytes, both are emitted with the suffix `<name>__<sha6>`. The full run took <1 minute on a workstation and produced 281 unique models from 2122 source PSC3 files.

## Step 5 — Serve

```bash
python -m tools.map_viewer.server
```

Defaults are `--root out/map_gltf_all` and `--models-root out/models`. `/` is the map viewer; `/models` (alias `/animations`) is the model/animation viewer.

## Optional flags

- `--limit N` on `psc3_export_all` — process only the first N scenes (smoke test).
- `--skip-existing` on `psc3_export_all` — skip models whose output dir already exists.
- `--no-decompress` on bundle extraction — useful for raw inspection only; do not pass this for the viewer pipeline.

## File-size expectations

| Tree                | Approx. size | Notes                            |
| ------------------- | ------------ | -------------------------------- |
| `out/mcb/`          | ~120 MiB     | LZ-compressed bundles            |
| `out/target_all/`   | ~600 MiB     | Decoded PSM2/PSC3/PNG per scene  |
| `out/map_gltf_all/` | ~99 MiB      | 191 scenes                       |
| `out/models/`       | ~540 MiB     | 1839 glTF sets across 281 models |

All `out/` paths are gitignored.

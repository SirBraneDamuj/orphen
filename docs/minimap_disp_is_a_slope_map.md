# "MINI MAP DISP" is a slope map, and only its GWORK line survives

The debug menu's `ON :MINI MAP DISP` row (string at `0x0034D5D8`, set by
`FUN_00268d30`) toggles bit 2 of `DAT_003555DD` (`bGpffffb66d`; gp `0xffffb66d`
against the `0x00359F70` base). Turning it on in retail produces exactly one
visible thing: a `GWORK: 48.75%` line in the top-left corner. There is no map.

Two separate features hang off that one bit, and only the readout still works.

## The GWORK line is arena usage, not anything map-shaped

`FUN_002239c8:131` (`0x00223D68`-`0x00223DC8`):

```c
if ((bGpffffb66d & 4) != 0) {
  uVar2 = (uint)(((iGpffffb7bc + -0xdc9a00) / 100) * 10000) / 0x1ae14;
  FUN_002681c0(0x34be50, uVar2 / 100, uVar2 % 100);   // "GWORK:%d.%d%%\n"
}
```

`uGpffffb7bc` (`0x0035572C`) is the global bump-allocator cursor — the one
`FUN_0021ad00`, `FUN_0021be58`, `FUN_0021c850`, `FUN_0021fa88` and the PSB4 /
PSM2 loaders walk forward, and that `FUN_00211b80` builds GS packets on top of.
The arena runs `0x00DC9A00` to `0x01849A00`:

- `0xdc9a00` is the base, subtracted here and used as the low bound of
  `FUN_00223038`'s range check.
- `0x1ae14` is `110100`, which is exactly `0xA80000 / 100`, so the divisor
  fixes the size at `0xA80000` = 11,010,048 bytes (10.5 MiB) and the top at
  `0x01849A00`. `mv3_play_movie_by_id` confirms it independently: it refuses a
  movie buffer when `0x18499ff < uGpffffb7bc`.

So the readout is `cursor - 0x00DC9A00` as a percentage of 10.5 MiB, printed to
two decimals. Verified on hardware in `s01_e024`: the cursor read `0x012E8428`,
which is 5,368,360 bytes used, and the screen read `GWORK:48.75%`.

`GWORK` is also the name in the game's own error strings — `ER_GWORK_OV`,
`ER_GWORK_BUF_OV [onm_load]`, `ER_GWORK_OV [route_work]`,
`ER_GWORK_OV [mpeg_work]` — all of which are this arena overflowing.

## The map half is a slope-edge extractor, and it still runs

`FUN_0022dd60(0)` runs whenever the bit is turned on, and again on every scene
transition while it is on (`FUN_002239c8:30`). It builds, out of the map's
collision mesh, the list of edges where the surface turns sharply — the
silhouette you would draw as a floor plan.

The arrays are the PSM2 map ones, not entity ones:

| gp | address | contents |
| --- | --- | --- |
| `0xffffb714` | `0x00355684` | vertex count |
| `0xffffb718` | `0x00355688` | primitive count |
| `0xffffb73c` | `0x003556AC` | the 0x80-stride records (`DRecord80`), `+0x70` = `primitiveFlags` |
| `0xffffb740` | `0x003556B0` | the 0x78-stride records (`DRecord78`), `+0x08` = 4 vertex indices, `+0x50`/`+0x60` = the two unit normals |
| — | `0x0035569C` | vertex positions, stride 0x10 |

Three passes:

- **`FUN_0022de88`** fills a vertex→primitive table at `0x01849A00` with
  `0xFFFF`: one 0x20-byte row per vertex, so 16 owner slots each.
- **`FUN_0022def0`** walks every primitive's 4 vertex indices, copies them into
  an 8-byte-per-primitive scratch buffer taken off the arena cursor, and
  appends the primitive index to each of those vertices' owner rows. That is
  the adjacency the edge walk needs.
- **`FUN_0022dfb0`** walks each primitive's up to 4 edges. `FUN_0022e2a0` finds
  the primitive sharing both endpoints; `FUN_0022e1c8` blanks the shared edge in
  the neighbour's scratch so it is not emitted twice, and reports whether this
  edge is still live. The angle is `999` when there is no neighbour — a mesh
  boundary always counts — otherwise it is the angle between the two
  primitives' normals in degrees, via `FUN_0022e340` + `FUN_0022e438`, minus 10
  when the neighbour is a quad (`primitiveFlags & 2`, which is also what picks
  `+0x50` vs `+0x60`). Edges over 49 degrees are appended to a list of 8-byte
  `{v0, v1, angleDegrees}` records at `uGpffffbc7c`, counted in `sGpffffbc80`.

Measured in `s01_e024`: 1820 vertices, 1630 primitives, **1573 edges kept**.

Note where the vertex→primitive table lands. `0x01849A00` is *past* the top of
the arena, and it is the same shared staging buffer `FUN_00204370`,
`FUN_00204818` and `FUN_002025e0` load files into — so the table is live only
until the next disc read. That is a dev-build arrangement, not a shipping one.

## The draw is dead code, and unfinished

`FUN_0022dd60(1)` is the render half. Ghidra finds three calls to
`FUN_0022dd60` in the whole binary — `0x00223A68`, `0x00223BAC` and
`0x00269F8C` — and **all three pass 0**. Nothing in retail ever asks it to
draw.

Forcing it proves why. Patching `0x00223FAC` to `jal FUN_0022dd60` / `li a0,1`
(in place of the `FUN_0020C290` backdrop call, so it runs at the end of the
draw list) prints `SLOPE>50` — the string at `0x0034C1C8` is `"SLOPE>%d\n"`,
called with `0x32`, which is the 49-degree threshold stated as "greater than
50". The extraction is fine. Nothing is drawn.

The reason is that the packet is never kicked. Every other builder on this path
— `FUN_002036d8`, `FUN_002190f8`, `FUN_0020E558`, `FUN_002152D0` — fills the
scratchpad staging packet at `DAT_00355724` (`0x70000010`), writes a DMA target
into `+0x0C`, and then calls `FUN_00207DE8` to submit it. `FUN_0022e578` (one
grey `0xC8C8C8C8` two-vertex line per edge) and `FUN_0022e7b8` (a four-vertex
blue `0xC8C80000` player marker at `±DAT_003524FC` = 0.3) fill the same staging
packet, write `0xC80` into `+0x0C` where a DMA address belongs, and return.
`FUN_0022e7b0`, the "process" step `FUN_0022dd60` calls first, is a stub that
returns 0.

The transform is complete, for what it is worth. `FUN_0022e638` composes
translate(`DAT_0031C210`, `DAT_0031C214`) × scale(`DAT_0031C218 + 10`) ×
rotate(`DAT_0031C21C`, which mode 1 sets to the negated camera yaw) into the
matrix at `0x0058BC80`; `FUN_0022e6e8` subtracts the player position
(`DAT_0058BED0`..`D8`), transforms, and keeps components 0 and 1 — the ground
plane is XY with Z up, the same convention as `DRecord78::slopeAngle`. Depth is
`DAT_003524F8` = 0.99990, a 2D-overlay z. The pan/zoom globals `DAT_0031C210`,
`_214` and `_218` are written to 0 by mode 0 and read by nothing else, so
whatever moved the view is gone too.

## What this is worth to the port

The readout is not portable in any meaningful sense: the port allocates with
`std::vector`, there is no 10.5 MiB bump arena, and a synthesised percentage
would be a number that means nothing.

The slope map is a different matter — everything it reads is already in
`Psm2RuntimeState` (`DAT_0035569c_sectionCRecords`,
`DAT_003556b0_dRecords78::vertexIndices` / `unitNormal`,
`DAT_003556ac_dRecords80::primitiveFlags`), so the extraction ports as-is. But
it would be a *new* debug overlay, not a ported behaviour: retail draws nothing,
so there is no original to match and no fidelity argument for adding it.
